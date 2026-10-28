#pragma once

#include <Arduino.h>

struct PowerReading {
  float vbat = NAN, ibatMa = NAN;
  float vsol = NAN, isolMa = NAN;
};

PowerReading readPower();
