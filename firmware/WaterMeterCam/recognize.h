// Ziffern- und Zeigererkennung.
//
// Grundidee: die Kamera haengt fest vor dem Zaehler, also erscheint jede Rolle
// immer an derselben Bildstelle. Damit reicht ein normierter Kreuzkorrelations-
// Abgleich (NCC) gegen zehn Vorlagen - es braucht kein neuronales Netz.
//
// Der eigentliche Trick steckt in der Kaskade: bei einem Rollenzaehler dreht
// sich eine Stelle genau um eine Ziffer weiter, waehrend die naechstniedrigere
// von 9 auf 0 laeuft. Die Position der unteren Stelle sagt also exakt voraus,
// wie weit die obere gedreht sein muss. Diese Zwangsbedingung loest die
// klassische "Ziffer steht zwischen 3 und 4"-Mehrdeutigkeit auf.
#pragma once

#include <Arduino.h>

#include "config.h"
#include "imgproc.h"

struct DigitResult {
  uint8_t patch[PATCH_PIX];  // normiert, Ziffer hell auf dunkel
  float scores[NUM_CLASSES][MAX_OFFSETS];

  int8_t rawClass;   // bester Treffer ohne Kaskadenbedingung
  int8_t rawOffset;
  float rawScore;

  int8_t digit;        // Ergebnis nach der Kaskade, -1 = unbestimmt
  int8_t offset;       // dabei benutzter Offset
  float score;         // NCC an dieser Stelle
  float position;      // kontinuierliche Rollenstellung 0..10
  uint8_t confidence;  // 0..100
  bool darkOnLight;    // erkannte Polaritaet
};

struct DialResult {
  float angleDeg;      // 0 = 12 Uhr, positiv im Uhrzeigersinn
  float position;      // kontinuierlich 0..10
  int8_t digit;
  uint8_t confidence;
  uint8_t profile[72];  // Dunkelheitsprofil in 5-Grad-Schritten, fuer die Web-UI
};

struct Recognition {
  bool valid;
  uint8_t digitCount;
  uint8_t dialCount;
  DigitResult digits[MAX_DIGITS];
  DialResult dials[MAX_DIALS];

  double rawValue;      // aus den erkannten Stellen zusammengesetzt
  uint8_t confidence;   // Minimum ueber alle Stellen
  float sharpness;      // Fokusmass des Gesamtbildes
  uint32_t durationMs;
  char error[80];
};

bool recognizeInit();

// Gibt den globalen Ergebnispuffer zurueck (wird bei jedem Lauf ueberschrieben).
Recognition *recognizeResult();

// Fuehrt eine komplette Erkennung auf einem Graustufenbild aus.
bool recognizeRun(const GrayImage &img);

// Extrahiert und normiert nur den Ausschnitt einer Stelle - fuer die Web-UI und
// das Anlernen.
void recognizeExtractDigitPatch(const GrayImage &img, uint8_t index, uint8_t *dst,
                                bool *darkOnLight);

// Sucht den Offset, bei dem das TPL_H hohe Fenster oben und unten in der
// Luecke zwischen zwei Ziffern liegt. Wird benutzt, wenn keine niedrigere
// Stelle die Drehung vorgibt (also fuer die letzte Stelle beim Anlernen).
// quality (optional) sagt, wie deutlich diese Luecke ist: < 0.3 heisst
// "die Rolle steht gerade zwischen zwei Ziffern".
int recognizeCenterOffset(const uint8_t *patch, float *quality);

// Kopiert das TPL_H hohe Fenster bei einem Offset aus dem Ausschnitt heraus.
void recognizeWindow(const uint8_t *patch, int offset, uint8_t *dst);

// Rendert einen Zeiger-Ausschnitt fuer die Web-UI: normierter Bildausschnitt
// mit eingezeichnetem erkanntem Winkel. dst muss n*n Bytes gross sein.
void recognizeRenderDial(const GrayImage &img, uint8_t index, uint8_t *dst, int n);

// Lernt aus dem aktuellen Bild anhand eines vom Benutzer eingetippten Zaehler-
// standes. reading ist der Gesamtwert (inkl. Nachkommastellen).
// report bekommt eine kurze Klartextmeldung.
bool recognizeLearnFromReading(const GrayImage &img, double reading, String &report);
