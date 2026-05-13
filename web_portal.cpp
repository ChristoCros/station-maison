#include "web_portal.h"
#include "config.h"
#include "auth.h"
#include "network_mgr.h"
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

static bool s_rebootPending = false;
static unsigned long s_rebootAt = 0;

static String getCookieToken(AsyncWebServerRequest* req) {
  if (!req->hasHeader("Cookie")) return "";
  String cookie = req->header("Cookie");
  int idx = cookie.indexOf("session=");
  if (idx < 0) return "";
  int end = cookie.indexOf(';', idx);
  if (end < 0) end = cookie.length();
  return cookie.substring(idx + 8, end);
}

static bool isAuthed(AsyncWebServerRequest* req) {
  return Auth::validateToken(getCookieToken(req));
}

static void sendJson(AsyncWebServerRequest* req, int code, const String& body) {
  AsyncWebServerResponse* res = req->beginResponse(code, "application/json", body);
  res->addHeader("Cache-Control", "no-store");
  req->send(res);
}

static void scheduleReboot(unsigned long delayMs = 800) {
  s_rebootPending = true;
  s_rebootAt = millis() + delayMs;
}

static void handleLogin(AsyncWebServerRequest* req, JsonVariant& json) {
  String user = json["user"] | "";
  String pass = json["pass"] | "";
  String token;
  if (Auth::tryLogin(user, pass, req->client()->remoteIP(), token)) {
    AsyncWebServerResponse* res = req->beginResponse(200, "application/json", "{\"ok\":true}");
    String cookie = "session=" + token + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=1800";
    res->addHeader("Set-Cookie", cookie);
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
  } else {
    sendJson(req, 401, "{\"ok\":false,\"error\":\"invalid_or_locked\"}");
  }
}

static void sendStatus(AsyncWebServerRequest* req) {
  JsonDocument doc;
  doc["sta_connected"]    = NetworkMgr::isStationConnected();
  doc["ap_mode"]          = NetworkMgr::isApMode();
  doc["ip"]               = NetworkMgr::localIP();
  doc["heap_free"]        = ESP.getFreeHeap();
  doc["uptime_s"]         = millis() / 1000UL;
  const auto& c = Config::get();
  doc["wifi_ssid"]        = c.wifiSsid;
  doc["shelly_main_url"]  = c.shellyMainUrl;
  doc["shelly_solar_url"] = c.shellySolarUrl;
  doc["first_boot"]       = c.isFirstBoot;
  doc["ota_password"]     = c.otaPassword;
  doc["ap_password"]      = c.apPassword;
  String out;
  serializeJson(doc, out);
  sendJson(req, 200, out);
}

static void onWsEvent(AsyncWebSocket* /*server*/, AsyncWebSocketClient* client,
                      AwsEventType type, void* /*arg*/, uint8_t* /*data*/, size_t /*len*/) {
  if (type == WS_EVT_CONNECT) {
    AsyncWebServerRequest* req = client->request();
    if (!req || !req->hasHeader("Cookie")) {
      client->close(4001, "unauth");
      return;
    }
    String cookie = req->header("Cookie");
    int idx = cookie.indexOf("session=");
    if (idx < 0) { client->close(4001, "unauth"); return; }
    int end = cookie.indexOf(';', idx);
    if (end < 0) end = cookie.length();
    String token = cookie.substring(idx + 8, end);
    if (!Auth::validateToken(token)) { client->close(4001, "unauth"); return; }
    Serial.printf("[WS] Client %u authenticated\n", client->id());
  }
}

void WebPortal::begin() {
  if (!LittleFS.begin(true)) {
    Serial.println(F("[WEB] LittleFS mount failed — UI files unavailable"));
  } else {
    Serial.println(F("[WEB] LittleFS mounted"));
  }
  Auth::begin();

  server.on("/login.html", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(LittleFS, "/login.html", "text/html");
  });
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(LittleFS, "/style.css", "text/css");
  });

  AsyncCallbackJsonWebHandler* loginH = new AsyncCallbackJsonWebHandler(
    "/api/login",
    [](AsyncWebServerRequest* req, JsonVariant& json) { handleLogin(req, json); }
  );
  server.addHandler(loginH);

  server.on("/api/logout", HTTP_POST, [](AsyncWebServerRequest* req) {
    Auth::logout(getCookieToken(req));
    AsyncWebServerResponse* res = req->beginResponse(200, "application/json", "{\"ok\":true}");
    res->addHeader("Set-Cookie", "session=; Path=/; Max-Age=0");
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isAuthed(req)) { req->redirect("/login.html"); return; }
    req->send(LittleFS, "/index.html", "text/html");
  });
  server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isAuthed(req)) { req->redirect("/login.html"); return; }
    req->send(LittleFS, "/index.html", "text/html");
  });
  server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isAuthed(req)) { req->send(401); return; }
    req->send(LittleFS, "/app.js", "text/javascript");
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!isAuthed(req)) { sendJson(req, 401, "{\"error\":\"unauth\"}"); return; }
    sendStatus(req);
  });

  AsyncCallbackJsonWebHandler* wifiH = new AsyncCallbackJsonWebHandler(
    "/api/wifi",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!isAuthed(req)) { sendJson(req, 401, "{\"error\":\"unauth\"}"); return; }
      String ssid = json["ssid"] | "";
      String pass = json["pass"] | "";
      if (ssid.isEmpty()) { sendJson(req, 400, "{\"error\":\"ssid_required\"}"); return; }
      Config::setWifi(ssid, pass);
      sendJson(req, 200, "{\"ok\":true,\"reboot\":true}");
      scheduleReboot();
    }
  );
  server.addHandler(wifiH);

  AsyncCallbackJsonWebHandler* shellyH = new AsyncCallbackJsonWebHandler(
    "/api/shelly",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!isAuthed(req)) { sendJson(req, 401, "{\"error\":\"unauth\"}"); return; }
      String main = json["main"] | "";
      String solar = json["solar"] | "";
      Config::setShellyUrls(main, solar);
      sendJson(req, 200, "{\"ok\":true}");
    }
  );
  server.addHandler(shellyH);

  AsyncCallbackJsonWebHandler* pwdH = new AsyncCallbackJsonWebHandler(
    "/api/password",
    [](AsyncWebServerRequest* req, JsonVariant& json) {
      if (!isAuthed(req)) { sendJson(req, 401, "{\"error\":\"unauth\"}"); return; }
      String oldPass = json["old"] | "";
      String newPass = json["new"] | "";
      if (newPass.length() < 8) { sendJson(req, 400, "{\"error\":\"too_short\"}"); return; }
      if (!Config::verifyAdminPassword(oldPass)) { sendJson(req, 401, "{\"error\":\"wrong_old\"}"); return; }
      Config::setAdminCredentials(Config::get().adminUser, newPass);
      Config::markBootCompleted();
      sendJson(req, 200, "{\"ok\":true}");
    }
  );
  server.addHandler(pwdH);

  server.on("/api/ota-password/regenerate", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!isAuthed(req)) { sendJson(req, 401, "{\"error\":\"unauth\"}"); return; }
    Config::regenerateOtaPassword();
    JsonDocument doc;
    doc["ok"] = true;
    doc["ota_password"] = Config::get().otaPassword;
    doc["reboot"] = true;
    String out;
    serializeJson(doc, out);
    sendJson(req, 200, out);
    scheduleReboot();
  });

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.onNotFound([](AsyncWebServerRequest* req) {
    req->send(404, "text/plain", "Not found");
  });

  server.begin();
  Serial.println(F("[WEB] Server listening on port 80"));
}

void WebPortal::loop() {
  static unsigned long lastCleanup = 0;
  unsigned long now = millis();
  if (now - lastCleanup > 60000) {
    lastCleanup = now;
    Auth::cleanup();
    ws.cleanupClients();
  }
  if (s_rebootPending && (long)(s_rebootAt - now) <= 0) {
    Serial.println(F("[WEB] Scheduled reboot now"));
    ESP.restart();
  }
}

void WebPortal::broadcastStatus(float powerMain, float powerSolar, bool mainOk, bool solarOk) {
  if (ws.count() == 0) return;
  JsonDocument doc;
  doc["powerMain"]  = powerMain;
  doc["powerSolar"] = powerSolar;
  doc["mainOk"]     = mainOk;
  doc["solarOk"]    = solarOk;
  doc["ts"]         = millis();
  String out;
  serializeJson(doc, out);
  ws.textAll(out);
}
