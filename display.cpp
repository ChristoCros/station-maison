#include "display.h"
#include "network_mgr.h"
#include "config.h"
#include <U8g2lib.h>
#include <Wire.h>
#include "time.h"

static const int LED_VERT  = 25;
static const int LED_JAUNE = 32;
static const int LED_ROUGE = 33;
static const int LED_BLEU  = 26;
static const int LED_BLANC = 27;

static U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

static void drawSmallLightning(int x, int y) {
  u8g2.drawTriangle(x + 4, y, x, y + 8, x + 4, y + 8);
  u8g2.drawTriangle(x + 3, y + 7, x + 7, y + 7, x + 3, y + 15);
}

static void drawSun(int x, int y) {
  u8g2.drawCircle(x, y, 3);
  for (int i = 0; i < 360; i += 45) {
    float angle = i * 0.01745f;
    u8g2.drawLine(x + cos(angle) * 4, y + sin(angle) * 4,
                  x + cos(angle) * 6, y + sin(angle) * 6);
  }
}

static String getFormattedTime() {
  struct tm t;
  if (!getLocalTime(&t, 10)) return "--:--";
  char b[8];
  strftime(b, sizeof(b), "%H:%M", &t);
  return String(b);
}

void Display::begin() {
  pinMode(LED_VERT,  OUTPUT);
  pinMode(LED_JAUNE, OUTPUT);
  pinMode(LED_ROUGE, OUTPUT);
  pinMode(LED_BLEU,  OUTPUT);
  pinMode(LED_BLANC, OUTPUT);

  Wire.begin(21, 22);
  u8g2.begin();
}

void Display::showBootMessage(const String& msg) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.setCursor(2, 14);
  u8g2.print("Solar Tracker");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setCursor(2, 32);
  u8g2.print(msg);
  u8g2.sendBuffer();
}

void Display::render(float powerMain, float powerSolar, bool staConnected, bool apMode) {
  u8g2.clearBuffer();

  if (apMode) {
    u8g2.setFont(u8g2_font_ncenB10_tr);
    u8g2.setCursor(2, 12);
    u8g2.print("SETUP MODE");
    u8g2.setFont(u8g2_font_5x8_tf);
    u8g2.setCursor(2, 26);
    u8g2.print("SSID: ");
    u8g2.print(NetworkMgr::apSSID());
    u8g2.setCursor(2, 38);
    u8g2.print("Pass: ");
    u8g2.print(Config::get().apPassword);
    u8g2.setCursor(2, 52);
    u8g2.print("http://");
    u8g2.print(NetworkMgr::localIP());
    u8g2.sendBuffer();
    return;
  }

  drawSmallLightning(5, 5);
  u8g2.setFont(u8g2_font_logisoso22_tn);
  u8g2.setCursor(35, 28);
  u8g2.print((int)powerMain);
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.print("W");

  u8g2.drawHLine(0, 34, 128);

  u8g2.setFont(u8g2_font_logisoso16_tn);
  u8g2.setCursor(5, 58);
  u8g2.print(getFormattedTime());

  u8g2.drawVLine(82, 38, 24);
  if (fabsf(powerSolar) > 5.0f) drawSun(106, 48);

  if (!staConnected) {
    u8g2.setFont(u8g2_font_5x8_tf);
    u8g2.setCursor(95, 8);
    u8g2.print("!WIFI");
  }

  u8g2.sendBuffer();
}

void Display::handleLeds(float powerMain) {
  if (powerMain > 1.0f) {
    digitalWrite(LED_VERT,  HIGH);
    digitalWrite(LED_JAUNE, (powerMain > 500.0f)  ? HIGH : LOW);
    digitalWrite(LED_ROUGE, (powerMain > 1000.0f) ? HIGH : LOW);
    digitalWrite(LED_BLEU,  LOW);
    digitalWrite(LED_BLANC, LOW);
  } else if (powerMain < -1.0f) {
    float injection = fabsf(powerMain);
    digitalWrite(LED_BLEU,  HIGH);
    digitalWrite(LED_BLANC, (injection >= 750.0f) ? HIGH : LOW);
    digitalWrite(LED_VERT,  LOW);
    digitalWrite(LED_JAUNE, LOW);
    digitalWrite(LED_ROUGE, LOW);
  } else {
    digitalWrite(LED_VERT,  LOW);
    digitalWrite(LED_JAUNE, LOW);
    digitalWrite(LED_ROUGE, LOW);
    digitalWrite(LED_BLEU,  LOW);
    digitalWrite(LED_BLANC, LOW);
  }
}
