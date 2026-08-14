// Auswahl des Kameramodells.
//
// **Das gehoert hierher und nicht in die .ino.** Die Arduino-IDE uebersetzt
// jede .cpp-Datei einzeln; ein #define in der .ino ist fuer camera.cpp
// unsichtbar, und camera_pins.h wuerde dann mit #error abbrechen.
#pragma once

#define CAMERA_MODEL_AI_THINKER
// #define CAMERA_MODEL_ESP32S3_EYE
// #define CAMERA_MODEL_XIAO_ESP32S3
// #define CAMERA_MODEL_M5STACK_WIDE
