#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Wire.h>
#include "time.h"

// ===== CONFIGURATION PINS LEDs =====
const int LED_VERT   = 25; // Conso 0-500W
const int LED_JAUNE  = 32; // Conso 500-1000W
const int LED_ROUGE  = 33; // Conso > 1000W
const int LED_BLEU   = 26; // Prod 0-750W
const int LED_BLANC  = 27; // Prod > 750W (Pin RX2)

// ===== WIFI & URL =====
const char* ssid = "xxxxxxxx";
const char* password = "xxxxxxxx";
const char* url_id0 = "http://localhost/rpc/EM1.GetStatus?id=0"; 
const char* url_id1 = "http://localhost/rpc/EM1.GetStatus?id=1"; 
const char* ntpServer = "pool.ntp.org";

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

void drawSmallLightning(int x, int y) {
  u8g2.drawTriangle(x + 4, y, x, y + 8, x + 4, y + 8);
  u8g2.drawTriangle(x + 3, y + 7, x + 7, y + 7, x + 3, y + 15);
}

void drawSun(int x, int y) {
  u8g2.drawCircle(x, y, 3);
  for (int i = 0; i < 360; i += 45) {
    float angle = i * 0.01745;
    u8g2.drawLine(x + cos(angle)*4, y + sin(angle)*4, x + cos(angle)*6, y + sin(angle)*6);
  }
}

// --- FONCTION DE GESTION DES LEDS ---
void gererLEDs(float powerMain) {
  // --- CAS 1 : CONSOMMATION (Valeur positive) ---
  if (powerMain > 1.0) {
    digitalWrite(LED_VERT,  HIGH); 
    digitalWrite(LED_JAUNE, (powerMain > 500.0)  ? HIGH : LOW);
    digitalWrite(LED_ROUGE, (powerMain > 1000.0) ? HIGH : LOW);
    
    // On éteint les LEDs de production
    digitalWrite(LED_BLEU,  LOW);
    digitalWrite(LED_BLANC, LOW);
  } 
  
  // --- CAS 2 : PRODUCTION / INJECTION (Valeur négative) ---
  else if (powerMain < -1.0) {
    float injection = abs(powerMain); // On transforme -800 en 800

    digitalWrite(LED_BLEU,  HIGH); 
    // La blanche s'allume si l'injection dépasse 750W, sinon elle s'éteint
    digitalWrite(LED_BLANC, (injection >= 750.0) ? HIGH : LOW);
    
    // On éteint les LEDs de consommation
    digitalWrite(LED_VERT,  LOW);
    digitalWrite(LED_JAUNE, LOW);
    digitalWrite(LED_ROUGE, LOW);
  }
  
  // --- CAS 3 : ÉQUILIBRE (Proche de 0W) ---
  else {
    digitalWrite(LED_VERT,  LOW);
    digitalWrite(LED_JAUNE, LOW);
    digitalWrite(LED_ROUGE, LOW);
    digitalWrite(LED_BLEU,  LOW);
    digitalWrite(LED_BLANC, LOW);
  }
}

String getFormattedTime() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) return "--:--";
  char timeStringBuff[10];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M", &timeinfo);
  return String(timeStringBuff);
}

float getShellyPower(const char* url) {
  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  float p = 0;
  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<512> doc;
    deserializeJson(doc, payload);
    p = doc["act_power"];
  }
  http.end();
  return p;
}

void setup() {
  // Config LEDs
  pinMode(LED_VERT, OUTPUT);
  pinMode(LED_JAUNE, OUTPUT);
  pinMode(LED_ROUGE, OUTPUT);
  pinMode(LED_BLEU, OUTPUT);
  pinMode(LED_BLANC, OUTPUT);

  Wire.begin(21, 22);
  u8g2.begin();
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  configTime(3600, 3600, ntpServer); 
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    float powerMain = getShellyPower(url_id0);
    float powerSolar = getShellyPower(url_id1);
    int pMainInt = (int)powerMain;

    u8g2.clearBuffer(); // Efface TOUT l'écran en mémoire avant de redessiner
    
    // --- ZONE HAUTE ---
    drawSmallLightning(5, 5); 
    
    u8g2.setFont(u8g2_font_logisoso22_tn);
    
 
    
    // Position fixe à 35px
    u8g2.setCursor(35, 28); 
    u8g2.print(pMainInt);
    
    // On ajoute le "W"
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.print("W");

    // --- SÉPARATEUR ---
    u8g2.drawHLine(0, 34, 128);

    // --- ZONE BASSE ---
    u8g2.setFont(u8g2_font_logisoso16_tn);
    u8g2.setCursor(5, 58);
    u8g2.print(getFormattedTime());
    
    u8g2.drawVLine(82, 38, 24);

    if (abs(powerSolar) > 5.0) {
      drawSun(106, 48);
    }
    
    u8g2.sendBuffer(); // Envoie le dessin propre à l'écran

    gererLEDs(powerMain);
  }
  delay(2000); 
}