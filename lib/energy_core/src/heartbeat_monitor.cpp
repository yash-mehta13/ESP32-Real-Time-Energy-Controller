#include "heartbeat_monitor.h"

HeartbeatMonitor::HeartbeatMonitor(size_t slots)
    : slots_(slots > HEARTBEAT_MAX_SLOTS ? HEARTBEAT_MAX_SLOTS : slots),
      last_{},
      seen_{}
{
}

void HeartbeatMonitor::beat(size_t slot, uint32_t nowMs)
{
    if (slot >= slots_)
    {
        return;
    }
    last_[slot] = nowMs;
    seen_[slot] = true;  // explicit flag: millis() == 0 is a legal timestamp
}

uint32_t HeartbeatMonitor::ageMs(size_t slot, uint32_t nowMs) const
{
    if (slot >= slots_ || !seen_[slot])
    {
        return HEARTBEAT_NEVER;
    }
    return static_cast<uint32_t>(nowMs - last_[slot]);
}

size_t HeartbeatMonitor::firstStale(uint32_t nowMs, uint32_t timeoutMs) const
{
    for (size_t i = 0; i < slots_; ++i)
    {
        const uint32_t age = ageMs(i, nowMs);
        if (age == HEARTBEAT_NEVER || age > timeoutMs)
        {
            return i;
        }
    }
    return static_cast<size_t>(-1);
}

bool HeartbeatMonitor::healthy(uint32_t nowMs, uint32_t timeoutMs,
                               uint32_t startMs, uint32_t graceMs) const
{
    if (static_cast<uint32_t>(nowMs - startMs) < graceMs)
    {
        return true;
    }
    return firstStale(nowMs, timeoutMs) == static_cast<size_t>(-1);
}
