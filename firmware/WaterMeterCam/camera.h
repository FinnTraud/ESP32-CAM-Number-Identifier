// Kameraansteuerung: Initialisierung, Beleuchtung, Bildaufnahme.
#pragma once

#include <Arduino.h>
#include <esp_camera.h>

bool cameraInit();

// Uebertraegt die Werte aus cfg an den Sensor. Nach jeder Aenderung der
// Kameraeinstellungen aufrufen.
void cameraApplySettings();

void flashSet(uint8_t duty);  // 0 = aus
bool cameraHasFlash();

// Nimmt ein Graustufenbild auf: LED an, warmupFrames verwerfen, Bild holen,
// LED aus. Der Rueckgabepuffer muss mit cameraRelease() freigegeben werden.
// Liefert nullptr bei Fehler.
camera_fb_t *cameraGrab();
void cameraRelease(camera_fb_t *fb);
