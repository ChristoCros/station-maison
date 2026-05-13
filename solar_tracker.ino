#include "config.h"
#include "network_mgr.h"
#include "shelly.h"
#include "display.h"
#include "web_portal.h"
#include <ArduinoOTA.h>

static unsigned long s_lastPollMs = 0;
static const unsigned long POLL_INTERVAL_MS = 2000;

static float s_powerMain = 0.0f;
static float s_powerSolar = 0.0f;
static bool s_mainOk = false;
static bool s_solarOk = false;

static void setupOTA() {
  ArduinoOTA.setHostname("solar-tracker");
  ArduinoOTA.setPassword(Config::get().otaPassword.c_str());

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.printf("[OTA] Start update: %s\n", type.c_str());
    Display::showBootMessage("OTA update...");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println(F("[OTA] End"));
    Display::showBootMessage("OTA done");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static unsigned long lastLog = 0;
    if (millis() - lastLog > 1000) {
      lastLog = millis();
      Serial.printf("[OTA] %u%%\n", (progress * 100) / total);
    }
  });
  ArduinoOTA.onError([](ota_error_t err) {
    Serial.printf("[OTA] Error %u\n", err);
  });
  ArduinoOTA.begin();
  Serial.printf("[OTA] Ready, password=%s (visible in dashboard)\n",
                Config::get().otaPassword.c_str());
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n[BOOT] Solar Tracker starting..."));

  Display::begin();
  Display::showBootMessage("Boot...");

  Config::begin();

  bool sta = NetworkMgr::begin();
  Display::showBootMessage(sta ? "WiFi OK" : "AP mode");

  WebPortal::begin();

  if (sta) setupOTA();

  Display::showBootMessage("Ready");
  delay(800);
}

void loop() {
  NetworkMgr::loop();
  WebPortal::loop();
  if (NetworkMgr::isStationConnected()) ArduinoOTA.handle();

  unsigned long now = millis();
  if (now - s_lastPollMs >= POLL_INTERVAL_MS) {
    s_lastPollMs = now;

    if (NetworkMgr::isStationConnected()) {
      const auto& cfg = Config::get();
      ShellyReading r0, r1;
      s_mainOk  = !cfg.shellyMainUrl.isEmpty()  && Shelly::read(cfg.shellyMainUrl,  r0);
      s_solarOk = !cfg.shellySolarUrl.isEmpty() && Shelly::read(cfg.shellySolarUrl, r1);
      if (s_mainOk)  s_powerMain  = r0.actPower;
      if (s_solarOk) s_powerSolar = r1.actPower;

      WebPortal::broadcastStatus(s_powerMain, s_powerSolar, s_mainOk, s_solarOk);
    }

    Display::render(s_powerMain, s_powerSolar,
                    NetworkMgr::isStationConnected(), NetworkMgr::isApMode());
    Display::handleLeds(s_mainOk ? s_powerMain : 0.0f);
  }

  delay(10);
}
