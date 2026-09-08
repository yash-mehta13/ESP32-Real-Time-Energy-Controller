#include "watchdog_manager.h"

#include <esp_task_wdt.h>

#include "config.h"

namespace
{
    volatile uint32_t heartbeatTimestamp[HEARTBEAT_COUNT] = {0};

    const char* heartbeatNames[HEARTBEAT_COUNT] =
    {
        "SensorTask",
        "ControlTask",
        "DisplayTask",
        "NetworkTask",
        "WatchdogTask",
        "CliTask"
    };

    portMUX_TYPE heartbeatMux = portMUX_INITIALIZER_UNLOCKED;
}

bool watchdogManagerBegin()
{
    esp_task_wdt_config_t config{};
    config.timeout_ms = WATCHDOG_TIMEOUT_MS;
    config.idle_core_mask = (1U << portNUM_PROCESSORS) - 1U;
    config.trigger_panic = true;

    esp_err_t result = esp_task_wdt_init(&config);

    if (result == ESP_ERR_INVALID_STATE)
    {
        result = esp_task_wdt_reconfigure(&config);
    }

    if (result != ESP_OK)
    {
        Serial.printf("[WATCHDOG] Initialization failed: %d\n",
                      static_cast<int>(result));
        return false;
    }

    Serial.printf("[WATCHDOG] TWDT ready: %lu ms\n",
                  (unsigned long)WATCHDOG_TIMEOUT_MS);
    return true;
}

bool watchdogManagerRegisterCurrentTask(const char* taskName)
{
    const esp_err_t result = esp_task_wdt_add(nullptr);

    if (result == ESP_OK || result == ESP_ERR_INVALID_ARG || result == ESP_ERR_INVALID_STATE)
    {
        Serial.printf("[WATCHDOG] Registered: %s\n", taskName);
        return true;
    }

    Serial.printf("[WATCHDOG] Registration failed: %s (%d)\n",
                  taskName, static_cast<int>(result));
    return false;
}

void watchdogManagerHeartbeat(HeartbeatId id)
{
    if (id >= HEARTBEAT_COUNT)
        return;

    portENTER_CRITICAL(&heartbeatMux);
    heartbeatTimestamp[id] = millis();
    portEXIT_CRITICAL(&heartbeatMux);
}

uint32_t watchdogManagerHeartbeatAge(HeartbeatId id)
{
    if (id >= HEARTBEAT_COUNT)
        return UINT32_MAX;

    uint32_t last = 0;

    portENTER_CRITICAL(&heartbeatMux);
    last = heartbeatTimestamp[id];
    portEXIT_CRITICAL(&heartbeatMux);

    if (last == 0)
        return UINT32_MAX;

    return millis() - last;
}

const char* watchdogManagerHeartbeatName(HeartbeatId id)
{
    return id < HEARTBEAT_COUNT ? heartbeatNames[id] : "UnknownTask";
}

bool watchdogManagerHealthy(uint32_t timeoutMs)
{
    for (uint8_t i = 0; i < HEARTBEAT_COUNT; ++i)
    {
        if (watchdogManagerHeartbeatAge(
                static_cast<HeartbeatId>(i)) > timeoutMs)
        {
            return false;
        }
    }

    return true;
}

void watchdogManagerPrintStatus(uint32_t timeoutMs)
{
    for (uint8_t i = 0; i < HEARTBEAT_COUNT; ++i)
    {
        const uint32_t age = watchdogManagerHeartbeatAge(
            static_cast<HeartbeatId>(i));

        Serial.printf(
            "[WATCHDOG] %-14s : %s",
            watchdogManagerHeartbeatName(
                static_cast<HeartbeatId>(i)),
            age == UINT32_MAX ? "NO HEARTBEAT" : "OK"
        );

        if (age != UINT32_MAX)
        {
            Serial.printf(" (%lu ms)", (unsigned long)age);
        }

        if (age > timeoutMs)
        {
            Serial.print(" [STALE]");
        }

        Serial.println();
    }
}
