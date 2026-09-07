#pragma once

// =====================================================
//                  BLYNK CONFIGURATION
// =====================================================

#define BLYNK_TEMPLATE_ID "YOUR_BLYNK_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "Energy Monitor"
#define BLYNK_AUTH_TOKEN "YOUR_BLYNK_AUTH_TOKEN"

// =====================================================
//                  WIFI CONFIGURATION
// =====================================================

constexpr const char* WIFI_SSID =
    "YOUR_WIFI_SSID";

constexpr const char* WIFI_PASSWORD =
    "YOUR_WIFI_PASSWORD";

// =====================================================
//                  HIVEMQ CONFIGURATION
// =====================================================

constexpr const char* MQTT_BROKER_HOST =
    "YOUR_HIVEMQ_CLUSTER_HOST";

constexpr const char* MQTT_USERNAME =
    "YOUR_HIVEMQ_USERNAME";

constexpr const char* MQTT_PASSWORD =
    "YOUR_HIVEMQ_PASSWORD";

constexpr const char* MQTT_DEVICE_ID =
    "device01";

// =====================================================
//                     NTP
// =====================================================

constexpr const char* NTP_SERVER_1 =
    "pool.ntp.org";

constexpr const char* NTP_SERVER_2 =
    "time.nist.gov";
