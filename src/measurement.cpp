#include "measurement.h"

#include "build_mode.h"
#include "config.h"

#if defined(ENERGY_SOURCE_PZEM)
#include <PZEM004Tv30.h>
#endif

namespace
{
#if defined(ENERGY_SOURCE_PZEM)
    PZEM004Tv30 pzem(Serial2, PZEM_RX_PIN, PZEM_TX_PIN);

    Measurement readPzem()
    {
        // The driver returns NAN for every call when the meter does not
        // answer. Each value is the meter's own measurement; apparent power
        // is derived (V*I) inside buildMeasurement().
        RawPzemReading raw{};
        raw.voltage = pzem.voltage();
        raw.current = pzem.current();
        raw.power = pzem.power();
        raw.energy = pzem.energy();       // kWh, meter accumulator
        raw.powerFactor = pzem.pf();

        return buildMeasurement(
            raw, MeasurementSource::PZEM, millis(), defaultMeasurementLimits());
    }
#endif
}

void measurementBegin()
{
#if defined(ENERGY_SOURCE_PZEM)
    Serial2.begin(PZEM_BAUD_RATE, SERIAL_8N1, PZEM_RX_PIN, PZEM_TX_PIN);
#endif
    // Simulation builds do not touch the PZEM UART at all.
}

Measurement readMeasurement()
{
#if defined(ENERGY_SOURCE_PZEM)
    return readPzem();
#else
    return simulationSample(millis());
#endif
}

MeasurementSource activeMeasurementSource()
{
    return kMeasurementSource;
}
