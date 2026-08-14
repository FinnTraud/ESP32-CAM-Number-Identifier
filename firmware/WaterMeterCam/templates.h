// Vorlagensammlung ("Bank") fuer den Ziffernabgleich.
//
// Es gibt MAX_DIGITS Banken (eine je Stelle) plus eine gemeinsame Bank. Welche
// benutzt wird, entscheidet cfg.sharedTemplates. Die gemeinsame Bank ist der
// Normalfall: alle Rollen eines Zaehlers tragen dieselbe Schrift, und die
// niederwertigste Stelle laeuft im Betrieb durch alle zehn Ziffern - damit ist
// die Sammlung nach kurzer Zeit von allein vollstaendig.
#pragma once

#include <Arduino.h>

#include "config.h"

#define TPL_BANKS (MAX_DIGITS + 1)
#define TPL_SHARED_BANK MAX_DIGITS

// Ab so vielen Beispielen wird nur noch langsam nachgefuehrt, damit ein
// einzelner Fehlgriff die Vorlage nicht kaputt macht.
#define TPL_MAX_WEIGHT 16

bool templatesInit();

// Welche Bank fuer eine Stelle zustaendig ist.
uint8_t templateBankFor(uint8_t digitIndex);

const uint8_t *templateData(uint8_t bank, uint8_t cls);
uint16_t templateWeight(uint8_t bank, uint8_t cls);

// Uebernimmt einen Ausschnitt (TPL_PIX Bytes, bereits normiert) als Beispiel
// fuer die Klasse cls. Beim ersten Mal ersetzt er die eingebaute Vorlage
// vollstaendig, danach wird gleitend gemittelt.
void templateLearn(uint8_t bank, uint8_t cls, const uint8_t *patch);

// Setzt eine einzelne Klasse bzw. alles auf die eingebauten Vorlagen zurueck.
void templateResetClass(uint8_t bank, uint8_t cls);
void templatesResetAll();

bool templatesSave();
bool templatesLoad();

// Wie viele der zehn Ziffern einer Bank schon aus echten Bildern stammen.
uint8_t templatesLearnedCount(uint8_t bank);
