#pragma once
#include <Arduino.h>

namespace WebPortal {
  void begin();
  void loop();
  void broadcastStatus(float powerMain, float powerSolar, bool mainOk, bool solarOk);
}
