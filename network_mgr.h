#pragma once
#include <Arduino.h>

namespace NetworkMgr {
  bool begin();
  void loop();
  bool isStationConnected();
  bool isApMode();
  String localIP();
  String apSSID();
}
