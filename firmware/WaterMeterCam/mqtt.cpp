#include "mqtt.h"

#include <WiFi.h>
#include <WiFiClient.h>

#include "config.h"
#include "meter.h"
#include "util.h"

static WiFiClient s_net;
static uint32_t s_lastAttempt = 0;
static uint32_t s_lastPing = 0;
static bool s_discoverySent = false;
static char s_error[64] = "";
static const uint16_t KEEPALIVE_S = 60;

static String deviceId() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[24];
  snprintf(buf, sizeof(buf), "wmc_%04x%08x", (uint16_t)(mac >> 32), (uint32_t)mac);
  return String(buf);
}

static String baseTopic() {
  String t = cfg.mqttTopic[0] ? String(cfg.mqttTopic) : String("wasserzaehler");
  while (t.endsWith("/")) t.remove(t.length() - 1);
  return t;
}

// --- Paketbau -------------------------------------------------------------

static void writeRemainingLength(uint8_t *buf, size_t &pos, uint32_t len) {
  do {
    uint8_t b = len % 128;
    len /= 128;
    if (len > 0) b |= 0x80;
    buf[pos++] = b;
  } while (len > 0);
}

static void writeString(uint8_t *buf, size_t &pos, const char *s) {
  uint16_t n = strlen(s);
  buf[pos++] = n >> 8;
  buf[pos++] = n & 0xFF;
  memcpy(buf + pos, s, n);
  pos += n;
}

static bool sendPacket(uint8_t type, const uint8_t *payload, size_t len) {
  uint8_t head[5];
  size_t hp = 0;
  head[hp++] = type;
  writeRemainingLength(head, hp, len);
  if (s_net.write(head, hp) != hp) return false;
  if (len && s_net.write(payload, len) != len) return false;
  return true;
}

// --- Verbindung -----------------------------------------------------------

static void disconnect(const char *why) {
  if (s_net.connected()) s_net.stop();
  s_discoverySent = false;
  if (why) {
    strncpy(s_error, why, sizeof(s_error) - 1);
    s_error[sizeof(s_error) - 1] = 0;
  }
}

static bool connectBroker() {
  if (!cfg.mqttEnabled || !cfg.mqttHost[0]) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  s_net.setTimeout(3);
  if (!s_net.connect(cfg.mqttHost, cfg.mqttPort ? cfg.mqttPort : 1883, 3000)) {
    disconnect("Broker nicht erreichbar");
    return false;
  }

  String clientId = deviceId();
  String willTopic = baseTopic() + "/status";
  const char *willMsg = "offline";

  uint8_t flags = 0x02;  // clean session
  flags |= 0x04;         // will flag
  flags |= 0x20;         // will retain
  // will QoS bleibt 0
  if (cfg.mqttUser[0]) flags |= 0x80;
  if (cfg.mqttUser[0] && cfg.mqttPass[0]) flags |= 0x40;

  size_t need = 10 + 2 + clientId.length() + 2 + willTopic.length() + 2 + strlen(willMsg) + 2 +
                strlen(cfg.mqttUser) + 2 + strlen(cfg.mqttPass) + 8;
  uint8_t *buf = (uint8_t *)malloc(need);
  if (!buf) {
    disconnect("kein Speicher");
    return false;
  }

  size_t p = 0;
  writeString(buf, p, "MQTT");
  buf[p++] = 4;  // Protokollversion 3.1.1
  buf[p++] = flags;
  buf[p++] = KEEPALIVE_S >> 8;
  buf[p++] = KEEPALIVE_S & 0xFF;
  writeString(buf, p, clientId.c_str());
  writeString(buf, p, willTopic.c_str());
  writeString(buf, p, willMsg);
  if (flags & 0x80) writeString(buf, p, cfg.mqttUser);
  if (flags & 0x40) writeString(buf, p, cfg.mqttPass);

  bool ok = sendPacket(0x10, buf, p);
  free(buf);
  if (!ok) {
    disconnect("CONNECT konnte nicht gesendet werden");
    return false;
  }

  // Auf CONNACK warten.
  uint32_t t0 = millis();
  while (s_net.available() < 4) {
    if (millis() - t0 > 3000) {
      disconnect("keine Antwort vom Broker");
      return false;
    }
    delay(10);
  }
  uint8_t resp[4];
  s_net.readBytes(resp, 4);
  if (resp[0] != 0x20 || resp[3] != 0x00) {
    char msg[48];
    snprintf(msg, sizeof(msg), "Broker lehnt ab (Code %u)", resp[3]);
    disconnect(msg);
    return false;
  }

  s_error[0] = 0;
  s_lastPing = millis();
  Serial.println("[mqtt] verbunden");

  mqttPublish((baseTopic() + "/status").c_str(), "online", true);
  if (cfg.mqttDiscovery && !s_discoverySent) {
    mqttPublishDiscovery();
    s_discoverySent = true;
  }
  return true;
}

bool mqttIsConnected() { return s_net.connected(); }
const char *mqttLastError() { return s_error; }
void mqttInvalidateDiscovery() { s_discoverySent = false; }

void mqttSetup() {
  s_lastAttempt = 0;
  s_discoverySent = false;
  s_error[0] = 0;
}

void mqttLoop() {
  if (!cfg.mqttEnabled) {
    if (s_net.connected()) disconnect(nullptr);
    return;
  }

  if (!s_net.connected()) {
    if (millis() - s_lastAttempt < 15000) return;
    s_lastAttempt = millis();
    connectBroker();
    return;
  }

  // Alles, was hereinkommt (PINGRESP, evtl. PUBACK), verwerfen.
  while (s_net.available()) s_net.read();

  if (millis() - s_lastPing > (uint32_t)KEEPALIVE_S * 500) {
    s_lastPing = millis();
    if (!sendPacket(0xC0, nullptr, 0)) disconnect("PINGREQ fehlgeschlagen");
  }
}

bool mqttPublish(const char *topic, const char *payload, bool retain) {
  if (!s_net.connected()) return false;
  size_t tlen = strlen(topic);
  size_t plen = strlen(payload);
  size_t need = 2 + tlen + plen;

  uint8_t *buf = (uint8_t *)malloc(need);
  if (!buf) return false;
  size_t p = 0;
  writeString(buf, p, topic);
  memcpy(buf + p, payload, plen);
  p += plen;

  bool ok = sendPacket(0x30 | (retain ? 0x01 : 0x00), buf, p);
  free(buf);
  if (!ok) disconnect("PUBLISH fehlgeschlagen");
  return ok;
}

// --- Nutzdaten ------------------------------------------------------------

void mqttPublishState() {
  if (!s_net.connected()) return;
  const MeterStatus &m = meterStatus();
  String base = baseTopic();
  char buf[96];

  if (m.hasValue) {
    snprintf(buf, sizeof(buf), "%.*f", (int)configDecimals(), m.value);
    mqttPublish((base + "/value").c_str(), buf, true);
  }
  snprintf(buf, sizeof(buf), "%.*f", (int)configDecimals(), m.lastRawValue);
  mqttPublish((base + "/raw").c_str(), buf, true);

  snprintf(buf, sizeof(buf), "%u", m.confidence);
  mqttPublish((base + "/confidence").c_str(), buf, true);

  mqttPublish((base + "/error").c_str(),
              m.lastReject == REJECT_NONE ? "" : m.lastRejectText, true);

  snprintf(buf, sizeof(buf), "%d", (int)WiFi.RSSI());
  mqttPublish((base + "/rssi").c_str(), buf, true);

  String json = "{";
  json += "\"value\":";
  snprintf(buf, sizeof(buf), "%.*f", (int)configDecimals(), m.value);
  json += m.hasValue ? buf : "null";
  json += ",\"raw\":";
  snprintf(buf, sizeof(buf), "%.*f", (int)configDecimals(), m.lastRawValue);
  json += buf;
  json += ",\"confidence\":" + String(m.confidence);
  json += ",\"readings\":" + String(m.readings);
  json += ",\"rejected\":" + String(m.rejected);
  json += ",\"error\":";
  jsonString(json, m.lastReject == REJECT_NONE ? "" : m.lastRejectText);
  json += "}";
  mqttPublish((base + "/json").c_str(), json.c_str(), true);
}

static void discoverySensor(const String &id, const char *name, const char *stateTopic,
                            const char *unit, const char *devClass, const char *stateClass,
                            const char *icon, const String &devBlock, const String &availTopic) {
  String topic = "homeassistant/sensor/" + id + "/config";
  String p = "{";
  p += "\"name\":";
  jsonString(p, name);
  p += ",\"uniq_id\":";
  jsonString(p, id.c_str());
  p += ",\"stat_t\":";
  jsonString(p, stateTopic);
  p += ",\"avty_t\":";
  jsonString(p, availTopic.c_str());
  if (unit && unit[0]) {
    p += ",\"unit_of_meas\":";
    jsonString(p, unit);
  }
  if (devClass && devClass[0]) {
    p += ",\"dev_cla\":";
    jsonString(p, devClass);
  }
  if (stateClass && stateClass[0]) {
    p += ",\"stat_cla\":";
    jsonString(p, stateClass);
  }
  if (icon && icon[0]) {
    p += ",\"ic\":";
    jsonString(p, icon);
  }
  p += ",\"dev\":" + devBlock;
  p += "}";
  mqttPublish(topic.c_str(), p.c_str(), true);
}

void mqttPublishDiscovery() {
  if (!s_net.connected()) return;
  String id = deviceId();
  String base = baseTopic();
  String avail = base + "/status";

  String dev = "{\"ids\":[";
  jsonString(dev, id.c_str());
  dev += "],\"name\":";
  jsonString(dev, cfg.hostname[0] ? cfg.hostname : "Wasserzaehler");
  dev += ",\"mdl\":\"ESP32-CAM\",\"mf\":\"" FW_NAME "\",\"sw\":\"" FW_VERSION "\"}";

  discoverySensor(id + "_value", "Zählerstand", (base + "/value").c_str(), cfg.unit, "water",
                  "total_increasing", "mdi:water", dev, avail);
  discoverySensor(id + "_raw", "Letzte Erkennung", (base + "/raw").c_str(), cfg.unit, "", "",
                  "mdi:eye", dev, avail);
  discoverySensor(id + "_conf", "Konfidenz", (base + "/confidence").c_str(), "%", "", "measurement",
                  "mdi:check-decagram", dev, avail);
  discoverySensor(id + "_err", "Fehler", (base + "/error").c_str(), "", "", "", "mdi:alert", dev,
                  avail);
  discoverySensor(id + "_rssi", "WLAN-Pegel", (base + "/rssi").c_str(), "dBm",
                  "signal_strength", "measurement", "", dev, avail);
  Serial.println("[mqtt] Discovery gesendet");
}
