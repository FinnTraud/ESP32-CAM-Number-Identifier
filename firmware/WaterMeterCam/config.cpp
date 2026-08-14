#include "config.h"

#include <LittleFS.h>

#include "util.h"

Settings cfg;

static const char *CONFIG_PATH = "/config.bin";

void configSetDefaults(Settings &s) {
  memset(&s, 0, sizeof(s));
  s.magic = CONFIG_MAGIC;
  s.version = CONFIG_VERSION;

  s.wifiSsid[0] = 0;
  s.wifiPass[0] = 0;
  strcpy(s.hostname, "wasserzaehler");

  s.flashBrightness = 40;  // bewusst niedrig: die LED blendet sonst das Zifferblatt aus
  s.brightness = 0;
  s.contrast = 1;
  s.saturation = 0;
  s.vflip = 0;
  s.hmirror = 0;
  s.aeLevel = 0;
  s.autoExposure = 1;
  s.manualExposure = 300;
  s.agcGain = 0;
  s.warmupFrames = 3;

  s.rotationDeg = 0.0f;

  // Typischer deutscher Hauswasserzaehler: 5 schwarze m3-Rollen, dahinter drei
  // rote Zeiger fuer 0,1 / 0,01 / 0,001 m3. Die Rechtecke sind nur Startwerte
  // und muessen in der Web-UI auf das eigene Bild gezogen werden.
  s.digitCount = 5;
  for (uint8_t i = 0; i < s.digitCount; i++) {
    s.digits[i].x = 200 + i * 40;
    s.digits[i].y = 210;
    s.digits[i].w = 32;
    s.digits[i].h = 48;
  }
  s.decimalDigits = 0;

  s.dialCount = 0;
  for (uint8_t i = 0; i < MAX_DIALS; i++) {
    s.dials[i].roi.x = 200 + i * 70;
    s.dials[i].roi.y = 320;
    s.dials[i].roi.w = 64;
    s.dials[i].roi.h = 64;
    s.dials[i].offsetDeg = 0;
    s.dials[i].clockwise = 1;
  }

  s.polarity = 0;
  s.rollDirection = 1;
  s.shiftSearch = MAX_SHIFT;
  s.minConfidence = 35;
  s.sharedTemplates = 1;
  s.autoLearn = 1;

  s.measureIntervalSec = 300;
  s.maxRatePerMin = 0.05f;  // 50 l/min - deutlich ueber jedem Haushaltsverbrauch
  s.allowDecrease = 0;

  s.mqttEnabled = 0;
  s.mqttHost[0] = 0;
  s.mqttPort = 1883;
  s.mqttUser[0] = 0;
  s.mqttPass[0] = 0;
  strcpy(s.mqttTopic, "wasserzaehler");
  s.mqttDiscovery = 1;
  strcpy(s.unit, "m³");

  s.crc = 0;
}

static uint32_t configCrc(const Settings &s) {
  // Alles ausser dem CRC-Feld selbst.
  return crc32((const uint8_t *)&s, sizeof(Settings) - sizeof(uint32_t));
}

bool configLoad() {
  configSetDefaults(cfg);

  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) return false;
  if (f.size() != sizeof(Settings)) {
    f.close();
    Serial.println("[cfg] Groesse passt nicht - Standardwerte");
    return false;
  }

  Settings tmp;
  size_t n = f.read((uint8_t *)&tmp, sizeof(tmp));
  f.close();
  if (n != sizeof(tmp)) return false;

  if (tmp.magic != CONFIG_MAGIC || tmp.version != CONFIG_VERSION) {
    Serial.println("[cfg] Magic/Version passt nicht - Standardwerte");
    return false;
  }
  if (configCrc(tmp) != tmp.crc) {
    Serial.println("[cfg] CRC-Fehler - Standardwerte");
    return false;
  }

  cfg = tmp;
  // Nullterminierung erzwingen, damit ein beschaedigtes Image keinen
  // Ueberlauf beim Ausgeben verursacht.
  cfg.wifiSsid[sizeof(cfg.wifiSsid) - 1] = 0;
  cfg.wifiPass[sizeof(cfg.wifiPass) - 1] = 0;
  cfg.hostname[sizeof(cfg.hostname) - 1] = 0;
  cfg.mqttHost[sizeof(cfg.mqttHost) - 1] = 0;
  cfg.mqttUser[sizeof(cfg.mqttUser) - 1] = 0;
  cfg.mqttPass[sizeof(cfg.mqttPass) - 1] = 0;
  cfg.mqttTopic[sizeof(cfg.mqttTopic) - 1] = 0;
  cfg.unit[sizeof(cfg.unit) - 1] = 0;
  if (!cfg.unit[0]) strcpy(cfg.unit, "m³");

  if (cfg.digitCount > MAX_DIGITS) cfg.digitCount = MAX_DIGITS;
  if (cfg.dialCount > MAX_DIALS) cfg.dialCount = MAX_DIALS;
  if (cfg.shiftSearch < 1) cfg.shiftSearch = 1;
  if (cfg.shiftSearch > MAX_SHIFT) cfg.shiftSearch = MAX_SHIFT;
  if (cfg.rollDirection != 1 && cfg.rollDirection != -1) cfg.rollDirection = 1;
  if (cfg.decimalDigits > cfg.digitCount) cfg.decimalDigits = 0;
  if (cfg.measureIntervalSec < 10) cfg.measureIntervalSec = 10;

  Serial.println("[cfg] geladen");
  return true;
}

bool configSave() {
  cfg.magic = CONFIG_MAGIC;
  cfg.version = CONFIG_VERSION;
  cfg.crc = configCrc(cfg);

  // Erst in eine temporaere Datei schreiben, dann umbenennen: ein Reset
  // mitten im Schreiben laesst so die alte Konfiguration intakt.
  File f = LittleFS.open("/config.tmp", "w");
  if (!f) {
    Serial.println("[cfg] Schreiben fehlgeschlagen");
    return false;
  }
  size_t n = f.write((const uint8_t *)&cfg, sizeof(cfg));
  f.close();
  if (n != sizeof(cfg)) {
    LittleFS.remove("/config.tmp");
    return false;
  }
  LittleFS.remove(CONFIG_PATH);
  if (!LittleFS.rename("/config.tmp", CONFIG_PATH)) {
    Serial.println("[cfg] rename fehlgeschlagen");
    return false;
  }
  Serial.println("[cfg] gespeichert");
  return true;
}
