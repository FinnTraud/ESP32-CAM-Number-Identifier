#include "webui.h"

#include <ESPmDNS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <img_converters.h>
#include <math.h>

#include "camera.h"
#include "config.h"
#include "meter.h"
#include "mqtt.h"
#include "recognize.h"
#include "templates.h"
#include "util.h"
#include "webui_html.h"

#define DIAL_PREVIEW 48

static WebServer server(80);

static uint8_t *s_frame = nullptr;  // Rohbild der letzten Aufnahme
static int s_frameW = 0, s_frameH = 0;
static bool s_frameValid = false;

// ---------------------------------------------------------------------------
// Bildpuffer
// ---------------------------------------------------------------------------

bool webCaptureFrame() {
  camera_fb_t *fb = cameraGrab();
  if (!fb) return false;

  if (!s_frame || s_frameW != (int)fb->width || s_frameH != (int)fb->height) {
    if (s_frame) free(s_frame);
    size_t n = (size_t)fb->width * fb->height;
    s_frame = (uint8_t *)(psramFound() ? ps_malloc(n) : malloc(n));
    if (!s_frame) {
      cameraRelease(fb);
      s_frameValid = false;
      return false;
    }
    s_frameW = fb->width;
    s_frameH = fb->height;
  }
  memcpy(s_frame, fb->buf, (size_t)s_frameW * s_frameH);
  cameraRelease(fb);
  s_frameValid = true;
  return true;
}

bool webWorkingFrame(GrayImage *out) {
  if (!s_frameValid || !s_frame) return false;
  out->data = s_frame;
  out->w = s_frameW;
  out->h = s_frameH;
  return true;
}

// Sorgt dafuer, dass ein Bild vorliegt - nimmt bei Bedarf eines auf.
static bool ensureFrame(GrayImage *img) {
  if (!webWorkingFrame(img)) {
    if (!webCaptureFrame()) return false;
    return webWorkingFrame(img);
  }
  return true;
}

// ---------------------------------------------------------------------------
// Messung
// ---------------------------------------------------------------------------

bool webRunMeasurement() {
  if (!webCaptureFrame()) {
    meterProcess(nullptr);
    return false;
  }
  GrayImage img;
  if (!webWorkingFrame(&img)) return false;

  bool ok = recognizeRun(img);
  Recognition *rec = recognizeResult();
  bool accepted = meterProcess(ok ? rec : nullptr);

  // Vorlagen nur nachfuehren, wenn das Ergebnis die Plausibilitaetspruefung
  // bestanden hat - sonst lernt sich die Erkennung ihre eigenen Fehler ein.
  if (accepted && cfg.autoLearn && rec && rec->valid && rec->confidence >= 70) {
    for (uint8_t i = 0; i < rec->digitCount; i++) {
      const DigitResult &d = rec->digits[i];
      if (d.digit >= 0 && d.confidence >= 70 && d.score > 0.8f) {
        static uint8_t win[TPL_PIX];
        recognizeWindow(d.patch, d.offset, win);
        templateLearn(templateBankFor(i), (uint8_t)d.digit, win);
      }
    }
  }

  if (cfg.mqttEnabled) mqttPublishState();
  return accepted;
}

// ---------------------------------------------------------------------------
// Kleine Helfer
// ---------------------------------------------------------------------------

static int argI(const char *n, int def) {
  return server.hasArg(n) ? (int)server.arg(n).toInt() : def;
}
static float argF(const char *n, float def) {
  return server.hasArg(n) ? server.arg(n).toFloat() : def;
}
static void argS(const char *n, char *dst, size_t size) {
  if (server.hasArg(n)) copyStr(dst, size, server.arg(n));
}

static void sendJson(const String &s, int code = 200) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json; charset=utf-8", s);
}

// Vergroessert einen Graustufenpuffer per Pixelwiederholung und schickt ihn
// als JPEG. So bleiben die winzigen Ausschnitte im Browser erkennbar.
static void sendGrayAsJpeg(const uint8_t *src, int w, int h, int scale, int quality) {
  int ow = w * scale, oh = h * scale;
  uint8_t *big = (uint8_t *)(psramFound() ? ps_malloc((size_t)ow * oh) : malloc((size_t)ow * oh));
  if (!big) {
    server.send(500, "text/plain", "kein Speicher");
    return;
  }
  for (int y = 0; y < oh; y++) {
    const uint8_t *s = src + (size_t)(y / scale) * w;
    uint8_t *d = big + (size_t)y * ow;
    for (int x = 0; x < ow; x++) d[x] = s[x / scale];
  }

  uint8_t *jpg = nullptr;
  size_t jlen = 0;
  bool ok = fmt2jpg(big, (size_t)ow * oh, ow, oh, PIXFORMAT_GRAYSCALE, quality, &jpg, &jlen);
  free(big);
  if (!ok) {
    server.send(500, "text/plain", "JPEG fehlgeschlagen");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(jlen);
  server.send(200, "image/jpeg", "");
  server.client().write(jpg, jlen);
  free(jpg);
}

// ---------------------------------------------------------------------------
// Endpunkte
// ---------------------------------------------------------------------------

static void handleRoot() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

static void handleSnapshot() {
  if (!webCaptureFrame()) {
    server.send(503, "text/plain", "Aufnahme fehlgeschlagen");
    return;
  }
  GrayImage img;
  if (!webWorkingFrame(&img)) {
    server.send(503, "text/plain", "kein Bild");
    return;
  }

  const uint8_t *src = img.data;
  uint8_t *rot = nullptr;
  if (fabsf(cfg.rotationDeg) > 0.01f) {
    size_t n = (size_t)img.w * img.h;
    rot = (uint8_t *)(psramFound() ? ps_malloc(n) : malloc(n));
    if (rot) {
      rotateFrame(img, rot, cfg.rotationDeg);
      src = rot;
    }
  }

  uint8_t *jpg = nullptr;
  size_t jlen = 0;
  bool ok = fmt2jpg((uint8_t *)src, (size_t)img.w * img.h, img.w, img.h, PIXFORMAT_GRAYSCALE, 85,
                    &jpg, &jlen);
  if (rot) free(rot);
  if (!ok) {
    server.send(500, "text/plain", "JPEG fehlgeschlagen");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(jlen);
  server.send(200, "image/jpeg", "");
  server.client().write(jpg, jlen);
  free(jpg);
}

static void handleDigitJpeg() {
  int i = argI("i", 0);
  Recognition *r = recognizeResult();
  if (!r || i < 0 || i >= r->digitCount) {
    server.send(404, "text/plain", "keine Stelle");
    return;
  }
  // Kopie anlegen und das gewaehlte Fenster markieren.
  static uint8_t buf[PATCH_PIX];
  memcpy(buf, r->digits[i].patch, PATCH_PIX);
  int o = clampT((int)r->digits[i].offset, 0, 2 * MAX_SHIFT);
  for (int x = 0; x < TPL_W; x++) {
    buf[(size_t)o * TPL_W + x] = 255;
    buf[(size_t)(o + TPL_H - 1) * TPL_W + x] = 255;
  }
  sendGrayAsJpeg(buf, TPL_W, PATCH_H, 4, 85);
}

static void handleDialJpeg() {
  int i = argI("i", 0);
  if (i < 0 || i >= cfg.dialCount) {
    server.send(404, "text/plain", "kein Zeiger");
    return;
  }
  GrayImage img;
  if (!ensureFrame(&img)) {
    server.send(503, "text/plain", "kein Bild");
    return;
  }
  static uint8_t buf[DIAL_PREVIEW * DIAL_PREVIEW];
  recognizeRenderDial(img, (uint8_t)i, buf, DIAL_PREVIEW);
  sendGrayAsJpeg(buf, DIAL_PREVIEW, DIAL_PREVIEW, 2, 85);
}

static void handleTemplateJpeg() {
  int b = argI("b", TPL_SHARED_BANK);
  int c = argI("c", 0);
  const uint8_t *t = templateData((uint8_t)b, (uint8_t)c);
  if (!t) {
    server.send(404, "text/plain", "keine Vorlage");
    return;
  }
  sendGrayAsJpeg(t, TPL_W, TPL_H, 4, 88);
}

static void handleTemplatesJson() {
  int b = clampT(argI("b", TPL_SHARED_BANK), 0, TPL_BANKS - 1);
  String j = "{\"bank\":" + String(b);
  j += ",\"learned\":" + String(templatesLearnedCount((uint8_t)b));
  j += ",\"weights\":[";
  for (int c = 0; c < NUM_CLASSES; c++) {
    if (c) j += ',';
    j += String(templateWeight((uint8_t)b, (uint8_t)c));
  }
  j += "]}";
  sendJson(j);
}

static void handleStatus() {
  const MeterStatus &m = meterStatus();
  Recognition *r = recognizeResult();
  char num[32];

  String j = "{";
  j += "\"hasValue\":" + String(m.hasValue ? "true" : "false");
  snprintf(num, sizeof(num), "%.*f", (int)configDecimals(), m.value);
  j += ",\"value\":\"" + String(num) + "\"";
  snprintf(num, sizeof(num), "%.*f", (int)configDecimals(), m.lastRawValue);
  j += ",\"raw\":\"" + String(num) + "\"";
  j += ",\"confidence\":" + String(m.confidence);
  j += ",\"minConfidence\":" + String(cfg.minConfidence);
  j += ",\"readings\":" + String(m.readings);
  j += ",\"rejected\":" + String(m.rejected);
  j += ",\"reject\":";
  jsonString(j, m.lastRejectText);
  j += ",\"epoch\":" + String(m.valueEpoch);
  j += ",\"unit\":";
  jsonString(j, cfg.unit);
  j += ",\"wifi\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
  j += ",\"rssi\":" + String((int)WiFi.RSSI());
  j += ",\"ip\":";
  jsonString(j, WiFi.getMode() == WIFI_MODE_AP ? WiFi.softAPIP().toString().c_str()
                                               : WiFi.localIP().toString().c_str());
  j += ",\"host\":";
  jsonString(j, cfg.hostname);
  j += ",\"mac\":";
  jsonString(j, WiFi.macAddress().c_str());
  j += ",\"mqttEnabled\":" + String(cfg.mqttEnabled ? "true" : "false");
  j += ",\"mqtt\":" + String(mqttIsConnected() ? "true" : "false");
  j += ",\"mqttError\":";
  jsonString(j, mqttLastError());
  j += ",\"heap\":" + String((uint32_t)ESP.getFreeHeap());
  j += ",\"psram\":" + String((uint32_t)ESP.getFreePsram());
  j += ",\"uptime\":" + String(millis() / 1000);
  j += ",\"sharpness\":" + String(r ? r->sharpness : 0.0f, 1);
  j += ",\"fw\":\"" FW_NAME " " FW_VERSION "\"";
  j += "}";
  sendJson(j);
}

static void handleConfigGet() {
  String j = "{";
  j += "\"rot\":" + String(cfg.rotationDeg, 1);
  j += ",\"dec\":" + String(cfg.decimalDigits);
  j += ",\"digits\":[";
  for (uint8_t i = 0; i < cfg.digitCount; i++) {
    if (i) j += ',';
    j += "{\"x\":" + String(cfg.digits[i].x) + ",\"y\":" + String(cfg.digits[i].y) +
         ",\"w\":" + String(cfg.digits[i].w) + ",\"h\":" + String(cfg.digits[i].h) + "}";
  }
  j += "],\"dials\":[";
  for (uint8_t i = 0; i < cfg.dialCount; i++) {
    if (i) j += ',';
    j += "{\"x\":" + String(cfg.dials[i].roi.x) + ",\"y\":" + String(cfg.dials[i].roi.y) +
         ",\"w\":" + String(cfg.dials[i].roi.w) + ",\"h\":" + String(cfg.dials[i].roi.h) +
         ",\"off\":" + String(cfg.dials[i].offsetDeg) + ",\"cw\":" + String(cfg.dials[i].clockwise) +
         "}";
  }
  j += "]";
  j += ",\"flash\":" + String(cfg.flashBrightness);
  j += ",\"bri\":" + String(cfg.brightness);
  j += ",\"con\":" + String(cfg.contrast);
  j += ",\"warm\":" + String(cfg.warmupFrames);
  j += ",\"vflip\":" + String(cfg.vflip ? "true" : "false");
  j += ",\"hmirror\":" + String(cfg.hmirror ? "true" : "false");
  j += ",\"ae\":" + String(cfg.autoExposure ? "true" : "false");
  j += ",\"exp\":" + String(cfg.manualExposure);
  j += ",\"gain\":" + String(cfg.agcGain);
  j += ",\"pol\":" + String(cfg.polarity);
  j += ",\"dir\":" + String(cfg.rollDirection);
  j += ",\"shift\":" + String(cfg.shiftSearch);
  j += ",\"minConf\":" + String(cfg.minConfidence);
  j += ",\"share\":" + String(cfg.sharedTemplates ? "true" : "false");
  j += ",\"autoLearn\":" + String(cfg.autoLearn ? "true" : "false");
  j += ",\"interval\":" + String(cfg.measureIntervalSec);
  j += ",\"maxRate\":" + String(cfg.maxRatePerMin, 4);
  j += ",\"allowDec\":" + String(cfg.allowDecrease ? "true" : "false");
  j += ",\"unit\":";
  jsonString(j, cfg.unit);
  j += ",\"mqttEnabled\":" + String(cfg.mqttEnabled ? "true" : "false");
  j += ",\"mqttHost\":";
  jsonString(j, cfg.mqttHost);
  j += ",\"mqttPort\":" + String(cfg.mqttPort);
  j += ",\"mqttUser\":";
  jsonString(j, cfg.mqttUser);
  j += ",\"mqttTopic\":";
  jsonString(j, cfg.mqttTopic);
  j += ",\"mqttDisc\":" + String(cfg.mqttDiscovery ? "true" : "false");
  j += ",\"ssid\":";
  jsonString(j, cfg.wifiSsid);
  j += "}";
  sendJson(j);
}

static void handleConfigPost() {
  // Geometrie
  if (server.hasArg("rot")) cfg.rotationDeg = clampT(argF("rot", 0), -45.0f, 45.0f);
  if (server.hasArg("nd")) cfg.digitCount = (uint8_t)clampT(argI("nd", 5), 0, MAX_DIGITS);
  if (server.hasArg("ndl")) cfg.dialCount = (uint8_t)clampT(argI("ndl", 0), 0, MAX_DIALS);
  if (server.hasArg("dec"))
    cfg.decimalDigits = (uint8_t)clampT(argI("dec", 0), 0, (int)cfg.digitCount);

  char key[8];
  for (uint8_t i = 0; i < MAX_DIGITS; i++) {
    snprintf(key, sizeof(key), "d%ux", (unsigned)i);
    if (!server.hasArg(key)) continue;
    cfg.digits[i].x = (int16_t)clampT(argI(key, 0), 0, FRAME_W - 1);
    snprintf(key, sizeof(key), "d%uy", (unsigned)i);
    cfg.digits[i].y = (int16_t)clampT(argI(key, 0), 0, FRAME_H - 1);
    snprintf(key, sizeof(key), "d%uw", (unsigned)i);
    cfg.digits[i].w = (int16_t)clampT(argI(key, 8), 4, FRAME_W);
    snprintf(key, sizeof(key), "d%uh", (unsigned)i);
    cfg.digits[i].h = (int16_t)clampT(argI(key, 8), 4, FRAME_H);
  }
  for (uint8_t i = 0; i < MAX_DIALS; i++) {
    snprintf(key, sizeof(key), "a%ux", (unsigned)i);
    if (!server.hasArg(key)) continue;
    cfg.dials[i].roi.x = (int16_t)clampT(argI(key, 0), 0, FRAME_W - 1);
    snprintf(key, sizeof(key), "a%uy", (unsigned)i);
    cfg.dials[i].roi.y = (int16_t)clampT(argI(key, 0), 0, FRAME_H - 1);
    snprintf(key, sizeof(key), "a%uw", (unsigned)i);
    cfg.dials[i].roi.w = (int16_t)clampT(argI(key, 16), 8, FRAME_W);
    snprintf(key, sizeof(key), "a%uh", (unsigned)i);
    cfg.dials[i].roi.h = (int16_t)clampT(argI(key, 16), 8, FRAME_H);
    snprintf(key, sizeof(key), "a%uo", (unsigned)i);
    cfg.dials[i].offsetDeg = (int16_t)clampT(argI(key, 0), -360, 360);
    snprintf(key, sizeof(key), "a%uc", (unsigned)i);
    cfg.dials[i].clockwise = argI(key, 1) >= 0 ? 1 : -1;
  }

  // Kamera
  bool camChanged = false;
  if (server.hasArg("flash")) {
    cfg.flashBrightness = (uint8_t)clampT(argI("flash", 40), 0, 255);
    camChanged = true;
  }
  if (server.hasArg("bri")) {
    cfg.brightness = (int8_t)clampT(argI("bri", 0), -2, 2);
    camChanged = true;
  }
  if (server.hasArg("con")) {
    cfg.contrast = (int8_t)clampT(argI("con", 0), -2, 2);
    camChanged = true;
  }
  if (server.hasArg("warm")) cfg.warmupFrames = (uint8_t)clampT(argI("warm", 3), 0, 10);
  if (server.hasArg("vflip")) {
    cfg.vflip = argI("vflip", 0) ? 1 : 0;
    camChanged = true;
  }
  if (server.hasArg("hmirror")) {
    cfg.hmirror = argI("hmirror", 0) ? 1 : 0;
    camChanged = true;
  }
  if (server.hasArg("ae")) {
    cfg.autoExposure = argI("ae", 1) ? 1 : 0;
    camChanged = true;
  }
  if (server.hasArg("exp")) {
    cfg.manualExposure = (uint16_t)clampT(argI("exp", 300), 0, 1200);
    camChanged = true;
  }
  if (server.hasArg("gain")) {
    cfg.agcGain = (uint8_t)clampT(argI("gain", 0), 0, 30);
    camChanged = true;
  }

  // Erkennung
  if (server.hasArg("pol")) cfg.polarity = (uint8_t)clampT(argI("pol", 0), 0, 2);
  if (server.hasArg("dir")) cfg.rollDirection = argI("dir", 1) >= 0 ? 1 : -1;
  if (server.hasArg("shift")) cfg.shiftSearch = (uint8_t)clampT(argI("shift", MAX_SHIFT), 1, MAX_SHIFT);
  if (server.hasArg("minConf")) cfg.minConfidence = (uint8_t)clampT(argI("minConf", 35), 0, 100);
  if (server.hasArg("share")) cfg.sharedTemplates = argI("share", 1) ? 1 : 0;
  if (server.hasArg("autoLearn")) cfg.autoLearn = argI("autoLearn", 1) ? 1 : 0;

  // Messung
  if (server.hasArg("interval"))
    cfg.measureIntervalSec = (uint32_t)clampT(argI("interval", 300), 10, 86400);
  if (server.hasArg("maxRate")) cfg.maxRatePerMin = argF("maxRate", 0.05f);
  if (server.hasArg("allowDec")) cfg.allowDecrease = argI("allowDec", 0) ? 1 : 0;
  argS("unit", cfg.unit, sizeof(cfg.unit));

  // MQTT
  bool mqttChanged = false;
  if (server.hasArg("mqttEnabled")) {
    cfg.mqttEnabled = argI("mqttEnabled", 0) ? 1 : 0;
    mqttChanged = true;
  }
  if (server.hasArg("mqttHost")) {
    argS("mqttHost", cfg.mqttHost, sizeof(cfg.mqttHost));
    mqttChanged = true;
  }
  if (server.hasArg("mqttPort")) {
    cfg.mqttPort = (uint16_t)clampT(argI("mqttPort", 1883), 1, 65535);
    mqttChanged = true;
  }
  if (server.hasArg("mqttUser")) {
    argS("mqttUser", cfg.mqttUser, sizeof(cfg.mqttUser));
    mqttChanged = true;
  }
  if (server.hasArg("mqttPass")) {
    argS("mqttPass", cfg.mqttPass, sizeof(cfg.mqttPass));
    mqttChanged = true;
  }
  if (server.hasArg("mqttTopic")) {
    argS("mqttTopic", cfg.mqttTopic, sizeof(cfg.mqttTopic));
    mqttChanged = true;
  }
  if (server.hasArg("mqttDisc")) {
    cfg.mqttDiscovery = argI("mqttDisc", 1) ? 1 : 0;
    mqttChanged = true;
  }

  // WLAN (wird erst beim Neustart uebernommen)
  argS("ssid", cfg.wifiSsid, sizeof(cfg.wifiSsid));
  argS("wifiPass", cfg.wifiPass, sizeof(cfg.wifiPass));
  argS("host", cfg.hostname, sizeof(cfg.hostname));

  if (cfg.decimalDigits > cfg.digitCount) cfg.decimalDigits = cfg.digitCount;

  bool ok = configSave();
  if (camChanged) cameraApplySettings();
  if (mqttChanged) mqttInvalidateDiscovery();

  sendJson(String("{\"ok\":") + (ok ? "true" : "false") + "}");
}

static void handleRecognize() {
  bool accepted = webRunMeasurement();
  Recognition *r = recognizeResult();
  if (!r) {
    sendJson("{\"ok\":false,\"error\":\"nicht initialisiert\",\"digits\":[],\"dials\":[]}", 500);
    return;
  }

  String j = "{\"ok\":" + String(r->valid ? "true" : "false");
  j += ",\"accepted\":" + String(accepted ? "true" : "false");
  j += ",\"raw\":" + String(r->rawValue, configDecimals());
  j += ",\"confidence\":" + String(r->confidence);
  j += ",\"sharpness\":" + String(r->sharpness, 1);
  j += ",\"ms\":" + String(r->durationMs);
  j += ",\"error\":";
  {
    const MeterStatus &m = meterStatus();
    jsonString(j, r->error[0] ? r->error : (accepted ? "" : m.lastRejectText));
  }
  j += ",\"digits\":[";
  for (uint8_t i = 0; i < r->digitCount; i++) {
    const DigitResult &d = r->digits[i];
    if (i) j += ',';
    j += "{\"d\":" + String(d.digit);
    j += ",\"conf\":" + String(d.confidence);
    j += ",\"score\":" + String(d.score, 3);
    j += ",\"pos\":" + String(d.position, 3);
    j += ",\"raw\":" + String(d.rawClass);
    j += ",\"rawScore\":" + String(d.rawScore, 3);
    j += ",\"off\":" + String(d.offset);
    j += ",\"dark\":" + String(d.darkOnLight ? "true" : "false");
    j += "}";
  }
  j += "],\"dials\":[";
  for (uint8_t i = 0; i < r->dialCount; i++) {
    const DialResult &d = r->dials[i];
    if (i) j += ',';
    j += "{\"d\":" + String(d.digit);
    j += ",\"conf\":" + String(d.confidence);
    j += ",\"angle\":" + String(d.angleDeg, 1);
    j += ",\"pos\":" + String(d.position, 2);
    j += "}";
  }
  j += "]}";
  sendJson(j);
}

static void handleLearn() {
  GrayImage img;
  if (!webCaptureFrame() || !webWorkingFrame(&img)) {
    sendJson("{\"ok\":false,\"message\":\"Aufnahme fehlgeschlagen\"}", 503);
    return;
  }
  String v = server.arg("value");
  v.replace(',', '.');
  double reading = v.toDouble();

  String report;
  bool ok = recognizeLearnFromReading(img, reading, report);
  if (ok) meterSetValue(reading);

  String j = "{\"ok\":" + String(ok ? "true" : "false") + ",\"message\":";
  jsonString(j, report.c_str());
  j += "}";
  sendJson(j);
}

static void handleSetValue() {
  String v = server.arg("value");
  v.replace(',', '.');
  meterSetValue(v.toDouble());
  sendJson("{\"ok\":true}");
}

static void handleClearValue() {
  meterClear();
  sendJson("{\"ok\":true}");
}

static void handleResetTemplates() {
  templatesResetAll();
  templatesSave();
  sendJson("{\"ok\":true}");
}

static void handleReboot() {
  sendJson("{\"ok\":true}");
  delay(300);
  ESP.restart();
}

static void handleValuePlain() {
  const MeterStatus &m = meterStatus();
  char buf[32];
  snprintf(buf, sizeof(buf), "%.*f", (int)configDecimals(), m.value);
  server.sendHeader("Cache-Control", "no-store");
  server.send(m.hasValue ? 200 : 503, "text/plain", m.hasValue ? buf : "no value");
}

static void handleMetrics() {
  const MeterStatus &m = meterStatus();
  String s;
  s += "# HELP watermeter_value Zaehlerstand\n# TYPE watermeter_value counter\n";
  s += "watermeter_value " + String(m.value, configDecimals()) + "\n";
  s += "# TYPE watermeter_confidence gauge\nwatermeter_confidence " + String(m.confidence) + "\n";
  s += "# TYPE watermeter_readings_total counter\nwatermeter_readings_total " + String(m.readings) +
       "\n";
  s += "# TYPE watermeter_rejected_total counter\nwatermeter_rejected_total " + String(m.rejected) +
       "\n";
  s += "# TYPE watermeter_rssi_dbm gauge\nwatermeter_rssi_dbm " + String((int)WiFi.RSSI()) + "\n";
  s += "# TYPE watermeter_uptime_seconds counter\nwatermeter_uptime_seconds " +
       String(millis() / 1000) + "\n";
  server.send(200, "text/plain; version=0.0.4", s);
}

static void handleOtaUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    Serial.printf("[ota] Start: %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("[ota] fertig: %u Bytes\n", up.totalSize);
    } else {
      Update.printError(Serial);
    }
  }
}

// ---------------------------------------------------------------------------

void webSetup() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/snapshot.jpg", HTTP_GET, handleSnapshot);
  server.on("/digit.jpg", HTTP_GET, handleDigitJpeg);
  server.on("/dial.jpg", HTTP_GET, handleDialJpeg);
  server.on("/template.jpg", HTTP_GET, handleTemplateJpeg);

  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.on("/api/recognize", HTTP_GET, handleRecognize);
  server.on("/api/templates", HTTP_GET, handleTemplatesJson);
  server.on("/api/learn", HTTP_POST, handleLearn);
  server.on("/api/setvalue", HTTP_POST, handleSetValue);
  server.on("/api/clearvalue", HTTP_POST, handleClearValue);
  server.on("/api/reset-templates", HTTP_POST, handleResetTemplates);
  server.on("/api/reboot", HTTP_POST, handleReboot);

  server.on(
      "/api/ota", HTTP_POST,
      []() {
        bool ok = !Update.hasError();
        server.sendHeader("Connection", "close");
        server.send(ok ? 200 : 500, "text/html; charset=utf-8",
                    ok ? "<meta charset=utf-8>Update erfolgreich, Neustart …"
                       : "<meta charset=utf-8>Update fehlgeschlagen");
        delay(500);
        if (ok) ESP.restart();
      },
      handleOtaUpload);

  server.on("/value", HTTP_GET, handleValuePlain);
  server.on("/metrics", HTTP_GET, handleMetrics);

  server.onNotFound([]() { server.send(404, "text/plain", "nicht gefunden"); });

  server.begin();
  Serial.println("[web] Server laeuft auf Port 80");
}

void webLoop() { server.handleClient(); }
