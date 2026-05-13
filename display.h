#pragma once
#include <Arduino.h>

namespace Display {
  void begin();
  void showBootMessage(const String& msg);
  void render(float powerMain, float powerSolar, bool staConnected, bool apMode);
  void handleLeds(float powerMain);
}
