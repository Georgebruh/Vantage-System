#include "sensors.h"

#include <Adafruit_BME280.h>
#include <Wire.h>
#include <driver/gpio.h>

#include "config.h"
#include "pins.h"

static uint32_t powerOnAt;
static bool lidarOn;

static const int HELD_PINS[] = {PIN_SENS_EN, PIN_US_TRIG, PIN_LID_TX, PIN_BME_PWR, PIN_BME_SDA, PIN_BME_SCL};

static void outLow(int pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

static float median(float *v, int n) {
  for (int i = 1; i < n; i++) {
    float x = v[i];
    int j = i - 1;
    while (j >= 0 && v[j] > x) {
      v[j + 1] = v[j];
      j--;
    }
    v[j + 1] = x;
  }
  return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0f;
}

// VNT-001 Annex D: nothing may drive an unpowered sensor.
void sensorsSafe() {
  outLow(PIN_SENS_EN);
  outLow(PIN_US_TRIG);
  outLow(PIN_LID_TX);
  outLow(PIN_BME_PWR);
  pinMode(PIN_BME_SDA, INPUT);
  pinMode(PIN_BME_SCL, INPUT);
  pinMode(PIN_US_ECHO, INPUT);
  pinMode(PIN_LID_RX, INPUT);
}

void sensorsHoldForSleep() {
  for (int p : HELD_PINS) gpio_hold_en((gpio_num_t)p);
  gpio_deep_sleep_hold_en();
}

void sensorsReleaseHold() {
  gpio_deep_sleep_hold_dis();
  for (int p : HELD_PINS) gpio_hold_dis((gpio_num_t)p);
}

void sensorPowerOn(bool lidar) {
  digitalWrite(PIN_SENS_EN, HIGH);
  powerOnAt = millis();
  lidarOn = lidar;
  if (lidar) {
    Serial1.setRxBufferSize(1024);
    Serial1.begin(115200, SERIAL_8N1, PIN_LID_RX, PIN_LID_TX);
  }
}

void sensorWaitWarmup() {
  while (millis() - powerOnAt < SENS_WARMUP_MS) delay(5);
}

void sensorPowerOff() {
  if (lidarOn) {
    Serial1.end();
    lidarOn = false;
  }
  outLow(PIN_US_TRIG);
  outLow(PIN_LID_TX);
  digitalWrite(PIN_SENS_EN, LOW);
}

Env readBme() {
  Env e;
  digitalWrite(PIN_BME_PWR, HIGH);
  delay(10);
  Wire1.begin(PIN_BME_SDA, PIN_BME_SCL, 100000);
  Wire1.setTimeOut(20);
  {
    Adafruit_BME280 bme;
    if (bme.begin(0x76, &Wire1) || bme.begin(0x77, &Wire1)) {
      bme.setSampling(Adafruit_BME280::MODE_FORCED, Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::FILTER_OFF);
      if (bme.takeForcedMeasurement()) {
        e.tempC = bme.readTemperature();
        e.pHpa = bme.readPressure() / 100.0f;
        e.rh = bme.readHumidity();
      }
    }
  }
  Wire1.end();
  pinMode(PIN_BME_SDA, INPUT);
  pinMode(PIN_BME_SCL, INPUT);
  digitalWrite(PIN_BME_PWR, LOW);
  if (!(e.tempC > -40.0f && e.tempC < 85.0f)) e.tempC = NAN;
  if (!(e.rh >= 0.0f && e.rh <= 100.0f)) e.rh = NAN;
  if (!(e.pHpa > 300.0f && e.pHpa < 1100.0f)) e.pHpa = NAN;
  return e;
}

float readUltrasonicMm(float tempC) {
  float c = 331.3f + 0.606f * (isnan(tempC) ? 27.0f : tempC);
  float v[US_SAMPLES];
  int n = 0;
  for (int i = 0; i < US_SAMPLES; i++) {
    digitalWrite(PIN_US_TRIG, HIGH);
    delayMicroseconds(20);
    digitalWrite(PIN_US_TRIG, LOW);
    unsigned long us = pulseIn(PIN_US_ECHO, HIGH, 60000);
    float mm = us * c / 2000.0f;
    if (us && mm >= US_MIN_MM && mm <= US_MAX_MM) v[n++] = mm;
    delay(70);
  }
  return n > US_SAMPLES / 2 ? median(v, n) : NAN;
}

static int resync(uint8_t *f, int k) {
  int s = 1;
  while (s < k && !(f[s] == 0x59 && (s + 1 >= k || f[s + 1] == 0x59))) s++;
  memmove(f, f + s, k - s);
  return k - s;
}

// TF-Luna frame: 59 59 DistL DistH AmpL AmpH TempL TempH Sum, distance in cm.
float readLidarMm(uint32_t timeoutMs) {
  if (!lidarOn) return NAN;
  while (Serial1.available()) Serial1.read();
  float v[LIDAR_SAMPLES];
  int n = 0, k = 0;
  uint8_t f[9];
  uint32_t t0 = millis();
  while (n < LIDAR_SAMPLES && millis() - t0 < timeoutMs) {
    if (!Serial1.available()) {
      delay(1);
      continue;
    }
    f[k++] = Serial1.read();
    if (f[0] != 0x59 || (k > 1 && f[1] != 0x59)) {
      k = resync(f, k);
      continue;
    }
    if (k < 9) continue;
    uint8_t sum = 0;
    for (int i = 0; i < 8; i++) sum += f[i];
    if (sum != f[8]) {
      k = resync(f, k);
      continue;
    }
    k = 0;
    uint16_t dist = f[2] | f[3] << 8;
    uint16_t amp = f[4] | f[5] << 8;
    if (dist > 0 && amp >= 100 && amp != 0xFFFF) v[n++] = dist * 10.0f;
  }
  return n >= 3 ? median(v, n) : NAN;
}
