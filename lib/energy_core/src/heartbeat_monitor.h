#pragma once

#include <stddef.h>
#include <stdint.h>

// Pure heartbeat table: one "last seen" timestamp per supervised task.
// A slot that never reported is reported as HEARTBEAT_NEVER, which counts as
// unhealthy once the start-up grace period has passed. Not thread-safe; the
// firmware wrapper guards it with a critical section.
constexpr uint32_t HEARTBEAT_NEVER = UINT32_MAX;
constexpr size_t HEARTBEAT_MAX_SLOTS = 8;

class HeartbeatMonitor
{
public:
    explicit HeartbeatMonitor(size_t slots);

    void beat(size_t slot, uint32_t nowMs);
    uint32_t ageMs(size_t slot, uint32_t nowMs) const;

    // True if every slot has beaten within timeoutMs. During the grace
    // period (nowMs - startMs < graceMs) the result is always true.
    bool healthy(uint32_t nowMs, uint32_t timeoutMs,
                 uint32_t startMs, uint32_t graceMs) const;

    // Index of the first stale/never-seen slot, or SIZE_MAX if none.
    size_t firstStale(uint32_t nowMs, uint32_t timeoutMs) const;

private:
    size_t slots_;
    uint32_t last_[HEARTBEAT_MAX_SLOTS];
    bool seen_[HEARTBEAT_MAX_SLOTS];
};
