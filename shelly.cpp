#include "shelly.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

bool Shelly::read(const String& url, ShellyReading& out) {
  out.valid = false;
  if (url.isEmpty()) return false;

  HTTPClient http;
  http.setTimeout(2000);
  http.setConnectTimeout(2000);
  if (!http.begin(url)) {
    Serial.printf("[SHELLY] begin() failed for %s\n", url.c_str());
    return false;
  }

  int code = http.GET();
  if (code != 200) {
    Serial.printf("[SHELLY] HTTP %d for %s\n", code, url.c_str());
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[SHELLY] JSON parse error: %s\n", err.c_str());
    return false;
  }

  JsonVariantConst v = doc["act_power"];
  if (v.isNull()) {
    Serial.println(F("[SHELLY] act_power missing in response"));
    return false;
  }
  out.actPower = v.as<float>();
  out.voltage  = doc["voltage"] | 0.0f;
  out.current  = doc["current"] | 0.0f;
  out.valid    = true;
  return true;
}
