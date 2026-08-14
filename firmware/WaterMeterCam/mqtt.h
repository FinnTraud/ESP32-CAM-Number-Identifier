// Minimaler MQTT-3.1.1-Client (nur Senden).
//
// Bewusst selbst geschrieben statt PubSubClient: so baut die Firmware in der
// Arduino-IDE ohne eine einzige nachzuinstallierende Bibliothek. Es wird nur
// gesendet (QoS 0) - der Zaehler muss nichts empfangen.
#pragma once

#include <Arduino.h>

void mqttSetup();
void mqttLoop();
bool mqttIsConnected();
const char *mqttLastError();

bool mqttPublish(const char *topic, const char *payload, bool retain);

// Veroeffentlicht Zaehlerstand, Konfidenz, Status usw.
void mqttPublishState();

// Home-Assistant-Autodiscovery (retained). Wird nach dem Verbinden einmal
// gesendet.
void mqttPublishDiscovery();

// Erzwingt beim naechsten Verbinden ein erneutes Senden der Discovery.
void mqttInvalidateDiscovery();
