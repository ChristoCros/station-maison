#pragma once
#include <Arduino.h>

struct ShellyReading {
  float actPower = 0.0f;
  float voltage  = 0.0f;
  float current  = 0.0f;
  bool  valid    = false;
};

namespace Shelly {
  bool read(const String& url, ShellyReading& out);
}
