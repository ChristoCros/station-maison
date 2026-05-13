#pragma once
#include <Arduino.h>
#include <IPAddress.h>

namespace Auth {
  void begin();
  bool tryLogin(const String& user, const String& pass, const IPAddress& ip, String& outToken);
  bool validateToken(const String& token);
  void logout(const String& token);
  void cleanup();
}
