#include "actuator_policy.h"

OutputCommand outputsForState(SystemState state)
{
    switch (state)
    {
        case SystemState::NORMAL:  return OutputCommand{true,  true,  false};
        case SystemState::WARNING: return OutputCommand{true,  false, true};
        case SystemState::FAULT:   return OutputCommand{false, false, true};
        case SystemState::INIT:    return OutputCommand{false, false, false};
        default:                   return OutputCommand{false, false, false};
    }
}

bool relayPinDrivenOn(bool commandedOn, RelayOutputMode mode)
{
    return commandedOn && mode == RelayOutputMode::FOLLOW_STATE;
}

int relayLevel(bool drivenOn, int onLevel, int offLevel)
{
    return drivenOn ? onLevel : offLevel;
}

OutputCommand outputsForResult(const ProtectionResult& result)
{
    if (result.synchronizationFailed)
    {
        return outputsForState(SystemState::FAULT);
    }
    return outputsForState(result.state);
}
