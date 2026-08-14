// Kleine Helfer ohne eigene Abhaengigkeiten.
#pragma once

#include <Arduino.h>

uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t len);
uint32_t crc32(const uint8_t *data, size_t len);

// Kopiert einen String sicher in ein festes char-Array (immer nullterminiert).
void copyStr(char *dst, size_t dstSize, const String &src);

// Haengt einen JSON-String-Wert (inkl. Anfuehrungszeichen) escaped an out an.
void jsonString(String &out, const char *value);

template <typename T>
inline T clampT(T v, T lo, T hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}
