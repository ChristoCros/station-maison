#include "network_mgr.h"
#include "config.h"
#include <WiFi.h>
#include "time.h"

static bool s_apMode = false;
static bool s_staConnected = false;
static unsigned long s_lastReconnectAttempt = 0;
static const unsigned long RECONNECT_INTERVAL_MS = 30000;
static const unsigned long STA_TIMEOUT_MS = 15000;

static String defaultApSsid() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[24];
  snprintf(buf, sizeof(buf), "SolarTracker-%04X", (uint16_t)(mac & 0xFFFF));
  return String(buf);
}

static bool tryStation() {
  const auto& c = Config::get();
  if (c.wifiSsid.isEmpty()) {
    Serial.println(F("[WIFI] No SSID configured, skipping STA"));
    return false;
  }

  Serial.printf("[WIFI] Connecting to %s...\n", c.wifiSsid.c_str());
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(c.wifiSsid.c_str(), c.wifiPassword.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < STA_TIMEOUT_MS) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    s_staConnected = true;
    s_apMode = false;
    Serial.printf("[WIFI] STA connected, IP=%s\n", WiFi.localIP().toString().c_str());
    configTime(3600, 3600, "pool.ntp.org");
    return true;
  }
  Serial.println(F("[WIFI] STA connection timed out"));
  WiFi.disconnect(true, true);
  return false;
}

static void startAp() {
  Serial.println(F("[WIFI] Starting fallback AP mode"));
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  String ssid = defaultApSsid();
  const String& pass = Config::get().apPassword;
  bool ok = WiFi.softAP(ssid.c_str(), pass.c_str());
  s_apMode = true;
  s_staConnected = false;
  if (ok) {
    Serial.printf("[WIFI] AP started. SSID=%s pass=%s IP=%s\n",
                  ssid.c_str(), pass.c_str(), WiFi.softAPIP().toString().c_str());
  } else {
    Serial.println(F("[WIFI] softAP() failed"));
  }
}

bool NetworkMgr::begin() {
  if (tryStation()) return true;
  startAp();
  return false;
}

void NetworkMgr::loop() {
  if (s_apMode) return;
  if (WiFi.status() == WL_CONNECTED) {
    s_staConnected = true;
    return;
  }
  s_staConnected = false;
  unsigned long now = millis();
  if (now - s_lastReconnectAttempt >= RECONNECT_INTERVAL_MS) {
    s_lastReconnectAttempt = now;
    Serial.println(F("[WIFI] Attempting reconnect..."));
    WiFi.reconnect();
  }
}

bool NetworkMgr::isStationConnected() { return s_staConnected; }
bool NetworkMgr::isApMode()           { return s_apMode; }
String NetworkMgr::localIP()          { return s_apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString(); }
String NetworkMgr::apSSID()           { return defaultApSsid(); }
