// Minimaler Arduino-Ersatz, damit die Erkennungslogik auf dem PC uebersetzt
// und getestet werden kann. Er bildet nur das nach, was die Firmware in den
// hier geprueften Dateien tatsaechlich benutzt - er ist kein Emulator.
#pragma once

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define PROGMEM
#define PSTR(s) (s)
#define pgm_read_byte(a) (*(const uint8_t *)(a))
#define memcpy_P memcpy

// --- String ---------------------------------------------------------------

class String {
 public:
  String() {}
  String(const char *s) : s_(s ? s : "") {}
  String(const std::string &s) : s_(s) {}
  String(char c) : s_(1, c) {}
  String(int v) { s_ = std::to_string(v); }
  String(long v) { s_ = std::to_string(v); }
  String(unsigned v) { s_ = std::to_string(v); }
  String(unsigned long v) { s_ = std::to_string(v); }
  String(double v, int digits = 2) {
    char b[64];
    snprintf(b, sizeof(b), "%.*f", digits, v);
    s_ = b;
  }
  String(float v, int digits = 2) {
    char b[64];
    snprintf(b, sizeof(b), "%.*f", digits, (double)v);
    s_ = b;
  }

  const char *c_str() const { return s_.c_str(); }
  size_t length() const { return s_.size(); }
  bool isEmpty() const { return s_.empty(); }
  long toInt() const { return strtol(s_.c_str(), nullptr, 10); }
  float toFloat() const { return strtof(s_.c_str(), nullptr); }
  double toDouble() const { return strtod(s_.c_str(), nullptr); }

  String &operator+=(const String &o) {
    s_ += o.s_;
    return *this;
  }
  String &operator+=(const char *o) {
    s_ += o;
    return *this;
  }
  String &operator+=(char c) {
    s_ += c;
    return *this;
  }
  bool operator==(const String &o) const { return s_ == o.s_; }

  void remove(size_t i) { if (i < s_.size()) s_.erase(i); }
  bool endsWith(const String &o) const {
    return s_.size() >= o.s_.size() && s_.compare(s_.size() - o.s_.size(), o.s_.size(), o.s_) == 0;
  }
  void replace(char a, char b) {
    for (auto &c : s_)
      if (c == a) c = b;
  }

  std::string s_;
};

inline String operator+(const String &a, const String &b) { return String(a.s_ + b.s_); }
inline String operator+(const String &a, const char *b) { return String(a.s_ + b); }
inline String operator+(const char *a, const String &b) { return String(std::string(a) + b.s_); }

// --- Zeit / Serial --------------------------------------------------------

unsigned long millis();
inline void delay(unsigned long) {}

struct SerialStub {
  void begin(unsigned long) {}
  void print(const char *s) { fputs(s, stdout); }
  void println() { fputs("\n", stdout); }
  void println(const char *s) { printf("%s\n", s); }
  void printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
  }
};
extern SerialStub Serial;

// --- ESP / PSRAM ----------------------------------------------------------

inline bool psramFound() { return false; }
inline void *ps_malloc(size_t n) { return malloc(n); }

struct EspStub {
  uint32_t getFreeHeap() { return 200000; }
  uint32_t getFreePsram() { return 0; }
  uint64_t getEfuseMac() { return 0x0011223344556677ULL; }
  void restart() { exit(0); }
};
extern EspStub ESP;
