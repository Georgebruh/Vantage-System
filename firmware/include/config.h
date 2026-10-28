#pragma once

#include <stdint.h>

constexpr char NODE_ID[] = "vantage-01";
constexpr char FW_VERSION[] = "0.1.0";

constexpr char GATEWAY_URL[] = "http://192.168.1.100:8080/ingest";

// Sensor face to the level datum (canal bed). 0 = send distance only.
constexpr uint32_t MOUNT_HEIGHT_MM = 0;
constexpr bool LIDAR_FITTED = false;

constexpr float RAIN_MM_PER_TIP = 0.2794f;

constexpr uint32_t INTERVAL_NORMAL_S = 300;
constexpr uint32_t INTERVAL_STORM_S = 60;
constexpr uint32_t INTERVAL_SAVE_S = 900;
constexpr uint32_t INTERVAL_SAVE_STORM_S = 300;
constexpr uint32_t INTERVAL_LOW_S = 3600;
constexpr uint32_t INTERVAL_CRITICAL_S = 10800;
constexpr uint32_t BENCH_INTERVAL_S = 5;

constexpr uint32_t STORM_TIPS = 2;
constexpr float STORM_RISE_MM = 50.0f;
constexpr uint32_t STORM_HOLD_S = 1800;

constexpr float VBAT_SAVE = 3.60f;
constexpr float VBAT_LOW = 3.45f;
constexpr float VBAT_CRITICAL = 3.30f;
constexpr float VBAT_HYST = 0.05f;

constexpr float INA_SHUNT_OHM = 0.1f;

constexpr uint32_t SENS_WARMUP_MS = 400;
constexpr int US_SAMPLES = 9;
constexpr float US_MIN_MM = 200.0f;
constexpr float US_MAX_MM = 6000.0f;
constexpr int LIDAR_SAMPLES = 15;

constexpr uint32_t WIFI_CACHED_TIMEOUT_MS = 4000;
constexpr uint32_t WIFI_TIMEOUT_MS = 10000;
constexpr uint32_t HTTP_TIMEOUT_MS = 6000;
constexpr uint8_t TX_BACKOFF_AFTER = 3;
constexpr uint8_t TX_BACKOFF_EVERY = 3;
