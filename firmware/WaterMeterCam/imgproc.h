// Bildverarbeitung auf 8-Bit-Graustufen. Bewusst ohne Fremdbibliothek, damit
// die Firmware in der Arduino-IDE ohne Zusatzinstallation baut.
#pragma once

#include <Arduino.h>

#include "config.h"

struct GrayImage {
  const uint8_t *data;
  int w;
  int h;
};

// Bilineare Abtastung mit Randklemmung.
float sampleBilinear(const GrayImage &img, float x, float y);

// Schneidet das Rechteck (rx, ry, rw, rh) aus dem *rotationskorrigierten* Bild
// aus und skaliert es auf dw x dh. Die Rotation wird um die Bildmitte
// gerechnet, damit ROIs und Vorschau dieselbe Geometrie sehen.
void extractRoi(const GrayImage &src, float rx, float ry, float rw, float rh, float rotDeg,
                uint8_t *dst, int dw, int dh);

// Erzeugt das rotationskorrigierte Vollbild (dst muss src.w * src.h gross sein).
void rotateFrame(const GrayImage &src, uint8_t *dst, float rotDeg);

// Spreizt den Kontrast auf 0..255 anhand des 2. und 98. Perzentils. Robuster
// als min/max, weil einzelne Glanzpunkte den Bereich sonst zerstoeren.
void normalizeContrast(uint8_t *buf, int n);

// Schaetzt die Polaritaet aus dem Rand des Ausschnitts: der Rand zeigt fast
// immer die Trommelflaeche, nicht die Ziffer. true = dunkle Ziffer auf hell.
bool isDarkOnLight(const uint8_t *buf, int w, int h);

void invertBuf(uint8_t *buf, int n);

// Otsu-Schwelle (nur fuer die Diagnoseansicht der Web-UI).
uint8_t otsuThreshold(const uint8_t *buf, int n);

// Mittelwert und Standardabweichung.
void meanStd(const uint8_t *buf, int n, float *mean, float *std);

// Schaerfemass (Varianz des Laplace-Operators) - dient in der Web-UI als
// Fokushilfe beim Einstellen des Objektivs.
float sharpness(const GrayImage &img);
