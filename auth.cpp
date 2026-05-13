#include "auth.h"
#include "config.h"

struct Session {
  String token;
  unsigned long expiresAt;
};

struct FailedLogin {
  IPAddress ip;
  uint8_t attempts;
  unsigned long firstAttemptMs;
  unsigned long lockoutUntilMs;
};

static const int MAX_SESSIONS = 4;
static const int MAX_FAILED_IPS = 8;
static const unsigned long SESSION_TTL_MS      = 30UL * 60UL * 1000UL;
static const uint8_t       LOCKOUT_MAX_ATTEMPTS = 5;
static const unsigned long LOCKOUT_WINDOW_MS    = 5UL * 60UL * 1000UL;
static const unsigned long LOCKOUT_DURATION_MS  = 10UL * 60UL * 1000UL;

static Session s_sessions[MAX_SESSIONS];
static FailedLogin s_failed[MAX_FAILED_IPS];

// Wrap-around safe: returns true if deadline is still in the future relative to now.
static inline bool isFuture(unsigned long deadline, unsigned long now) {
  return (long)(deadline - now) > 0;
}

static String randomToken() {
  uint8_t buf[24];
  for (int i = 0; i < 24; i++) buf[i] = esp_random() & 0xFF;
  String s;
  s.reserve(48);
  const char* hex = "0123456789abcdef";
  for (int i = 0; i < 24; i++) {
    s += hex[buf[i] >> 4];
    s += hex[buf[i] & 0xF];
  }
  return s;
}

static FailedLogin* findOrCreateFailed(const IPAddress& ip) {
  unsigned long now = millis();
  FailedLogin* freeSlot = nullptr;
  FailedLogin* oldest = &s_failed[0];
  for (int i = 0; i < MAX_FAILED_IPS; i++) {
    if (s_failed[i].attempts == 0 && !freeSlot) freeSlot = &s_failed[i];
    if (s_failed[i].attempts != 0 && s_failed[i].ip == ip) return &s_failed[i];
    if ((long)(s_failed[i].firstAttemptMs - oldest->firstAttemptMs) < 0) oldest = &s_failed[i];
  }
  FailedLogin* slot = freeSlot ? freeSlot : oldest;
  slot->ip = ip;
  slot->attempts = 0;
  slot->firstAttemptMs = now;
  slot->lockoutUntilMs = 0;
  return slot;
}

void Auth::begin() {
  for (int i = 0; i < MAX_SESSIONS; i++) {
    s_sessions[i].token = "";
    s_sessions[i].expiresAt = 0;
  }
  for (int i = 0; i < MAX_FAILED_IPS; i++) {
    s_failed[i].attempts = 0;
    s_failed[i].firstAttemptMs = 0;
    s_failed[i].lockoutUntilMs = 0;
  }
}

bool Auth::tryLogin(const String& user, const String& pass, const IPAddress& ip, String& outToken) {
  unsigned long now = millis();
  FailedLogin* f = findOrCreateFailed(ip);
  if (isFuture(f->lockoutUntilMs, now)) {
    Serial.printf("[AUTH] IP %s in lockout\n", ip.toString().c_str());
    return false;
  }

  bool ok = (user == Config::get().adminUser) && Config::verifyAdminPassword(pass);
  if (!ok) {
    if (now - f->firstAttemptMs > LOCKOUT_WINDOW_MS) {
      f->firstAttemptMs = now;
      f->attempts = 1;
    } else {
      f->attempts++;
    }
    if (f->attempts >= LOCKOUT_MAX_ATTEMPTS) {
      f->lockoutUntilMs = now + LOCKOUT_DURATION_MS;
      Serial.printf("[AUTH] IP %s locked out (%d attempts)\n", ip.toString().c_str(), f->attempts);
    } else {
      Serial.printf("[AUTH] Failed login IP %s (%d/%d)\n", ip.toString().c_str(), f->attempts, LOCKOUT_MAX_ATTEMPTS);
    }
    return false;
  }

  f->attempts = 0;
  f->lockoutUntilMs = 0;

  String token = randomToken();
  Session* slot = nullptr;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (s_sessions[i].token.isEmpty() || !isFuture(s_sessions[i].expiresAt, now)) {
      slot = &s_sessions[i];
      break;
    }
  }
  if (!slot) {
    Session* oldest = &s_sessions[0];
    for (int i = 1; i < MAX_SESSIONS; i++) {
      if ((long)(s_sessions[i].expiresAt - oldest->expiresAt) < 0) oldest = &s_sessions[i];
    }
    slot = oldest;
  }
  slot->token = token;
  slot->expiresAt = now + SESSION_TTL_MS;
  outToken = token;
  Serial.printf("[AUTH] Login OK from %s\n", ip.toString().c_str());
  return true;
}

bool Auth::validateToken(const String& token) {
  if (token.isEmpty()) return false;
  unsigned long now = millis();
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (!s_sessions[i].token.isEmpty() && s_sessions[i].token == token) {
      if (!isFuture(s_sessions[i].expiresAt, now)) {
        s_sessions[i].token = "";
        return false;
      }
      s_sessions[i].expiresAt = now + SESSION_TTL_MS;
      return true;
    }
  }
  return false;
}

void Auth::logout(const String& token) {
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (s_sessions[i].token == token) {
      s_sessions[i].token = "";
      s_sessions[i].expiresAt = 0;
    }
  }
}

void Auth::cleanup() {
  unsigned long now = millis();
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (!s_sessions[i].token.isEmpty() && !isFuture(s_sessions[i].expiresAt, now)) {
      s_sessions[i].token = "";
      s_sessions[i].expiresAt = 0;
    }
  }
}
