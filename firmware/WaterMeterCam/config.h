// Zentrale Konstanten und die persistente Konfiguration.
#pragma once

#include <Arduino.h>

#define FW_NAME "WaterMeterCam"
#define FW_VERSION "1.0.0"

// ---------------------------------------------------------------------------
// Feste Groessen
// ---------------------------------------------------------------------------

// Arbeitsaufloesung der Kamera. VGA graustufig = 300 KB und landet im PSRAM.
#define FRAME_W 640
#define FRAME_H 480

#define MAX_DIGITS 8  // schwarze Rollen (Vorkomma + ggf. Nachkomma)
#define MAX_DIALS 4   // rote Zeiger-Rollen

// Groesse eines normierten Ziffernausschnitts bzw. eines Templates.
#define TPL_W 16
#define TPL_H 24
#define TPL_PIX (TPL_W * TPL_H)
#define NUM_CLASSES 10

// Vertikaler Suchbereich beim Abgleich, in Template-Zeilen. Eine halbe
// Ziffernhoehe reicht aus: ist die Rolle weiter als das gedreht, passt die
// naechste Ziffer besser - genau das soll der Abgleich dann auch finden.
// Deshalb wird der Ausschnitt (TPL_H + 2*MAX_SHIFT) Zeilen hoch extrahiert.
#define MAX_SHIFT (TPL_H / 2)
#define MAX_OFFSETS (2 * MAX_SHIFT + 1)
#define PATCH_H (TPL_H + 2 * MAX_SHIFT)
#define PATCH_PIX (TPL_W * PATCH_H)

// Index des "ungedrehten" Offsets im Suchfenster.
#define OFFSET_CENTER MAX_SHIFT

// ---------------------------------------------------------------------------
// Konfiguration (wird als Binaerblob in LittleFS abgelegt)
// ---------------------------------------------------------------------------

#define CONFIG_MAGIC 0x574D4331UL  // "WMC1"
#define CONFIG_VERSION 1

struct Roi {
  int16_t x, y, w, h;
};

struct DialCfg {
  Roi roi;
  int16_t offsetDeg;  // Winkel der "0" gegenueber 12 Uhr
  int8_t clockwise;   // 1 = im Uhrzeigersinn steigend, -1 = dagegen
};

struct Settings {
  uint32_t magic;
  uint16_t version;

  // --- Netzwerk ---
  char wifiSsid[33];
  char wifiPass[65];
  char hostname[33];

  // --- Kamera ---
  uint8_t flashBrightness;  // PWM 0..255 fuer die weisse LED an GPIO4
  int8_t brightness;        // -2..2
  int8_t contrast;          // -2..2
  int8_t saturation;        // -2..2 (bei Graustufen wirkungslos, aber gespeichert)
  uint8_t vflip;
  uint8_t hmirror;
  int8_t aeLevel;           // -2..2
  uint8_t autoExposure;     // 1 = AEC an
  uint16_t manualExposure;  // aec_value 0..1200, nur wenn autoExposure == 0
  uint8_t agcGain;          // 0..30, nur wenn autoExposure == 0
  uint8_t warmupFrames;     // Frames, die nach dem Einschalten der LED verworfen werden

  // --- Geometrie ---
  float rotationDeg;  // Bildrotation zur Korrektur einer schiefen Montage
  uint8_t digitCount;
  Roi digits[MAX_DIGITS];
  uint8_t dialCount;
  DialCfg dials[MAX_DIALS];
  uint8_t decimalDigits;  // wie viele der schwarzen Rollen hinter dem Komma stehen

  // --- Erkennung ---
  uint8_t polarity;      // 0 = automatisch, 1 = dunkel auf hell, 2 = hell auf dunkel
  int8_t rollDirection;  // +1 = Ziffer wandert beim Hochzaehlen nach oben
  uint8_t shiftSearch;   // 1..MAX_SHIFT
  uint8_t minConfidence; // 0..100
  uint8_t sharedTemplates;  // 1 = eine gemeinsame Vorlagensammlung fuer alle Stellen
  uint8_t autoLearn;        // Templates bei sicheren Messungen nachfuehren

  // --- Plausibilitaet ---
  uint32_t measureIntervalSec;
  float maxRatePerMin;  // maximal zulaessiger Zuwachs je Minute, in Zaehlereinheiten
  uint8_t allowDecrease;

  // --- MQTT ---
  uint8_t mqttEnabled;
  char mqttHost[64];
  uint16_t mqttPort;
  char mqttUser[33];
  char mqttPass[65];
  char mqttTopic[64];
  uint8_t mqttDiscovery;  // Home-Assistant-Autodiscovery senden
  char unit[8];           // Einheit des Zaehlerstands, z. B. "m3"

  uint32_t crc;
};

extern Settings cfg;

void configSetDefaults(Settings &s);
bool configLoad();
bool configSave();

// Anzahl Nachkommastellen des Gesamtwerts (schwarze Nachkommarollen + Zeiger).
inline uint8_t configDecimals() { return cfg.decimalDigits + cfg.dialCount; }

// Anzahl aller Kaskadenglieder (Rollen + Zeiger).
inline uint8_t configElements() { return cfg.digitCount + cfg.dialCount; }
