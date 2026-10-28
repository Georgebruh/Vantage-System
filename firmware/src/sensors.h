#pragma once

#include <Arduino.h>

struct Env {
  float tempC = NAN, rh = NAN, pHpa = NAN;
};

void sensorsSafe();
void sensorsHoldForSleep();
void sensorsReleaseHold();

void sensorPowerOn(bool lidar);
void sensorPowerOff();
void sensorWaitWarmup();

Env readBme();
float readUltrasonicMm(float tempC);
float readLidarMm(uint32_t timeoutMs);
