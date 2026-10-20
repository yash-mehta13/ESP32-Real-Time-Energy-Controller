#pragma once

#include <stddef.h>
#include <stdint.h>

enum class RemoteCommandType : uint8_t
{
    NONE = 0,
    RESET,
    ACK
};

enum class RemoteCommandVerdict : uint8_t
{
    ACCEPTED = 0,
    REJECTED_TOPIC,
    REJECTED_PAYLOAD,
    REJECTED_LENGTH,
    REJECTED_HOLDOFF  // arrived too soon after subscribing (possible retained message)
};

struct RemoteCommandResult
{
    RemoteCommandVerdict verdict;
    RemoteCommandType type;
};

// Exact-match validation. The payload is treated as raw bytes of the given
// length (not as a C string), so embedded NULs or trailing data are
// rejected. Returns ACCEPTED only for topic==resetTopic && payload=="reset"
// or topic==ackTopic && payload=="ack".
RemoteCommandResult parseRemoteCommand(
    const char* topic,
    const uint8_t* payload,
    size_t length,
    const char* resetTopic,
    const char* ackTopic);

// Brokers deliver retained messages immediately after SUBSCRIBE and
// PubSubClient does not expose the retain flag. Commands received inside
// the hold-off window after (re)subscribing are therefore dropped, so a
// stale retained "reset" cannot act on reconnect. Rollover-safe.
bool commandInHoldoff(uint32_t nowMs, uint32_t subscribedAtMs, uint32_t holdoffMs);

const char* remoteCommandVerdictName(RemoteCommandVerdict verdict);
