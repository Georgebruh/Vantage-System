#include "power.h"

#include <Wire.h>

#include "config.h"
#include "pins.h"

namespace {

constexpr uint8_t REG_CONFIG = 0x00;
constexpr uint8_t REG_BUS = 0x02;
constexpr uint8_t REG_CURRENT = 0x04;
constexpr uint8_t REG_CAL = 0x05;
constexpr uint8_t REG_MASK = 0x06;
constexpr uint8_t REG_MFR_ID = 0xFE;
constexpr uint16_t MFR_TI = 0x5449;
constexpr uint16_t CVRF = 0x0008;

// AVG 16, 1.1 ms bus and shunt conversion; triggered and power-down modes.
constexpr uint16_t CFG_BASE = 0x4000 | (0b010 << 9) | (0b100 << 6) | (0b100 << 3);
constexpr uint16_t CFG_TRIGGERED = CFG_BASE | 0b011;
constexpr uint16_t CFG_OFF = CFG_BASE;

constexpr float CURRENT_LSB_A = 0.0001f;

class Ina226 {
 public:
  Ina226(TwoWire &w, uint8_t addr) : w_(w), addr_(addr) {}

  bool read(float &volts, float &milliamps) {
    uint16_t id;
    if (!get(REG_MFR_ID, id) || id != MFR_TI) return false;
    uint16_t cal = (uint16_t)lroundf(0.00512f / (CURRENT_LSB_A * INA_SHUNT_OHM));
    if (!set(REG_CAL, cal) || !set(REG_CONFIG, CFG_TRIGGERED)) return false;

    uint16_t mask = 0;
    uint32_t t0 = millis();
    while (!(mask & CVRF)) {
      if (millis() - t0 > 150 || !get(REG_MASK, mask)) return false;
      if (!(mask & CVRF)) delay(2);
    }
    uint16_t bus, cur;
    bool ok = get(REG_BUS, bus) && get(REG_CURRENT, cur);
    set(REG_CONFIG, CFG_OFF);
    if (!ok) return false;
    volts = bus * 0.00125f;
    milliamps = (int16_t)cur * CURRENT_LSB_A * 1000.0f;
    return true;
  }

 private:
  bool set(uint8_t reg, uint16_t v) {
    w_.beginTransmission(addr_);
    w_.write(reg);
    w_.write(v >> 8);
    w_.write(v & 0xFF);
    return w_.endTransmission() == 0;
  }

  bool get(uint8_t reg, uint16_t &v) {
    w_.beginTransmission(addr_);
    w_.write(reg);
    if (w_.endTransmission(false) != 0) return false;
    if (w_.requestFrom(addr_, (uint8_t)2) != 2) return false;
    v = (uint16_t)w_.read() << 8;
    v |= w_.read();
    return true;
  }

  TwoWire &w_;
  uint8_t addr_;
};

}  // namespace

PowerReading readPower() {
  PowerReading p;
  Wire.begin(PIN_PWR_SDA, PIN_PWR_SCL, 100000);
  Wire.setTimeOut(20);
  float v, i;
  if (Ina226(Wire, INA_BATT_ADDR).read(v, i)) {
    p.vbat = v;
    p.ibatMa = i;
  }
  if (Ina226(Wire, INA_SOLAR_ADDR).read(v, i)) {
    p.vsol = v;
    p.isolMa = i;
  }
  Wire.end();
  return p;
}
