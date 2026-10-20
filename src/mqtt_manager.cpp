#include "mqtt_manager.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <esp_system.h>

#include "config.h"
#include "energy_topics.h"
#include "hivemq_ca.h"
#include "remote_command.h"
#include "telemetry_json.h"

namespace
{
    WiFiClientSecure secureClient;
    PubSubClient mqttClient(secureClient);

    QueueHandle_t commandQueue = nullptr;
    bool timeSynchronized = false;
    uint32_t subscribedAtMs = 0;

    // Runs inside mqttClient.loop() on NetworkTask. It only validates and
    // enqueues; it never touches GPIO or protection state, and it does no
    // blocking work (queue send has zero wait).
    void mqttCallback(char* topic, byte* payload, unsigned int length)
    {
        const RemoteCommandResult parsed = parseRemoteCommand(
            topic, payload, length, MQTT_TOPIC_CMD_RESET, MQTT_TOPIC_CMD_ACK);

        if (parsed.verdict != RemoteCommandVerdict::ACCEPTED)
        {
            Serial.printf("[MQTT] Rejected command: %s\n",
                          remoteCommandVerdictName(parsed.verdict));
            return;
        }

        if (commandInHoldoff(millis(), subscribedAtMs, MQTT_COMMAND_HOLDOFF_MS))
        {
            Serial.printf("[MQTT] Dropped command inside %lu ms post-subscribe hold-off "
                          "(possible retained message)\n",
                          (unsigned long)MQTT_COMMAND_HOLDOFF_MS);
            return;
        }

        CliCommand command{
            parsed.type == RemoteCommandType::RESET
                ? CommandType::REMOTE_RESET
                : CommandType::REMOTE_ACK};

        Serial.printf("[MQTT] Command accepted: %s\n", topic);

        // A dropped command is harmless: reset/ack are idempotent requests
        // that the operator can resend, and the protection interlock does
        // not depend on them.
        if (commandQueue == nullptr ||
            xQueueSend(commandQueue, &command, 0) != pdPASS)
        {
            Serial.println("[MQTT] Command dropped: queue full");
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

    subscribedAtMs = millis();
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
    bool relayCommandedOn,
    bool relayOutputEnabled
)
{
    if (!mqttClient.connected())
        return false;

    TelemetryInput input{};
    input.deviceId = MQTT_DEVICE_ID;
    input.firmwareVersion = FIRMWARE_VERSION;
    input.measurement = m;
    input.protection = p;
    input.uptimeMs = millis();
    input.measurementAgeMs = static_cast<uint32_t>(input.uptimeMs - m.timestamp);
    input.relayCommandedOn = relayCommandedOn;
    input.relayOutputEnabled = relayOutputEnabled;

    char payload[768];

    if (buildTelemetryJson(payload, sizeof(payload), input) < 0)
    {
        Serial.println("[MQTT] Telemetry payload did not fit buffer");
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
