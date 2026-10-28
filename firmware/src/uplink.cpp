#include "uplink.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "config.h"
#include "state.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy include/secrets.example.h to include/secrets.h and set the Wi-Fi credentials."
#endif

static bool waitConnected(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > timeoutMs) return false;
    delay(50);
  }
  return true;
}

static bool connect() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  if (st.wifiCached) {
    WiFi.begin(WIFI_SSID, WIFI_PASS, st.wifiChannel, st.wifiBssid);
    if (waitConnected(WIFI_CACHED_TIMEOUT_MS)) return true;
    st.wifiCached = false;
    WiFi.disconnect();
  }
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  if (!waitConnected(WIFI_TIMEOUT_MS)) return false;
  st.wifiChannel = WiFi.channel();
  memcpy(st.wifiBssid, WiFi.BSSID(), 6);
  st.wifiCached = true;
  return true;
}

static void put(JsonObject o, const char *key, float v, int decimals) {
  if (isnan(v)) {
    o[key] = nullptr;
  } else {
    o[key] = serialized(String(v, decimals));
  }
}

static String buildBody() {
  JsonDocument doc;
  doc["node"] = NODE_ID;
  doc["fw"] = FW_VERSION;
  doc["session"] = st.session;
  doc["boots"] = st.boots;
  doc["brownouts"] = st.brownouts;
  doc["crashes"] = st.crashes;
  doc["dropped"] = st.dropped;
  doc["rssi"] = WiFi.RSSI();

  uint64_t now = nowMs();
  JsonArray readings = doc["readings"].to<JsonArray>();
  for (int i = 0; i < st.count; i++) {
    const Record &r = recordAt(i);
    JsonObject o = readings.add<JsonObject>();
    o["seq"] = r.seq;
    o["age_s"] = (uint32_t)((now - r.tMs) / 1000);
    o["mode"] = modeName(r.mode);
    o["storm"] = r.storm;
    put(o, "temp_c", r.tempC, 2);
    put(o, "rh", r.rh, 1);
    put(o, "p_hpa", r.pHpa, 2);
    put(o, "dist_mm", r.distMm, 0);
    put(o, "lidar_mm", r.lidarMm, 0);
    put(o, "level_mm", MOUNT_HEIGHT_MM && !isnan(r.distMm) ? MOUNT_HEIGHT_MM - r.distMm : NAN, 0);
    o["rain_tips"] = r.rainTips;
    put(o, "rain_mm", r.rainTips * RAIN_MM_PER_TIP, 2);
    put(o, "vbat", r.vbat, 3);
    put(o, "ibat_ma", r.ibatMa, 1);
    put(o, "vsol", r.vsol, 3);
    put(o, "isol_ma", r.isolMa, 1);
  }
  String body;
  serializeJson(doc, body);
  return body;
}

bool uplinkFlush() {
  if (!st.count) return true;
  bool ok = false;
  if (connect()) {
    String body = buildBody();
    HTTPClient http;
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);
    if (http.begin(GATEWAY_URL)) {
      http.addHeader("Content-Type", "application/json");
      int code = http.POST(body);
      ok = code >= 200 && code < 300;
      http.end();
    }
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  if (ok) {
    clearRecords();
    st.txFails = 0;
  } else if (st.txFails < 255) {
    st.txFails++;
  }
  return ok;
}
