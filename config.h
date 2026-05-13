#pragma once
#include <Arduino.h>

struct AppConfig {
  String wifiSsid;
  String wifiPassword;
  String shellyMainUrl;
  String shellySolarUrl;
  String adminUser;
  String adminPasswordHash;
  String adminPasswordSalt;
  String apPassword;
  String otaPassword;
  bool isFirstBoot;
};

namespace Config {
  void begin();
  const AppConfig& get();
  bool save();
  void setWifi(const String& ssid, const String& pass);
  void setShellyUrls(const String& mainUrl, const String& solarUrl);
  void setAdminCredentials(const String& user, const String& plainPassword);
  bool verifyAdminPassword(const String& plainPassword);
  void regenerateOtaPassword();
  void markBootCompleted();
}
