// WaterMeterCam - Zaehlerstandserkennung fuer mechanische Wasserzaehler
// auf einer ESP32-CAM.
//
// Die Erkennung laeuft komplett auf dem Geraet, ohne Server, ohne Cloud und
// ohne eine einzige nachzuinstallierende Arduino-Bibliothek. Eingerichtet wird
// alles ueber die eingebaute Weboberflaeche.
//
// Board:      AI Thinker ESP32-CAM (oder ein anderes Modell unten waehlen)
// Partition:  "Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)"
// PSRAM:      Enabled
//
// Ausfuehrliche Anleitung: siehe README.md und docs/.

// Anderes Kameramodell? -> board_config.h (nicht hier: ein #define in der .ino
// ist fuer die einzeln uebersetzten .cpp-Dateien unsichtbar).

#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <time.h>

#include "camera.h"
#include "config.h"
#include "meter.h"
#include "mqtt.h"
#include "recognize.h"
#include "templates.h"
#include "webui.h"

static uint32_t s_nextMeasure = 0;
static bool s_apMode = false;

static String apName() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[32];
  snprintf(buf, sizeof(buf), "WaterMeterCam-%04X", (uint16_t)(mac >> 32));
  return String(buf);
}

static void startAccessPoint() {
  s_apMode = true;
  WiFi.mode(WIFI_AP);
  // Das Passwort steht im README - ohne Passwort waere das offene Netz im
  // Keller die groessere Baustelle als der vergessene Zettel.
  WiFi.softAP(apName().c_str(), "wasserzaehler");
  Serial.printf("[wifi] Access Point \"%s\" - http://%s/\n", apName().c_str(),
                WiFi.softAPIP().toString().c_str());
}

static void startWifi() {
  if (!cfg.wifiSsid[0]) {
    Serial.println("[wifi] keine SSID konfiguriert");
    startAccessPoint();
    return;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // Modem-Sleep macht den Webserver traege
  WiFi.setHostname(cfg.hostname);
  WiFi.begin(cfg.wifiSsid, cfg.wifiPass);
  Serial.printf("[wifi] verbinde mit \"%s\" ", cfg.wifiSsid);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[wifi] fehlgeschlagen - starte eigenen Access Point");
    startAccessPoint();
    return;
  }

  Serial.printf("[wifi] verbunden, IP %s\n", WiFi.localIP().toString().c_str());
  if (MDNS.begin(cfg.hostname)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[wifi] erreichbar unter http://%s.local/\n", cfg.hostname);
  }
  // Zeitstempel sind nur ein Komfortmerkmal - schlaegt NTP fehl, laeuft alles
  // andere unveraendert weiter.
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== " FW_NAME " " FW_VERSION " ===");
  Serial.printf("PSRAM: %s (%u kB frei)\n", psramFound() ? "ja" : "nein",
                (unsigned)(ESP.getFreePsram() / 1024));

  if (!LittleFS.begin(true)) {
    Serial.println("[fs] LittleFS konnte nicht eingebunden werden!");
    Serial.println("[fs] Partitionsschema mit SPIFFS-Bereich waehlen.");
  }

  configLoad();

  if (!cameraInit()) {
    Serial.println("[cam] ohne Kamera geht nichts - Neustart in 5 s");
    delay(5000);
    ESP.restart();
  }

  templatesInit();
  recognizeInit();
  meterInit();

  startWifi();
  mqttSetup();
  webSetup();

  // Erste Messung kurz nach dem Start, damit man beim Einrichten nicht wartet.
  s_nextMeasure = millis() + 3000;
}

void loop() {
  webLoop();
  mqttLoop();

  if ((int32_t)(millis() - s_nextMeasure) >= 0) {
    s_nextMeasure = millis() + cfg.measureIntervalSec * 1000UL;
    bool ok = webRunMeasurement();
    const MeterStatus &m = meterStatus();
    Serial.printf("[messung] %s  Wert=%.*f  roh=%.*f  konfidenz=%u%%  %s\n",
                  ok ? "ok  " : "fail", (int)configDecimals(), m.value, (int)configDecimals(),
                  m.lastRawValue, m.confidence, m.lastRejectText);
  }

  // Wenn der Access Point laeuft, hat das Verbinden beim Start nicht geklappt.
  // Alle 5 Minuten neu probieren, damit ein spaeter wieder erreichbarer Router
  // das Geraet von allein einsammelt.
  static uint32_t retryAt = 0;
  if (s_apMode && cfg.wifiSsid[0]) {
    if (retryAt == 0) retryAt = millis() + 300000;
    if ((int32_t)(millis() - retryAt) >= 0) {
      Serial.println("[wifi] neuer Verbindungsversuch");
      ESP.restart();
    }
  }
}
