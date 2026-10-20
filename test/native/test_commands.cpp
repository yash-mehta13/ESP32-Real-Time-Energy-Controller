#include "mini_test.h"
#include <string.h>

#define MQTT_DEVICE_ID "device01"
#include "energy_topics.h"
#include "remote_command.h"

namespace
{
    RemoteCommandResult parse(const char* topic, const char* payload)
    {
        return parseRemoteCommand(topic, reinterpret_cast<const uint8_t*>(payload),
                                  strlen(payload), MQTT_TOPIC_CMD_RESET,
                                  MQTT_TOPIC_CMD_ACK);
    }
}

TEST(reset_command_accepted)
{
    const RemoteCommandResult r = parse(MQTT_TOPIC_CMD_RESET, "reset");
    CHECK(r.verdict == RemoteCommandVerdict::ACCEPTED);
    CHECK(r.type == RemoteCommandType::RESET);
}

TEST(ack_command_accepted)
{
    const RemoteCommandResult r = parse(MQTT_TOPIC_CMD_ACK, "ack");
    CHECK(r.verdict == RemoteCommandVerdict::ACCEPTED);
    CHECK(r.type == RemoteCommandType::ACK);
}

TEST(ack_payload_on_reset_topic_is_rejected)
{
    const RemoteCommandResult r = parse(MQTT_TOPIC_CMD_RESET, "ack");
    CHECK(r.verdict == RemoteCommandVerdict::REJECTED_PAYLOAD);
    CHECK(r.type == RemoteCommandType::NONE);
}

TEST(reset_payload_on_ack_topic_is_rejected)
{
    CHECK(parse(MQTT_TOPIC_CMD_ACK, "reset").verdict == RemoteCommandVerdict::REJECTED_PAYLOAD);
}

TEST(wrong_or_foreign_topic_is_rejected)
{
    CHECK(parse("energy/device02/cmd/reset", "reset").verdict == RemoteCommandVerdict::REJECTED_TOPIC);
    CHECK(parse("energy/device01/cmd/reset/", "reset").verdict == RemoteCommandVerdict::REJECTED_TOPIC);
    CHECK(parse("energy/device01/telemetry", "reset").verdict == RemoteCommandVerdict::REJECTED_TOPIC);
    CHECK(parse("", "reset").verdict == RemoteCommandVerdict::REJECTED_TOPIC);
}

TEST(null_topic_is_rejected)
{
    const uint8_t p[] = {'r','e','s','e','t'};
    const RemoteCommandResult r = parseRemoteCommand(nullptr, p, 5, MQTT_TOPIC_CMD_RESET, MQTT_TOPIC_CMD_ACK);
    CHECK(r.verdict == RemoteCommandVerdict::REJECTED_TOPIC);
}

TEST(case_whitespace_and_prefix_variants_are_rejected)
{
    CHECK(parse(MQTT_TOPIC_CMD_RESET, "RESET").verdict == RemoteCommandVerdict::REJECTED_PAYLOAD);
    CHECK(parse(MQTT_TOPIC_CMD_RESET, "reset\n").verdict == RemoteCommandVerdict::REJECTED_PAYLOAD);
    CHECK(parse(MQTT_TOPIC_CMD_RESET, " reset").verdict == RemoteCommandVerdict::REJECTED_PAYLOAD);
    CHECK(parse(MQTT_TOPIC_CMD_RESET, "resets").verdict == RemoteCommandVerdict::REJECTED_PAYLOAD);
    CHECK(parse(MQTT_TOPIC_CMD_RESET, "rese").verdict == RemoteCommandVerdict::REJECTED_PAYLOAD);
    CHECK(parse(MQTT_TOPIC_CMD_RESET, "").verdict == RemoteCommandVerdict::REJECTED_PAYLOAD);
}

TEST(embedded_nul_does_not_pass_as_reset)
{
    const uint8_t p[] = {'r','e','s','e','t','\0','x'};
    const RemoteCommandResult r = parseRemoteCommand(MQTT_TOPIC_CMD_RESET, p, sizeof(p),
                                                     MQTT_TOPIC_CMD_RESET, MQTT_TOPIC_CMD_ACK);
    CHECK(r.verdict == RemoteCommandVerdict::REJECTED_PAYLOAD);
}

TEST(oversized_payload_is_rejected_by_length)
{
    uint8_t big[1024];
    memset(big, 'a', sizeof(big));
    memcpy(big, "reset", 5);
    const RemoteCommandResult r = parseRemoteCommand(MQTT_TOPIC_CMD_RESET, big, sizeof(big),
                                                     MQTT_TOPIC_CMD_RESET, MQTT_TOPIC_CMD_ACK);
    CHECK(r.verdict == RemoteCommandVerdict::REJECTED_LENGTH);
    CHECK(r.type == RemoteCommandType::NONE);
}

TEST(null_payload_pointer_is_rejected)
{
    const RemoteCommandResult r = parseRemoteCommand(MQTT_TOPIC_CMD_RESET, nullptr, 5,
                                                     MQTT_TOPIC_CMD_RESET, MQTT_TOPIC_CMD_ACK);
    CHECK(r.verdict == RemoteCommandVerdict::REJECTED_PAYLOAD);
}

TEST(holdoff_window_drops_early_commands_and_is_rollover_safe)
{
    CHECK(commandInHoldoff(1000, 1000, 2000));
    CHECK(commandInHoldoff(2999, 1000, 2000));
    CHECK(!commandInHoldoff(3000, 1000, 2000));
    CHECK(commandInHoldoff(100, 0xFFFFFF00u, 2000));     // crossed the wrap
    CHECK(!commandInHoldoff(5000, 0xFFFFFF00u, 2000));
}

// ---- topic / identity consistency ----------------------------------------

TEST(topics_are_derived_from_device_id)
{
    CHECK_STR(MQTT_TOPIC_TELEMETRY, "energy/device01/telemetry");
    CHECK_STR(MQTT_TOPIC_STATE, "energy/device01/status/state");
    CHECK_STR(MQTT_TOPIC_FAULT, "energy/device01/fault/code");
    CHECK_STR(MQTT_TOPIC_AVAILABILITY, "energy/device01/status/availability");
    CHECK_STR(MQTT_TOPIC_CMD_RESET, "energy/device01/cmd/reset");
    CHECK_STR(MQTT_TOPIC_CMD_ACK, "energy/device01/cmd/ack");
}

TEST(device_id_validation_rejects_topic_injection)
{
    static_assert(topicSegmentValid("device01"), "ok");
    static_assert(topicSegmentValid("Dev_01-A"), "ok");
    static_assert(!topicSegmentValid(""), "empty");
    static_assert(!topicSegmentValid("a/b"), "slash");
    static_assert(!topicSegmentValid("a+b"), "plus wildcard");
    static_assert(!topicSegmentValid("a#"), "hash wildcard");
    static_assert(!topicSegmentValid("a b"), "space");
    static_assert(!topicSegmentValid("123456789012345678901234567890123"), "33 chars");
    CHECK(true);
}
