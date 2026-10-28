#pragma once

#include <Arduino.h>

enum class PowerMode : uint8_t { Unknown, Normal, Save, Low, Critical };

struct Record {
  uint64_t tMs;
  uint32_t seq;
  float tempC, rh, pHpa;
  float distMm, lidarMm;
  float vbat, ibatMa, vsol, isolMa;
  uint16_t rainTips;
  PowerMode mode;
  bool storm;
};

constexpr int RING_SIZE = 48;

struct State {
  uint32_t magic;
  uint32_t session;
  uint32_t boots;
  uint32_t brownouts;
  uint32_t crashes;
  uint32_t seq;
  uint32_t cycles;
  uint64_t nextMeasureMs;
  uint64_t stormUntilMs;
  uint64_t lastTipMs;
  uint32_t rainPending;
  bool rainWaitRelease;
  PowerMode mode;
  float lastDistMm;
  uint8_t txFails;
  uint16_t dropped;
  bool wifiCached;
  uint8_t wifiChannel;
  uint8_t wifiBssid[6];
  uint8_t head, count;
  Record ring[RING_SIZE];
};

extern State st;

uint64_t nowMs();
void stateInit(bool force);
void pushRecord(const Record &r);
const Record &recordAt(int i);
void clearRecords();
const char *modeName(PowerMode m);
