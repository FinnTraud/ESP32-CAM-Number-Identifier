#include "meter.h"

#include <LittleFS.h>
#include <time.h>

#include "config.h"
#include "util.h"

static const char *STATE_PATH = "/state.bin";
static const uint32_t STATE_MAGIC = 0x53544131UL;  // "STA1"

struct StateBlob {
  uint32_t magic;
  double value;
  uint32_t epoch;
  uint32_t readings;
  uint32_t rejected;
  uint8_t hasValue;
  uint8_t pad[3];
  uint32_t crc;
};

static MeterStatus s_status;

const char *rejectReasonText(RejectReason r) {
  switch (r) {
    case REJECT_NONE: return "ok";
    case REJECT_RECOGNITION: return "Erkennung fehlgeschlagen";
    case REJECT_CONFIDENCE: return "Konfidenz zu niedrig";
    case REJECT_DECREASE: return "Wert kleiner als zuvor";
    case REJECT_RATE: return "Sprung zu gross";
  }
  return "?";
}

static uint32_t nowEpoch() {
  time_t t = time(nullptr);
  // Vor der ersten NTP-Synchronisation liefert time() Werte weit in der
  // Vergangenheit - die sind als Zeitstempel wertlos.
  return (t > 1600000000) ? (uint32_t)t : 0;
}

static void stateSave() {
  StateBlob b;
  memset(&b, 0, sizeof(b));
  b.magic = STATE_MAGIC;
  b.value = s_status.value;
  b.epoch = s_status.valueEpoch;
  b.readings = s_status.readings;
  b.rejected = s_status.rejected;
  b.hasValue = s_status.hasValue ? 1 : 0;
  b.crc = crc32((const uint8_t *)&b, sizeof(b) - sizeof(uint32_t));

  File f = LittleFS.open("/state.tmp", "w");
  if (!f) return;
  bool ok = f.write((const uint8_t *)&b, sizeof(b)) == sizeof(b);
  f.close();
  if (!ok) {
    LittleFS.remove("/state.tmp");
    return;
  }
  LittleFS.remove(STATE_PATH);
  LittleFS.rename("/state.tmp", STATE_PATH);
}

static void stateLoad() {
  File f = LittleFS.open(STATE_PATH, "r");
  if (!f) return;
  StateBlob b;
  if (f.read((uint8_t *)&b, sizeof(b)) != sizeof(b)) {
    f.close();
    return;
  }
  f.close();
  if (b.magic != STATE_MAGIC) return;
  if (crc32((const uint8_t *)&b, sizeof(b) - sizeof(uint32_t)) != b.crc) {
    Serial.println("[meter] Zustand hat CRC-Fehler - verworfen");
    return;
  }
  s_status.value = b.value;
  s_status.valueEpoch = b.epoch;
  s_status.readings = b.readings;
  s_status.rejected = b.rejected;
  s_status.hasValue = b.hasValue != 0;
  Serial.printf("[meter] letzter Stand: %.4f\n", s_status.value);
}

void meterInit() {
  memset(&s_status, 0, sizeof(s_status));
  strcpy(s_status.lastRejectText, "-");
  stateLoad();
  // Nach einem Neustart ist unbekannt, wie viel Zeit vergangen ist. Die
  // Ratenpruefung wird deshalb bei der ersten Messung ausgesetzt.
  s_status.valueMillis = 0;
}

const MeterStatus &meterStatus() { return s_status; }

void meterSetValue(double value) {
  s_status.value = value;
  s_status.hasValue = true;
  s_status.valueEpoch = nowEpoch();
  s_status.valueMillis = millis();
  s_status.lastReject = REJECT_NONE;
  strcpy(s_status.lastRejectText, "von Hand gesetzt");
  stateSave();
}

void meterClear() {
  s_status.hasValue = false;
  s_status.value = 0;
  s_status.valueEpoch = 0;
  s_status.valueMillis = 0;
  s_status.lastReject = REJECT_NONE;
  strcpy(s_status.lastRejectText, "zurueckgesetzt");
  stateSave();
}

static void reject(RejectReason r, const char *detail) {
  s_status.rejected++;
  s_status.lastReject = r;
  snprintf(s_status.lastRejectText, sizeof(s_status.lastRejectText), "%s%s%s", rejectReasonText(r),
           detail && detail[0] ? ": " : "", detail ? detail : "");
}

bool meterProcess(const Recognition *rec) {
  s_status.readings++;

  if (!rec || !rec->valid) {
    reject(REJECT_RECOGNITION, rec ? rec->error : "kein Ergebnis");
    return false;
  }

  s_status.lastRawValue = rec->rawValue;
  s_status.confidence = rec->confidence;

  if (rec->confidence < cfg.minConfidence) {
    char d[32];
    snprintf(d, sizeof(d), "%u < %u", rec->confidence, cfg.minConfidence);
    reject(REJECT_CONFIDENCE, d);
    return false;
  }

  if (!s_status.hasValue) {
    // Erster Wert ueberhaupt: es gibt nichts, wogegen man pruefen koennte.
    s_status.value = rec->rawValue;
    s_status.hasValue = true;
    s_status.valueEpoch = nowEpoch();
    s_status.valueMillis = millis();
    s_status.lastReject = REJECT_NONE;
    strcpy(s_status.lastRejectText, "erster Wert");
    stateSave();
    return true;
  }

  double delta = rec->rawValue - s_status.value;

  if (delta < 0 && !cfg.allowDecrease) {
    char d[48];
    snprintf(d, sizeof(d), "%.4f -> %.4f", s_status.value, rec->rawValue);
    reject(REJECT_DECREASE, d);
    return false;
  }

  if (s_status.valueMillis != 0) {
    uint32_t elapsed = millis() - s_status.valueMillis;
    float minutes = elapsed / 60000.0f;
    if (minutes < 0.05f) minutes = 0.05f;  // sehr kurze Abstaende nicht ueberbewerten
    float rate = (float)fabs(delta) / minutes;
    s_status.lastRatePerMin = rate;
    if (cfg.maxRatePerMin > 0 && rate > cfg.maxRatePerMin) {
      char d[64];
      snprintf(d, sizeof(d), "%.4f in %.1f min", delta, minutes);
      reject(REJECT_RATE, d);
      return false;
    }
  }

  bool changed = fabs(delta) > 1e-9;
  s_status.value = rec->rawValue;
  s_status.valueMillis = millis();
  s_status.lastReject = REJECT_NONE;
  strcpy(s_status.lastRejectText, "-");
  if (changed) {
    s_status.valueEpoch = nowEpoch();
    stateSave();  // nur bei echter Aenderung schreiben - schont den Flash
  }
  return true;
}
