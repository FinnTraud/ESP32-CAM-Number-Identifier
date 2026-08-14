// Zaehlerlogik: Plausibilitaetspruefung und Speicherung des letzten Standes.
//
// Eine Einzelmessung darf falsch sein - der Zaehlerstand darf es nicht. Deshalb
// wird jede Erkennung gegen den letzten gueltigen Wert geprueft: ein Wasser-
// zaehler laeuft nur vorwaerts und nur mit begrenzter Rate.
#pragma once

#include <Arduino.h>

#include "recognize.h"

enum RejectReason : uint8_t {
  REJECT_NONE = 0,
  REJECT_RECOGNITION,   // Erkennung selbst fehlgeschlagen
  REJECT_CONFIDENCE,    // zu unsicher
  REJECT_DECREASE,      // Zaehler waere rueckwaerts gelaufen
  REJECT_RATE,          // Sprung zu gross fuer die verstrichene Zeit
};

struct MeterStatus {
  bool hasValue;
  double value;          // letzter *akzeptierter* Stand
  double lastRawValue;   // letzte Erkennung, auch wenn verworfen
  uint8_t confidence;
  uint32_t valueEpoch;   // Unixzeit der letzten Aenderung (0 = unbekannt)
  uint32_t valueMillis;  // millis() der letzten Akzeptanz
  uint32_t readings;
  uint32_t rejected;
  RejectReason lastReject;
  char lastRejectText[64];
  float lastRatePerMin;
};

void meterInit();
const MeterStatus &meterStatus();

// Verarbeitet ein Erkennungsergebnis. Liefert true, wenn der Wert akzeptiert
// wurde. rec darf nullptr sein (Erkennung fehlgeschlagen).
bool meterProcess(const Recognition *rec);

// Setzt den Stand von Hand (Web-UI, Erstinbetriebnahme, nach Zaehlertausch).
void meterSetValue(double value);

// Vergisst den gespeicherten Stand, damit die naechste Messung wieder
// bedingungslos angenommen wird.
void meterClear();

const char *rejectReasonText(RejectReason r);
