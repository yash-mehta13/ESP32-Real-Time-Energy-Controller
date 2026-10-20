#pragma once

// Single authoritative build-mode policy. The mode is chosen ONLY by the
// PlatformIO environment (platformio.ini build_flags), never by config.h, so
// the reported measurement source always equals the source in use.
//
//   esp32dev_sim   -DENERGY_SOURCE_SIMULATION  relay GPIO held at OFF level
//   esp32dev_pzem  -DENERGY_SOURCE_PZEM        real meter, real relay output

#include "actuator_policy.h"
#include "measurement_types.h"

#if defined(ENERGY_SOURCE_PZEM) && defined(ENERGY_SOURCE_SIMULATION)
#error "Define exactly one of ENERGY_SOURCE_PZEM / ENERGY_SOURCE_SIMULATION"
#elif defined(ENERGY_SOURCE_PZEM)
constexpr MeasurementSource kMeasurementSource = MeasurementSource::PZEM;
#  if defined(ENERGY_RELAY_OUTPUT_DISABLE)
constexpr RelayOutputMode kRelayOutputMode = RelayOutputMode::HOLD_OFF;  // bench: meter only
#  else
constexpr RelayOutputMode kRelayOutputMode = RelayOutputMode::FOLLOW_STATE;
#  endif
#elif defined(ENERGY_SOURCE_SIMULATION)
#  if defined(ENERGY_RELAY_OUTPUT_ENABLE)
#  error "Simulated measurements must never drive the relay output"
#  endif
constexpr MeasurementSource kMeasurementSource = MeasurementSource::SIMULATION;
constexpr RelayOutputMode kRelayOutputMode = RelayOutputMode::HOLD_OFF;
#else
#error "Select a build environment: esp32dev_sim or esp32dev_pzem (see platformio.ini)"
#endif
