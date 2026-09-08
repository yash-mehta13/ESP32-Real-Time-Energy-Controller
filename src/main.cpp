#define BLYNK_PRINT Serial

#include "config.h"

#include <Arduino.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>
#include <esp_task_wdt.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "actuator.h"
#include "diagnostics.h"
#include "measurement.h"
#include "mqtt_manager.h"
#include "protection.h"
#include "watchdog_manager.h"

// -----------------------------------------------------------------------------
// Application synchronization
// -----------------------------------------------------------------------------

namespace
{
    SemaphoreHandle_t measurementMutex = nullptr;
    QueueHandle_t measurementQueue = nullptr;
    QueueHandle_t commandQueue = nullptr;

    Measurement latestMeasurement{};

    LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, LCD_COLUMNS, LCD_ROWS);

    TaskHandle_t sensorTaskHandle = nullptr;
    TaskHandle_t controlTaskHandle = nullptr;
    TaskHandle_t displayTaskHandle = nullptr;
    TaskHandle_t networkTaskHandle = nullptr;
    TaskHandle_t watchdogTaskHandle = nullptr;
    TaskHandle_t cliTaskHandle = nullptr;

    bool wifiConnected = false;
    bool blynkConnected = false;
    bool timeSynchronized = false;

    uint32_t lastWiFiAttempt = 0;
    uint32_t lastBlynkAttempt = 0;
    uint32_t lastMqttAttempt = 0;
    uint32_t lastTelemetry = 0;
    uint32_t lastNtpCheck = 0;
    bool mqttWasConnected = false;

    void fatalStop(const char* message)
    {
        Serial.printf("[FATAL] %s\n", message);
        actuatorApplyState(SystemState::FAULT);

        while (true)
        {
            delay(1000);
        }
    }

    bool copyLatestMeasurement(Measurement& output)
    {
        if (measurementMutex == nullptr)
            return false;

        if (xSemaphoreTake(measurementMutex, pdMS_TO_TICKS(50)) != pdTRUE)
            return false;

        output = latestMeasurement;
        xSemaphoreGive(measurementMutex);
        return true;
    }

    NetworkSnapshot getNetworkSnapshot()
    {
        NetworkSnapshot snapshot{};
        snapshot.wifiConnected = wifiConnected;
        snapshot.timeSynchronized = timeSynchronized;
        snapshot.blynkConnected = blynkConnected;
        snapshot.mqttConnected = mqttManagerIsConnected();
        snapshot.rssi = wifiConnected ? WiFi.RSSI() : 0;
        snapshot.ip = wifiConnected ? WiFi.localIP() : IPAddress(0, 0, 0, 0);
        return snapshot;
    }

    bool isNtpTimeValid()
    {
        const time_t now = time(nullptr);
        return now > 1700000000;
    }

    void requestCommand(CommandType type)
    {
        CliCommand command{type};

        if (xQueueSend(commandQueue, &command, pdMS_TO_TICKS(50)) != pdPASS)
        {
            Serial.println("[CLI] Command queue full");
        }
    }

    void applyProtectionResult(const ProtectionResult& result)
    {
        if (result.stateChanged || result.faultChanged)
        {
            actuatorApplyState(result.state);

            Serial.printf(
                "[SYSTEM] State=%s | Fault=%s\n",
                systemStateName(result.state),
                faultCodeName(result.fault)
            );
        }
    }

    // -------------------------------------------------------------------------
    // Sensor task
    // -------------------------------------------------------------------------

    void sensorTask(void*)
    {
        Serial.println("[SensorTask] Started");
        watchdogManagerRegisterCurrentTask("SensorTask");

        TickType_t lastWake = xTaskGetTickCount();

        for (;;)
        {
            const Measurement measurement = readMeasurement();

            if (xQueueSend(
                    measurementQueue,
                    &measurement,
                    pdMS_TO_TICKS(50)) != pdPASS)
            {
                Serial.println("[SensorTask] WARNING: measurement queue full");
            }

            if (xSemaphoreTake(
                    measurementMutex,
                    pdMS_TO_TICKS(50)) == pdTRUE)
            {
                latestMeasurement = measurement;
                xSemaphoreGive(measurementMutex);
            }

            watchdogManagerHeartbeat(HEARTBEAT_SENSOR);
            esp_task_wdt_reset();

            vTaskDelayUntil(
                &lastWake,
                pdMS_TO_TICKS(SENSOR_PERIOD_MS)
            );
        }
    }

    // -------------------------------------------------------------------------
    // Control task
    // -------------------------------------------------------------------------

    void controlTask(void*)
    {
        Serial.println("[ControlTask] Started");
        watchdogManagerRegisterCurrentTask("ControlTask");

        for (;;)
        {
            Measurement measurement{};

            if (xQueueReceive(
                    measurementQueue,
                    &measurement,
                    pdMS_TO_TICKS(CONTROL_PERIOD_MS)) == pdPASS)
            {
                const ProtectionResult result =
                    protectionUpdate(measurement);

                applyProtectionResult(result);
            }

            CliCommand command{};

            while (xQueueReceive(commandQueue, &command, 0) == pdPASS)
            {
                switch (command.type)
                {
                    case CommandType::RESET:
                    case CommandType::REMOTE_RESET:
                        if (command.type == CommandType::REMOTE_RESET)
                            Serial.println("[MQTT] Remote reset command executing");

                        if (protectionReset())
                            actuatorApplyState(SystemState::INIT);
                        break;

                    case CommandType::ACK:
                    case CommandType::REMOTE_ACK:
                        if (command.type == CommandType::REMOTE_ACK)
                            Serial.println("[MQTT] Remote ACK command executing");

                        protectionAcknowledge();
                        break;

                    default:
                        break;
                }
            }

            watchdogManagerHeartbeat(HEARTBEAT_CONTROL);
            esp_task_wdt_reset();
        }
    }

    // -------------------------------------------------------------------------
    // Display task
    // -------------------------------------------------------------------------

    void displayTask(void*)
    {
        Serial.println("[DisplayTask] Started");
        watchdogManagerRegisterCurrentTask("DisplayTask");

        TickType_t lastWake = xTaskGetTickCount();

        for (;;)
        {
            Measurement measurement{};
            copyLatestMeasurement(measurement);

            const ProtectionSnapshot protection =
                protectionGetSnapshot();

            lcd.clear();

            if (!measurement.valid)
            {
                lcd.setCursor(0, 0);
                lcd.print("Sensor Error");
                lcd.setCursor(0, 1);
                lcd.print("Check PZEM");
            }
            else
            {
                lcd.setCursor(0, 0);
                lcd.printf(
                    "V:%5.1f I:%4.2f",
                    measurement.voltage,
                    measurement.current
                );

                lcd.setCursor(0, 1);
                lcd.printf(
                    "P:%5.0f %s",
                    measurement.power,
                    systemStateName(protection.state)
                );
            }

            watchdogManagerHeartbeat(HEARTBEAT_DISPLAY);
            esp_task_wdt_reset();

            vTaskDelayUntil(
                &lastWake,
                pdMS_TO_TICKS(DISPLAY_PERIOD_MS)
            );
        }
    }

    // -------------------------------------------------------------------------
    // Network task
    // -------------------------------------------------------------------------

    void networkTask(void*)
    {
        Serial.println("[NetworkTask] Started");
        watchdogManagerRegisterCurrentTask("NetworkTask");

        SystemState publishedState = SystemState::INIT;
        FaultCode publishedFault = FaultCode::NONE;

        for (;;)
        {
            const uint32_t now = millis();

            // Wi-Fi ------------------------------------------------------------
            if (WiFi.status() != WL_CONNECTED)
            {
                if (wifiConnected)
                {
                    wifiConnected = false;
                    blynkConnected = false;
                    timeSynchronized = false;
                    mqttManagerSetTimeSynchronized(false);

                    mqttWasConnected = false;

                    Serial.println("[NETWORK] WiFi disconnected");
                }

                if (now - lastWiFiAttempt >= WIFI_RECONNECT_INTERVAL_MS)
                {
                    lastWiFiAttempt = now;
                    Serial.println("[NETWORK] Attempting WiFi connection...");
                    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
                }
            }
            else
            {
                if (!wifiConnected)
                {
                    wifiConnected = true;

                    Serial.printf(
                        "[NETWORK] WiFi connected. IP=%s RSSI=%ld dBm\n",
                        WiFi.localIP().toString().c_str(),
                        (long)WiFi.RSSI()
                    );

                    configTime(
                        NTP_GMT_OFFSET_SECONDS,
                        NTP_DAYLIGHT_OFFSET_SECONDS,
                        NTP_SERVER_1,
                        NTP_SERVER_2
                    );

                    timeSynchronized = false;
                }

                // NTP ----------------------------------------------------------
                if (!timeSynchronized &&
                    now - lastNtpCheck >= NTP_CHECK_INTERVAL_MS)
                {
                    lastNtpCheck = now;
                    timeSynchronized = isNtpTimeValid();

                    if (timeSynchronized)
                    {
                        Serial.println("[NTP] Time synchronized");
                    }
                }

                mqttManagerSetTimeSynchronized(timeSynchronized);

                // Blynk --------------------------------------------------------
                if (!Blynk.connected())
                {
                    blynkConnected = false;

                    if (now - lastBlynkAttempt >= BLYNK_RECONNECT_INTERVAL_MS)
                    {
                        lastBlynkAttempt = now;

                        if (Blynk.connect(BLYNK_CONNECT_TIMEOUT_MS))
                        {
                            blynkConnected = true;
                            Serial.println("[NETWORK] Blynk connected");
                        }
                    }
                }
                else
                {
                    blynkConnected = true;
                    Blynk.run();
                }

                // MQTT --------------------------------------------------------
                if (!mqttManagerIsConnected())
                {
                    if (timeSynchronized &&
                        now - lastMqttAttempt >= MQTT_RECONNECT_INTERVAL_MS)
                    {
                        lastMqttAttempt = now;
                        mqttManagerConnect();
                    }
                }
                else
                {
                    mqttManagerLoop();
                }

                // State/fault change publication ------------------------------
                const ProtectionSnapshot protection =
                    protectionGetSnapshot();

                const bool mqttNowConnected = mqttManagerIsConnected();

                if (mqttNowConnected && !mqttWasConnected)
                {
                    mqttManagerPublishState(protection.state);
                    mqttManagerPublishFault(protection);
                    publishedState = protection.state;
                    publishedFault = protection.fault.activeFault;
                }

                if (mqttNowConnected)
                {
                    if (protection.state != publishedState)
                    {
                        if (mqttManagerPublishState(protection.state))
                            publishedState = protection.state;
                    }

                    if (protection.fault.activeFault != publishedFault)
                    {
                        if (mqttManagerPublishFault(protection))
                            publishedFault = protection.fault.activeFault;
                    }
                }

                // Periodic telemetry ------------------------------------------
                if (now - lastTelemetry >= TELEMETRY_PERIOD_MS)
                {
                    lastTelemetry = now;

                    Measurement measurement{};
                    copyLatestMeasurement(measurement);

                    if (blynkConnected && measurement.valid)
                    {
                        Blynk.virtualWrite(V0, measurement.voltage);
                        Blynk.virtualWrite(V1, measurement.current);
                        Blynk.virtualWrite(V2, measurement.power);
                        Blynk.virtualWrite(V3, measurement.energy);
                        Blynk.virtualWrite(V4, measurement.apparentPower);
                        Blynk.virtualWrite(V5, measurement.powerFactor);
                    }

                    if (mqttManagerIsConnected())
                    {
                        mqttManagerPublishTelemetry(
                            measurement,
                            protection,
                            actuatorRelayIsOn()
                        );
                    }
                }
            }

            watchdogManagerHeartbeat(HEARTBEAT_NETWORK);
            esp_task_wdt_reset();

            vTaskDelay(pdMS_TO_TICKS(NETWORK_PERIOD_MS));
        }
    }

    // -------------------------------------------------------------------------
    // Watchdog supervisor task
    // -------------------------------------------------------------------------

    void watchdogTask(void*)
    {
        Serial.println("[WatchdogTask] Started");
        watchdogManagerRegisterCurrentTask("WatchdogTask");

        const uint32_t startup = millis();

        for (;;)
        {
            watchdogManagerHeartbeat(HEARTBEAT_WATCHDOG);
            esp_task_wdt_reset();

            if (millis() - startup >= WATCHDOG_STARTUP_GRACE_MS)
            {
                if (!watchdogManagerHealthy(HEARTBEAT_TIMEOUT_MS))
                {
                    Serial.println();
                    Serial.println("========================================");
                    Serial.println("         WATCHDOG FAILURE");
                    Serial.println("========================================");

                    watchdogManagerPrintStatus(HEARTBEAT_TIMEOUT_MS);

                    Serial.println("[WATCHDOG] Restarting system...");
                    delay(100);
                    esp_restart();
                }
            }

            vTaskDelay(
                pdMS_TO_TICKS(WATCHDOG_SUPERVISOR_PERIOD_MS)
            );
        }
    }

    // -------------------------------------------------------------------------
    // CLI task
    // -------------------------------------------------------------------------

    void cliTask(void*)
    {
        Serial.println("[CliTask] Started");
        watchdogManagerRegisterCurrentTask("CliTask");

        char buffer[CLI_BUFFER_SIZE]{};
        size_t index = 0;
        bool overflow = false;

        diagnosticsPrintHelp();
        Serial.print("\r\ndiag> ");

        for (;;)
        {
            while (Serial.available() > 0)
            {
                const char c = static_cast<char>(Serial.read());

                if (c == '\r' || c == '\n')
                {
                    if (index == 0 && !overflow)
                        continue;

                    Serial.println();

                    if (overflow)
                    {
                        Serial.println("[CLI] ERROR: command too long");
                    }
                    else
                    {
                        buffer[index] = '\0';
                        const CommandType command =
                            diagnosticsParseCommand(buffer);

                        switch (command)
                        {
                            case CommandType::HELP:
                                diagnosticsPrintHelp();
                                break;

                            case CommandType::STATUS:
                            {
                                Measurement m{};
                                copyLatestMeasurement(m);
                                diagnosticsPrintStatus(
                                    m,
                                    protectionGetSnapshot(),
                                    getNetworkSnapshot()
                                );
                                break;
                            }

                            case CommandType::MEASURE:
                            {
                                Measurement m{};
                                copyLatestMeasurement(m);
                                diagnosticsPrintMeasurement(m);
                                break;
                            }

                            case CommandType::FAULT:
                                diagnosticsPrintFault(
                                    protectionGetSnapshot()
                                );
                                break;

                            case CommandType::WATCHDOG:
                                diagnosticsPrintWatchdog();
                                break;

                            case CommandType::TASKS:
                                diagnosticsPrintTasks();
                                break;

                            case CommandType::UPTIME:
                                diagnosticsPrintUptime();
                                break;

                            case CommandType::VERSION:
                                diagnosticsPrintVersion();
                                break;

                            case CommandType::RESET:
                            case CommandType::ACK:
                                requestCommand(command);
                                break;

                            default:
                                Serial.printf(
                                    "[CLI] Unknown command: %s\n",
                                    buffer
                                );
                                break;
                        }
                    }

                    index = 0;
                    overflow = false;
                    Serial.print("\r\ndiag> ");
                    continue;
                }

                if (c == '\b' || c == 127)
                {
                    if (index > 0)
                    {
                        --index;
                        Serial.print("\b \b");
                    }
                    continue;
                }

                if (c < 32 || c > 126)
                    continue;

                if (index < sizeof(buffer) - 1)
                {
                    buffer[index++] = c;
                    Serial.print(c);
                }
                else
                {
                    overflow = true;
                }
            }

            watchdogManagerHeartbeat(HEARTBEAT_CLI);
            esp_task_wdt_reset();

            vTaskDelay(pdMS_TO_TICKS(CLI_TASK_PERIOD_MS));
        }
    }

    void createTasks()
    {
        if (xTaskCreatePinnedToCore(
                sensorTask, "SensorTask", SENSOR_TASK_STACK,
                nullptr, SENSOR_TASK_PRIORITY, &sensorTaskHandle, 1) != pdPASS)
            fatalStop("SensorTask creation failed");

        if (xTaskCreatePinnedToCore(
                controlTask, "ControlTask", CONTROL_TASK_STACK,
                nullptr, CONTROL_TASK_PRIORITY, &controlTaskHandle, 1) != pdPASS)
            fatalStop("ControlTask creation failed");

        if (xTaskCreatePinnedToCore(
                displayTask, "DisplayTask", DISPLAY_TASK_STACK,
                nullptr, DISPLAY_TASK_PRIORITY, &displayTaskHandle, 1) != pdPASS)
            fatalStop("DisplayTask creation failed");

        if (xTaskCreatePinnedToCore(
                networkTask, "NetworkTask", NETWORK_TASK_STACK,
                nullptr, NETWORK_TASK_PRIORITY, &networkTaskHandle, 0) != pdPASS)
            fatalStop("NetworkTask creation failed");

        if (xTaskCreatePinnedToCore(
                watchdogTask, "WatchdogTask", WATCHDOG_TASK_STACK,
                nullptr, WATCHDOG_TASK_PRIORITY, &watchdogTaskHandle, 0) != pdPASS)
            fatalStop("WatchdogTask creation failed");

        if (xTaskCreatePinnedToCore(
                cliTask, "CliTask", CLI_TASK_STACK,
                nullptr, CLI_TASK_PRIORITY, &cliTaskHandle, 0) != pdPASS)
            fatalStop("CliTask creation failed");

        diagnosticsSetTaskHandles(
            sensorTaskHandle,
            controlTaskHandle,
            displayTaskHandle,
            networkTaskHandle,
            watchdogTaskHandle,
            cliTaskHandle
        );
    }
}

void setup()
{
    Serial.begin(SERIAL_BAUD_RATE);
    delay(500);

    Serial.println();
    Serial.println("==========================================");
    Serial.println(" ESP32 REAL-TIME ENERGY CONTROLLER");
    Serial.println(" ESP32 + FreeRTOS + MQTT + Blynk");
    Serial.printf(" Firmware %s | %s\n", FIRMWARE_VERSION, FIRMWARE_BUILD);
    Serial.println("==========================================");

    measurementBegin();
    actuatorBegin();
    actuatorApplyState(SystemState::INIT);
    protectionBegin();

    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Energy Controller");
    lcd.setCursor(0, 1);
    lcd.print("Booting...");

    measurementMutex = xSemaphoreCreateMutex();
    measurementQueue = xQueueCreate(
        MEASUREMENT_QUEUE_LENGTH,
        sizeof(Measurement)
    );
    commandQueue = xQueueCreate(
        COMMAND_QUEUE_LENGTH,
        sizeof(CliCommand)
    );

    if (!measurementMutex)
        fatalStop("Measurement mutex creation failed");

    if (!measurementQueue)
        fatalStop("Measurement queue creation failed");

    if (!commandQueue)
        fatalStop("Command queue creation failed");

    if (!watchdogManagerBegin())
        fatalStop("Watchdog initialization failed");

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);

    Blynk.config(BLYNK_AUTH_TOKEN);

    mqttManagerBegin(commandQueue);

    lastWiFiAttempt = millis() - WIFI_RECONNECT_INTERVAL_MS;
    lastBlynkAttempt = millis() - BLYNK_RECONNECT_INTERVAL_MS;
    lastMqttAttempt = millis() - MQTT_RECONNECT_INTERVAL_MS;

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    createTasks();

    diagnosticsBegin();

    Serial.println("[SYSTEM] Initialization complete");
}

void loop()
{
    // Application execution is handled by FreeRTOS tasks.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
