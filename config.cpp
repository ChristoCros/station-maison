#include "config.h"
#include <Preferences.h>
#include <mbedtls/sha256.h>

static AppConfig g_config;
static Preferences prefs;

static String bytesToHex(const uint8_t* data, size_t len) {
  String s;
  s.reserve(len * 2);
  const char* hex = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    s += hex[data[i] >> 4];
    s += hex[data[i] & 0xF];
  }
  return s;
}

static String sha256Hex(const String& input) {
  uint8_t hash[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, (const uint8_t*)input.c_str(), input.length());
  mbedtls_sha256_finish(&ctx, hash);
  mbedtls_sha256_free(&ctx);
  return bytesToHex(hash, 32);
}

static String randomHex(size_t bytes) {
  uint8_t buf[32];
  if (bytes > sizeof(buf)) bytes = sizeof(buf);
  for (size_t i = 0; i < bytes; i++) buf[i] = esp_random() & 0xFF;
  return bytesToHex(buf, bytes);
}

void Config::begin() {
  prefs.begin("solartracker", false);
  g_config.wifiSsid          = prefs.getString("ssid",     "");
  g_config.wifiPassword      = prefs.getString("pass",     "");
  g_config.shellyMainUrl     = prefs.getString("urlMain",  "");
  g_config.shellySolarUrl    = prefs.getString("urlSolar", "");
  g_config.adminUser         = prefs.getString("admUser",  "admin");
  g_config.adminPasswordHash = prefs.getString("admHash",  "");
  g_config.adminPasswordSalt = prefs.getString("admSalt",  "");
  g_config.apPassword        = prefs.getString("apPass",   "");
  g_config.otaPassword       = prefs.getString("otaPass",  "");
  g_config.isFirstBoot       = prefs.getBool("firstBoot",  true);

  bool dirty = false;

  if (g_config.adminPasswordHash.isEmpty()) {
    setAdminCredentials("admin", "admin");
    g_config.isFirstBoot = true;
    dirty = true;
  }

  if (g_config.apPassword.isEmpty()) {
    g_config.apPassword = randomHex(6); // 12 chars hex, > 8 (WPA2 min)
    dirty = true;
  }

  if (g_config.otaPassword.isEmpty()) {
    g_config.otaPassword = randomHex(10); // 20 chars hex, ~80 bits entropy
    dirty = true;
  }

  if (dirty) save();
}

const AppConfig& Config::get() { return g_config; }

bool Config::save() {
  prefs.putString("ssid",     g_config.wifiSsid);
  prefs.putString("pass",     g_config.wifiPassword);
  prefs.putString("urlMain",  g_config.shellyMainUrl);
  prefs.putString("urlSolar", g_config.shellySolarUrl);
  prefs.putString("admUser",  g_config.adminUser);
  prefs.putString("admHash",  g_config.adminPasswordHash);
  prefs.putString("admSalt",  g_config.adminPasswordSalt);
  prefs.putString("apPass",   g_config.apPassword);
  prefs.putString("otaPass",  g_config.otaPassword);
  prefs.putBool("firstBoot",  g_config.isFirstBoot);
  return true;
}

void Config::setWifi(const String& ssid, const String& pass) {
  g_config.wifiSsid = ssid;
  g_config.wifiPassword = pass;
  save();
}

void Config::setShellyUrls(const String& mainUrl, const String& solarUrl) {
  g_config.shellyMainUrl = mainUrl;
  g_config.shellySolarUrl = solarUrl;
  save();
}

void Config::setAdminCredentials(const String& user, const String& plainPassword) {
  g_config.adminUser = user;
  g_config.adminPasswordSalt = randomHex(16);
  g_config.adminPasswordHash = sha256Hex(g_config.adminPasswordSalt + plainPassword);
  save();
}

bool Config::verifyAdminPassword(const String& plainPassword) {
  if (g_config.adminPasswordHash.isEmpty()) return false;
  String computed = sha256Hex(g_config.adminPasswordSalt + plainPassword);
  if (computed.length() != g_config.adminPasswordHash.length()) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < computed.length(); i++) {
    diff |= computed[i] ^ g_config.adminPasswordHash[i];
  }
  return diff == 0;
}

void Config::regenerateOtaPassword() {
  g_config.otaPassword = randomHex(10);
  save();
}

void Config::markBootCompleted() {
  g_config.isFirstBoot = false;
  save();
}
