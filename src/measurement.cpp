#include "measurement.h"

#include <PZEM004Tv30.h>
#include <math.h>

#include "config.h"

namespace
{
    PZEM004Tv30 pzem(Serial2, PZEM_RX_PIN, PZEM_TX_PIN);

    Measurement makeInvalid()
    {
        Measurement m{};
        m.valid = false;
        m.timestamp = millis();
        return m;
    }

    void calculateDerivedValues(Measurement& m)
    {
        if (isfinite(m.voltage) &&
            isfinite(m.current) &&
            m.voltage > 0.0f &&
            m.current >= 0.0f)
        {
            m.apparentPower = m.voltage * m.current;
        }
        else
        {
            m.apparentPower = 0.0f;
        }

        if (isfinite(m.power) &&
            m.apparentPower > 0.0f)
        {
            m.powerFactor = m.power / m.apparentPower;
            m.powerFactor = constrain(m.powerFactor, 0.0f, 1.0f);
        }
        else
        {
            m.powerFactor = 0.0f;
        }
    }

    bool validate(const Measurement& m)
    {
        return isfinite(m.voltage) &&
               isfinite(m.current) &&
               isfinite(m.power) &&
               isfinite(m.energy) &&
               isfinite(m.apparentPower) &&
               isfinite(m.powerFactor) &&
               m.voltage >= 0.0f &&
               m.current >= 0.0f &&
               m.power >= 0.0f &&
               m.energy >= 0.0f &&
               m.apparentPower >= 0.0f &&
               m.powerFactor >= 0.0f &&
               m.powerFactor <= 1.0f;
    }

    Measurement readPzem()
    {
        Measurement m{};

        m.voltage = pzem.voltage();
        m.current = pzem.current();
        m.power = pzem.power();
        m.energy = pzem.energy();

        calculateDerivedValues(m);
        m.timestamp = millis();
        m.valid = validate(m);

        return m;
    }

    Measurement generateSimulation()
    {
        Measurement m{};
        const uint32_t phase = millis() % 15000UL;

        if (phase < 5000UL)
        {
            m.voltage = 230.0f;
            m.current = 0.20f;
            m.power = 40.0f;
            m.energy = 0.0f;
        }
        else if (phase < 10000UL)
        {
            m.voltage = 230.0f;
            m.current = 2.17f;
            m.power = 500.0f;
            m.energy = 0.0f;
        }
        else
        {
            m.voltage = 230.0f;
            m.current = 2.60f;
            m.power = 600.0f;
            m.energy = 0.0f;
        }

        calculateDerivedValues(m);
        m.timestamp = millis();
        m.valid = validate(m);
        return m;
    }
}

void measurementBegin()
{
    Serial2.begin(PZEM_BAUD_RATE, SERIAL_8N1, PZEM_RX_PIN, PZEM_TX_PIN);
}

Measurement readMeasurement()
{
    switch (MEASUREMENT_SOURCE)
    {
        case 0:
            return readPzem();

        case 1:
            return generateSimulation();

        default:
            return makeInvalid();
    }
}

const char* measurementSourceName()
{
    switch (MEASUREMENT_SOURCE)
    {
        case 0:
            return "PZEM";

        case 1:
            return "SIMULATION";

        default:
            return "UNKNOWN";
    }
}
