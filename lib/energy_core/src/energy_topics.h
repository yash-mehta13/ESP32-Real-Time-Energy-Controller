#pragma once
#pragma once

#include <stddef.h>

// Topic names are derived from MQTT_DEVICE_ID at compile time so that the
// device ID and the topic namespace cannot drift apart. The including
// translation unit must #define MQTT_DEVICE_ID "<id>" first (the firmware
// does this in config.h).

#ifndef MQTT_DEVICE_ID
#error "MQTT_DEVICE_ID must be defined (string literal) before including energy_topics.h"
#endif

#define MQTT_TOPIC_ROOT "energy/" MQTT_DEVICE_ID

#define MQTT_TOPIC_TELEMETRY    MQTT_TOPIC_ROOT "/telemetry"
#define MQTT_TOPIC_STATE        MQTT_TOPIC_ROOT "/status/state"
#define MQTT_TOPIC_FAULT        MQTT_TOPIC_ROOT "/fault/code"
#define MQTT_TOPIC_AVAILABILITY MQTT_TOPIC_ROOT "/status/availability"
#define MQTT_TOPIC_CMD_RESET    MQTT_TOPIC_ROOT "/cmd/reset"
#define MQTT_TOPIC_CMD_ACK      MQTT_TOPIC_ROOT "/cmd/ack"

// A device ID is exactly one MQTT topic level: 1..32 characters of
// [A-Za-z0-9_-]. No '/', '+', '#', spaces or empty IDs.
constexpr bool topicSegmentCharOk(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

constexpr bool topicSegmentValid(const char* s, size_t i = 0)
{
    return s[i] == '\0' ? (i > 0 && i <= 32)
                        : (i < 32 && topicSegmentCharOk(s[i]) &&
                           topicSegmentValid(s, i + 1));
}

static_assert(topicSegmentValid(MQTT_DEVICE_ID),
              "MQTT_DEVICE_ID must be 1-32 chars of [A-Za-z0-9_-]");
