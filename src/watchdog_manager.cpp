#include "watchdog_manager.h"

#include <esp_task_wdt.h>

// esp_task_wdt_config_t / esp_task_wdt_init(const config*) is the ESP-IDF 5
// API shipped with arduino-esp32 3.x. Older cores use a different signature.
#if !defined(ESP_ARDUINO_VERSION_MAJOR) || ESP_ARDUINO_VERSION_MAJOR < 3
#error "watchdog_manager.cpp requires arduino-esp32 3.x (ESP-IDF 5 task watchdog API)"
#endif

#include "config.h"
#include "heartbeat_monitor.h"

namespace
{
    // Application-level heartbeat table (pure logic in lib/energy_core).
    HeartbeatMonitor monitor(HEARTBEAT_COUNT);
    uint8_t registrationFailures = 0;

    const char* heartbeatNames[HEARTBEAT_COUNT] =
    {
        "SensorTask",
        "ControlTask",
        "DisplayTask",
        "NetworkTask",
        "WatchdogTask",
        "CliTask"
    };

    portMUX_TYPE heartbeatMux = portMUX_INITIALIZER_UNLOCKED;
}

bool watchdogManagerBegin()
{
    esp_task_wdt_config_t config{};
    config.timeout_ms = WATCHDOG_TIMEOUT_MS;
    config.idle_core_mask = (1U << portNUM_PROCESSORS) - 1U;
    config.trigger_panic = true;

    esp_err_t result = esp_task_wdt_init(&config);

    if (result == ESP_ERR_INVALID_STATE)
    {
        result = esp_task_wdt_reconfigure(&config);
    }

    if (result != ESP_OK)
    {
        Serial.printf("[WATCHDOG] Initialization failed: %d\n",
                      static_cast<int>(result));
        return false;
    }

    Serial.printf("[WATCHDOG] TWDT ready: %lu ms\n",
                  (unsigned long)WATCHDOG_TIMEOUT_MS);
    return true;
}

bool watchdogManagerRegisterCurrentTask(const char* taskName)
{
    const esp_err_t result = esp_task_wdt_add(nullptr);

    // ESP_ERR_INVALID_STATE means "already subscribed", which is fine.
    // Anything else (including ESP_ERR_INVALID_ARG / NO_MEM) means this
    // task is NOT covered by the task watchdog, so it is counted and shown
    // by the 'watchdog' CLI command instead of being reported as success.
    if (result == ESP_OK || result == ESP_ERR_INVALID_STATE)
    {
        Serial.printf("[WATCHDOG] Registered: %s\n", taskName);
        return true;
    }

    portENTER_CRITICAL(&heartbeatMux);
    ++registrationFailures;
    portEXIT_CRITICAL(&heartbeatMux);

    Serial.printf("[WATCHDOG] Registration FAILED: %s (%d) - not covered by TWDT\n",
                  taskName, static_cast<int>(result));
    return false;
}

uint8_t watchdogManagerRegistrationFailures()
{
    return registrationFailures;
}

void watchdogManagerHeartbeat(HeartbeatId id)
{
    if (id >= HEARTBEAT_COUNT)
        return;

    const uint32_t now = millis();

    portENTER_CRITICAL(&heartbeatMux);
    monitor.beat(id, now);
    portEXIT_CRITICAL(&heartbeatMux);
}

uint32_t watchdogManagerHeartbeatAge(HeartbeatId id)
{
    if (id >= HEARTBEAT_COUNT)
        return UINT32_MAX;

    const uint32_t now = millis();

    portENTER_CRITICAL(&heartbeatMux);
    const uint32_t age = monitor.ageMs(id, now);
    portEXIT_CRITICAL(&heartbeatMux);

    return age;
}

const char* watchdogManagerHeartbeatName(HeartbeatId id)
{
    return id < HEARTBEAT_COUNT ? heartbeatNames[id] : "UnknownTask";
}

bool watchdogManagerHealthy(uint32_t timeoutMs)
{
    const uint32_t now = millis();

    portENTER_CRITICAL(&heartbeatMux);
    const bool healthy = monitor.firstStale(now, timeoutMs) == static_cast<size_t>(-1);
    portEXIT_CRITICAL(&heartbeatMux);

    return healthy;
}

void watchdogManagerPrintStatus(uint32_t timeoutMs)
{
    for (uint8_t i = 0; i < HEARTBEAT_COUNT; ++i)
    {
        const uint32_t age = watchdogManagerHeartbeatAge(
            static_cast<HeartbeatId>(i));

        Serial.printf(
            "[WATCHDOG] %-14s : %s",
            watchdogManagerHeartbeatName(
                static_cast<HeartbeatId>(i)),
            age == UINT32_MAX ? "NO HEARTBEAT" : "OK"
        );

        if (age != UINT32_MAX)
        {
            Serial.printf(" (%lu ms)", (unsigned long)age);
        }

        if (age > timeoutMs)
        {
            Serial.print(" [STALE]");
        }

        Serial.println();
    }
}
