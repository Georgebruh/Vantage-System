#include <Arduino.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "pins.h"
#include "power.h"
#include "rain.h"
#include "sensors.h"
#include "state.h"
#ifndef VANTAGE_BENCH
#include "uplink.h"
#endif

static PowerMode classify(float v, float h) {
  if (v >= VBAT_SAVE + h) return PowerMode::Normal;
  if (v >= VBAT_LOW + h) return PowerMode::Save;
  if (v >= VBAT_CRITICAL + h) return PowerMode::Low;
  return PowerMode::Critical;
}

// Step down at the threshold, step back up only VBAT_HYST above it.
static PowerMode decideMode(float vbat) {
  if (isnan(vbat)) return PowerMode::Unknown;
  PowerMode prev = st.mode == PowerMode::Unknown ? PowerMode::Critical : st.mode;
  PowerMode m = classify(vbat, 0);
  if (m < prev) {
    PowerMode h = classify(vbat, VBAT_HYST);
    m = h < prev ? h : prev;
  }
  return m;
}

static Record measure(PowerMode mode, bool lidar) {
  Record r{};
  r.tMs = nowMs();
  r.tempC = r.rh = r.pHpa = r.distMm = r.lidarMm = NAN;
  PowerReading p = readPower();
  r.vbat = p.vbat;
  r.ibatMa = p.ibatMa;
  r.vsol = p.vsol;
  r.isolMa = p.isolMa;
  if (mode == PowerMode::Unknown) mode = decideMode(p.vbat);
  r.mode = mode;
  if (mode != PowerMode::Critical) {
    sensorPowerOn(lidar && mode <= PowerMode::Normal);
    Env e = readBme();
    r.tempC = e.tempC;
    r.rh = e.rh;
    r.pHpa = e.pHpa;
    sensorWaitWarmup();
    r.distMm = readUltrasonicMm(e.tempC);
    r.lidarMm = readLidarMm(1000);
    sensorPowerOff();
  }
  return r;
}

static void logRecord(const Record &r, const char *extra) {
  Serial.printf("#%lu %s%s T=%.2f RH=%.1f P=%.2f dist=%.0f lidar=%.0f rain=%u Vbat=%.3f Ibat=%.1f Vsol=%.3f Isol=%.1f %s\n",
                (unsigned long)r.seq, modeName(r.mode), r.storm ? "+storm" : "", r.tempC, r.rh, r.pHpa, r.distMm,
                r.lidarMm, r.rainTips, r.vbat, r.ibatMa, r.vsol, r.isolMa, extra);
}

#ifdef VANTAGE_BENCH

static void scan(TwoWire &w, int sda, int scl, const char *name) {
  w.begin(sda, scl, 100000);
  w.setTimeOut(20);
  Serial.printf("I2C %s:", name);
  for (uint8_t a = 1; a < 127; a++) {
    w.beginTransmission(a);
    if (w.endTransmission() == 0) Serial.printf(" 0x%02X", a);
  }
  Serial.println();
  w.end();
}

static uint32_t benchTips;

void setup() {
  sensorsReleaseHold();
  sensorsSafe();
  stateInit(true);
  rainBegin(ESP_SLEEP_WAKEUP_UNDEFINED);
  Serial.begin(115200);
  delay(1500);
  Serial.printf("\nVantage %s %s bench\n", NODE_ID, FW_VERSION);
  scan(Wire, PIN_PWR_SDA, PIN_PWR_SCL, "power (IO38/IO47)");
  digitalWrite(PIN_BME_PWR, HIGH);
  delay(10);
  scan(Wire1, PIN_BME_SDA, PIN_BME_SCL, "BME280 (IO41/IO42)");
  pinMode(PIN_BME_SDA, INPUT);
  pinMode(PIN_BME_SCL, INPUT);
  digitalWrite(PIN_BME_PWR, LOW);
}

void loop() {
  Record r = measure(PowerMode::Unknown, true);
  r.seq = st.seq++;
  rainCollect();
  benchTips += st.rainPending;
  r.rainTips = st.rainPending;
  st.rainPending = 0;
  char extra[32];
  snprintf(extra, sizeof extra, "tips_total=%lu", (unsigned long)benchTips);
  logRecord(r, extra);
  delay(BENCH_INTERVAL_S * 1000);
}

#else

static uint32_t intervalS(PowerMode m, bool storm) {
  switch (m) {
    case PowerMode::Save: return storm ? INTERVAL_SAVE_STORM_S : INTERVAL_SAVE_S;
    case PowerMode::Low: return INTERVAL_LOW_S;
    case PowerMode::Critical: return INTERVAL_CRITICAL_S;
    default: return storm ? INTERVAL_STORM_S : INTERVAL_NORMAL_S;
  }
}

static void startWatchdog() {
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = 60000;
  cfg.trigger_panic = true;
  if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) esp_task_wdt_init(&cfg);
  esp_task_wdt_add(nullptr);
}

[[noreturn]] static void sleepUntilNext() {
  uint64_t now = nowMs();
  uint64_t ms = st.nextMeasureMs > now ? st.nextMeasureMs - now : 0;
  if (ms < 1000) ms = 1000;
  sensorsSafe();
  sensorsHoldForSleep();
  rainArmSleep();
  Serial.flush();
  esp_sleep_enable_timer_wakeup(ms * 1000ULL);
  esp_deep_sleep_start();
}

static void runCycle() {
  Record r = measure(PowerMode::Unknown, LIDAR_FITTED);
  st.mode = r.mode;
  r.seq = st.seq++;
  rainCollect();
  r.rainTips = st.rainPending > 0xFFFF ? 0xFFFF : st.rainPending;
  st.rainPending = 0;

  bool rising = !isnan(r.distMm) && !isnan(st.lastDistMm) && st.lastDistMm - r.distMm >= STORM_RISE_MM;
  if (!isnan(r.distMm)) st.lastDistMm = r.distMm;
  if (r.rainTips >= STORM_TIPS || rising) st.stormUntilMs = r.tMs + STORM_HOLD_S * 1000ULL;
  r.storm = r.tMs < st.stormUntilMs;
  pushRecord(r);
  st.nextMeasureMs = r.tMs + intervalS(r.mode, r.storm) * 1000ULL;
  st.cycles++;

  const char *tx = "tx=skip";
  bool backoff = st.txFails >= TX_BACKOFF_AFTER && st.cycles % TX_BACKOFF_EVERY && !r.storm;
  if (r.mode != PowerMode::Critical && !backoff) tx = uplinkFlush() ? "tx=ok" : "tx=fail";
  logRecord(r, tx);
}

void setup() {
  sensorsReleaseHold();
  sensorsSafe();
  setCpuFrequencyMhz(80);

  esp_reset_reason_t reset = esp_reset_reason();
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  stateInit(reset == ESP_RST_POWERON);
  st.boots++;
  rainBegin(cause);
  Serial.begin(115200);
  startWatchdog();

  uint64_t now = nowMs();
  if (st.nextMeasureMs > now + INTERVAL_CRITICAL_S * 1000ULL) st.nextMeasureMs = now;

  if (reset == ESP_RST_BROWNOUT) {
    st.brownouts++;
    st.mode = PowerMode::Critical;
    st.nextMeasureMs = now + INTERVAL_CRITICAL_S * 1000ULL;
    sleepUntilNext();
  }
  if (reset == ESP_RST_PANIC || reset == ESP_RST_TASK_WDT || reset == ESP_RST_INT_WDT || reset == ESP_RST_WDT) {
    st.crashes++;
  }

  if (now < st.nextMeasureMs) {
    bool canSpeedUp = st.mode != PowerMode::Low && st.mode != PowerMode::Critical;
    if (cause == ESP_SLEEP_WAKEUP_EXT0 && canSpeedUp && st.rainPending >= STORM_TIPS && now >= st.stormUntilMs) {
      uint64_t soon = now + intervalS(st.mode, true) * 1000ULL;
      if (soon < st.nextMeasureMs) st.nextMeasureMs = soon;
    }
    sleepUntilNext();
  }

  runCycle();
  sleepUntilNext();
}

void loop() {}

#endif
