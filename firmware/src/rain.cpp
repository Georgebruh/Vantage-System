#include "rain.h"

#include <Arduino.h>
#include <driver/rtc_io.h>
#include <esp_timer.h>

#include "pins.h"
#include "state.h"

static constexpr int64_t STABLE_HIGH_US = 10000;
static constexpr int64_t LOCKOUT_US = 100000;

static volatile uint32_t isrTips;
static volatile int64_t lastRiseUs;
static volatile int64_t lastTipUs;
static int64_t bootOffsetMs;

// A tip counts only after the line has been high for 10 ms, which rejects contact bounce.
static void ARDUINO_ISR_ATTR onRain() {
  int64_t t = esp_timer_get_time();
  if (digitalRead(PIN_RAIN)) {
    lastRiseUs = t;
    return;
  }
  if (t - lastRiseUs >= STABLE_HIGH_US && t - lastTipUs >= LOCKOUT_US) {
    isrTips = isrTips + 1;
    lastTipUs = t;
  }
}

void rainBegin(esp_sleep_wakeup_cause_t cause) {
  rtc_gpio_deinit((gpio_num_t)PIN_RAIN);
  pinMode(PIN_RAIN, INPUT_PULLUP);

  uint64_t now = nowMs();
  if (cause == ESP_SLEEP_WAKEUP_EXT0) {
    if (!st.rainWaitRelease && now - st.lastTipMs >= LOCKOUT_US / 1000) {
      st.rainPending++;
      st.lastTipMs = now;
    }
    st.rainWaitRelease = false;
  }

  int64_t t = esp_timer_get_time();
  bootOffsetMs = (int64_t)now - t / 1000;
  lastTipUs = ((int64_t)st.lastTipMs - bootOffsetMs) * 1000;
  lastRiseUs = digitalRead(PIN_RAIN) ? t - STABLE_HIGH_US : t;
  isrTips = 0;
  attachInterrupt(PIN_RAIN, onRain, CHANGE);
}

void rainCollect() {
  noInterrupts();
  uint32_t n = isrTips;
  int64_t last = lastTipUs;
  isrTips = 0;
  interrupts();
  if (n) {
    st.rainPending += n;
    st.lastTipMs = bootOffsetMs + last / 1000;
  }
}

void rainArmSleep() {
  uint32_t t0 = millis();
  while (!digitalRead(PIN_RAIN) && millis() - t0 < 300) delay(5);
  detachInterrupt(PIN_RAIN);
  rainCollect();

  // Still closed: wake on release instead, so the same tip is not counted twice.
  st.rainWaitRelease = !digitalRead(PIN_RAIN);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_RAIN, st.rainWaitRelease ? 1 : 0);
  rtc_gpio_pullup_en((gpio_num_t)PIN_RAIN);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_RAIN);
}
