// HTTP-Oberflaeche und JSON-Schnittstelle.
#pragma once

#include <Arduino.h>

#include "imgproc.h"

void webSetup();
void webLoop();

// Nimmt ein Bild auf und legt es im Arbeitspuffer ab (Rohbild, ohne Drehung).
// Alle Endpunkte und die periodische Messung benutzen diesen Puffer.
bool webCaptureFrame();
bool webWorkingFrame(GrayImage *out);

// Fuehrt eine Messung durch (Aufnahme, Erkennung, Plausibilitaet, MQTT).
bool webRunMeasurement();
