#include "state.h"

#include <esp_attr.h>
#include <esp_random.h>
#include <sys/time.h>

RTC_NOINIT_ATTR State st;

static constexpr uint32_t STATE_MAGIC = 0x564E5400u ^ sizeof(State);

// System time runs on the RTC timer and survives deep sleep; never set it.
uint64_t nowMs() {
  timeval tv;
  gettimeofday(&tv, nullptr);
  return (uint64_t)tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
}

void stateInit(bool force) {
  if (!force && st.magic == STATE_MAGIC && st.count <= RING_SIZE && st.head < RING_SIZE) return;
  memset(&st, 0, sizeof st);
  st.magic = STATE_MAGIC;
  st.session = esp_random();
  st.mode = PowerMode::Unknown;
  st.lastDistMm = NAN;
}

void pushRecord(const Record &r) {
  uint8_t i = (st.head + st.count) % RING_SIZE;
  if (st.count == RING_SIZE) {
    st.head = (st.head + 1) % RING_SIZE;
    st.dropped++;
  } else {
    st.count++;
  }
  st.ring[i] = r;
}

const Record &recordAt(int i) { return st.ring[(st.head + i) % RING_SIZE]; }

void clearRecords() {
  st.head = 0;
  st.count = 0;
  st.dropped = 0;
}

const char *modeName(PowerMode m) {
  switch (m) {
    case PowerMode::Normal: return "normal";
    case PowerMode::Save: return "save";
    case PowerMode::Low: return "low";
    case PowerMode::Critical: return "critical";
    default: return "unknown";
  }
}
