// LittleFS-Ersatz fuer den Host-Testlauf: bildet das Dateisystem auf ein
// Verzeichnis unter /tmp ab, damit Konfiguration und Vorlagen wie auf dem
// Geraet gespeichert und wieder gelesen werden.
#pragma once

#include <cstdio>
#include <string>

#include "Arduino.h"

class File {
 public:
  File() {}
  explicit File(FILE *f) : f_(f) {}
  operator bool() const { return f_ != nullptr; }

  int read(uint8_t *buf, size_t n) { return f_ ? (int)fread(buf, 1, n, f_) : -1; }
  size_t write(const uint8_t *buf, size_t n) { return f_ ? fwrite(buf, 1, n, f_) : 0; }
  size_t size() {
    if (!f_) return 0;
    long cur = ftell(f_);
    fseek(f_, 0, SEEK_END);
    long end = ftell(f_);
    fseek(f_, cur, SEEK_SET);
    return (size_t)end;
  }
  void close() {
    if (f_) fclose(f_);
    f_ = nullptr;
  }

 private:
  FILE *f_ = nullptr;
};

class LittleFSStub {
 public:
  bool begin(bool = false) { return true; }
  File open(const char *path, const char *mode) {
    return File(fopen(full(path).c_str(), mode[0] == 'w' ? "wb" : "rb"));
  }
  bool remove(const char *path) { return ::remove(full(path).c_str()) == 0; }
  bool rename(const char *a, const char *b) {
    return ::rename(full(a).c_str(), full(b).c_str()) == 0;
  }

  static std::string root;

 private:
  std::string full(const char *p) { return root + p; }
};

extern LittleFSStub LittleFS;
