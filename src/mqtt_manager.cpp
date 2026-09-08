#include "mqtt_manager.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <esp_system.h>

#include "config.h"
#include "hivemq_ca.h"

namespace
{
    WiFiClientSecure secureClient;
    PubSubClient mqttClient(secureClient);

    QueueHandle_t commandQueue = nullptr;
    bool timeSynchronized = false;

    void mqttCallback(char* topic, byte* payload, unsigned int length)
    {
        char message[32]{};

        const size_t copyLength =
            min(static_cast<size_t>(length), sizeof(message) - 1U);

        memcpy(message, payload, copyLength);
        message[copyLength] = '\0';

        CliCommand command{CommandType::NONE};

        if (strcmp(topic, MQTT_TOPIC_CMD_RESET) == 0 &&
            strcmp(message, "reset") == 0)
        {
            command.type = CommandType::REMOTE_RESET;
        }
        else if (strcmp(topic, MQTT_TOPIC_CMD_ACK) == 0 &&
                 strcmp(message, "ack") == 0)
        {
            command.type = CommandType::REMOTE_ACK;
        }
        else
        {
            Serial.println("[MQTT] Rejected command");
            return;
        }

        Serial.printf("[MQTT] Command received: %s -> %s\n",
                      topic, message);

        if (commandQueue == nullptr ||
            xQueueSend(commandQueue, &command, 0) != pdPASS)
        {
            Serial.println("[MQTT] Command queue full");
        }
    }

    void makeClientId(char* buffer, size_t size)
    {
        const uint64_t chipId = ESP.getEfuseMac();

        snprintf(
            buffer,
            size,
            "%s-%04X%08X",
            MQTT_DEVICE_ID,
            static_cast<uint16_t>(chipId >> 32),
            static_cast<uint32_t>(chipId)
        );
    }
}

void mqttManagerBegin(QueueHandle_t queue)
{
    commandQueue = queue;

    secureClient.setCACert(HIVEMQ_ROOT_CA);

    mqttClient.setServer(MQTT_BROKER_HOST, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setKeepAlive(30);
    mqttClient.setBufferSize(1024);

    // Available in current PubSubClient releases. If an older release
    // rejects this API, update the library version in platformio.ini.
    mqttClient.setSocketTimeout(5);

    Serial.println("[MQTT] Manager initialized with TLS certificate validation");
}

void mqttManagerSetTimeSynchronized(bool synchronized)
{
    timeSynchronized = synchronized;
}

bool mqttManagerConnect()
{
    if (WiFi.status() != WL_CONNECTED || !timeSynchronized)
    {
        return false;
    }

    char clientId[64]{};
    makeClientId(clientId, sizeof(clientId));

    Serial.printf("[MQTT] Connecting: %s\n", clientId);

    const bool connected = mqttClient.connect(
        clientId,
        MQTT_USERNAME,
        MQTT_PASSWORD,
        MQTT_TOPIC_AVAILABILITY,
        1,
        true,
        "offline"
    );

    if (!connected)
    {
        Serial.printf("[MQTT] Connection failed, state=%d\n",
                      mqttClient.state());
        return false;
    }

    if (!mqttClient.publish(MQTT_TOPIC_AVAILABILITY, "online", true))
    {
        Serial.println("[MQTT] Failed to publish online status");
    }

    const bool resetSubscribed = mqttClient.subscribe(MQTT_TOPIC_CMD_RESET);
    const bool ackSubscribed = mqttClient.subscribe(MQTT_TOPIC_CMD_ACK);

    if (!resetSubscribed || !ackSubscribed)
    {
        Serial.println("[MQTT] WARNING: command subscription failed");
    }

    Serial.println("[MQTT] Connected");
    return true;
}

void mqttManagerLoop()
{
    if (mqttClient.connected())
    {
        mqttClient.loop();
    }
}

bool mqttManagerIsConnected()
{
    return mqttClient.connected();
}

bool mqttManagerPublishTelemetry(
    const Measurement& m,
    const ProtectionSnapshot& p,
    bool relayOn
)
{
    if (!mqttClient.connected())
        return false;

    char payload[768]{};

    const int written = snprintf(
        payload,
        sizeof(payload),
        "{"
        "\"device_id\":\"%s\","
        "\"firmware_version\":\"%s\","
        "\"voltage\":%.2f,"
        "\"current\":%.3f,"
        "\"power\":%.2f,"
        "\"energy\":%.3f,"
        "\"apparent_power\":%.2f,"
        "\"power_factor\":%.3f,"
        "\"valid\":%s,"
        "\"state\":\"%s\","
        "\"fault\":\"%s\","
        "\"fault_active\":%s,"
        "\"fault_acknowledged\":%s,"
        "\"relay_on\":%s,"
        "\"source\":\"%s\","
        "\"uptime_ms\":%lu"
        "}",
        MQTT_DEVICE_ID,
        FIRMWARE_VERSION,
        m.voltage,
        m.current,
        m.power,
        m.energy,
        m.apparentPower,
        m.powerFactor,
        m.valid ? "true" : "false",
        systemStateName(p.state),
        faultCodeName(p.fault.activeFault),
        p.fault.active ? "true" : "false",
        p.fault.acknowledged ? "true" : "false",
        relayOn ? "true" : "false",
        measurementSourceName(),
        (unsigned long)millis()
    );

    if (written < 0 ||
        static_cast<size_t>(written) >= sizeof(payload))
    {
        Serial.println("[MQTT] Telemetry payload truncated");
        return false;
    }

    const bool ok = mqttClient.publish(MQTT_TOPIC_TELEMETRY, payload);

    if (!ok)
        Serial.println("[MQTT] Telemetry publish failed");

    return ok;
}

bool mqttManagerPublishState(SystemState state)
{
    if (!mqttClient.connected())
        return false;

    return mqttClient.publish(
        MQTT_TOPIC_STATE,
        systemStateName(state),
        true
    );
}

bool mqttManagerPublishFault(const ProtectionSnapshot& p)
{
    if (!mqttClient.connected())
        return false;

    return mqttClient.publish(
        MQTT_TOPIC_FAULT,
        faultCodeName(p.fault.activeFault),
        true
    );
}
