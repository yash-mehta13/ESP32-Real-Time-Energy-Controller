#include "remote_command.h"

#include <string.h>

namespace
{
    bool payloadEquals(const uint8_t* payload, size_t length, const char* expected)
    {
        const size_t expectedLength = strlen(expected);
        return payload != nullptr && length == expectedLength &&
               memcmp(payload, expected, expectedLength) == 0;
    }
}

RemoteCommandResult parseRemoteCommand(
    const char* topic,
    const uint8_t* payload,
    size_t length,
    const char* resetTopic,
    const char* ackTopic)
{
    RemoteCommandResult r{RemoteCommandVerdict::REJECTED_TOPIC, RemoteCommandType::NONE};

    if (topic == nullptr)
    {
        return r;
    }

    RemoteCommandType type = RemoteCommandType::NONE;
    const char* expected = nullptr;

    if (strcmp(topic, resetTopic) == 0)
    {
        type = RemoteCommandType::RESET;
        expected = "reset";
    }
    else if (strcmp(topic, ackTopic) == 0)
    {
        type = RemoteCommandType::ACK;
        expected = "ack";
    }
    else
    {
        return r;
    }

    if (length > 16)
    {
        r.verdict = RemoteCommandVerdict::REJECTED_LENGTH;
        return r;
    }

    if (!payloadEquals(payload, length, expected))
    {
        r.verdict = RemoteCommandVerdict::REJECTED_PAYLOAD;
        return r;
    }

    r.verdict = RemoteCommandVerdict::ACCEPTED;
    r.type = type;
    return r;
}

bool commandInHoldoff(uint32_t nowMs, uint32_t subscribedAtMs, uint32_t holdoffMs)
{
    return static_cast<uint32_t>(nowMs - subscribedAtMs) < holdoffMs;
}

const char* remoteCommandVerdictName(RemoteCommandVerdict verdict)
{
    switch (verdict)
    {
        case RemoteCommandVerdict::ACCEPTED:         return "ACCEPTED";
        case RemoteCommandVerdict::REJECTED_TOPIC:   return "REJECTED_TOPIC";
        case RemoteCommandVerdict::REJECTED_PAYLOAD: return "REJECTED_PAYLOAD";
        case RemoteCommandVerdict::REJECTED_LENGTH:  return "REJECTED_LENGTH";
        case RemoteCommandVerdict::REJECTED_HOLDOFF: return "REJECTED_HOLDOFF";
        default:                                     return "UNKNOWN";
    }
}
