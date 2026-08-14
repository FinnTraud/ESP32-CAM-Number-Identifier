# Fehlersuche

Der serielle Monitor (115200 Baud) ist die erste Anlaufstelle – dort steht bei
jeder Messung eine Zeile mit Wert, Rohwert, Konfidenz und Ablehnungsgrund.

---

## Beim Flashen

**„A fatal error occurred: Failed to connect to ESP32“**
IO0 ist nicht auf GND, oder der Reset kam zum falschen Zeitpunkt. Ablauf: IO0
auf GND → Reset → Upload starten → nach dem Upload Brücke ab → Reset.

**„Sketch too big“**
Falsches Partitionsschema. Es muss **Minimal SPIFFS (1.9MB APP with OTA/190KB
SPIFFS)** sein.

**„Brownout detector was triggered“ im Dauerlauf**
Netzteil zu schwach oder Kabel zu dünn. 5 V mit mindestens 1 A, kurze Leitung.
USB-Ports am PC reichen oft nicht.

**`[fs] LittleFS konnte nicht eingebunden werden!`**
Ebenfalls das Partitionsschema. Ohne SPIFFS-Bereich gibt es kein Dateisystem,
und weder Konfiguration noch Vorlagen überleben einen Neustart.

**`[cam] init fehlgeschlagen: 0x105`**
Kamera nicht erkannt: Flachbandkabel sitzt nicht richtig, oder das falsche
`CAMERA_MODEL_*` ist oben in `WaterMeterCam.ino` gesetzt.

**`[cam] WARNUNG: kein PSRAM gefunden`**
Das Board hat kein PSRAM (oder es ist im Boardmenü abgeschaltet). Die Firmware
fällt auf QVGA zurück und läuft, aber die Erkennung wird deutlich schlechter.
Im Boardmenü **PSRAM: Enabled** setzen; hilft das nicht, hat das Board keins.

---

## Erkennung

### Alle Ziffern falsch, Konfidenz durchgängig niedrig

Fast immer der Ausschnitt. *Ansicht → Jetzt erkennen* und die Ausschnittsbilder
ansehen:

| Was du siehst | Ursache |
|---|---|
| Verschwommener Brei | Objektiv nicht auf Nahdistanz gestellt |
| Zwei halbe Ziffern übereinander, immer | Rechteck zu hoch – es umfasst mehr als eine Ziffernteilung |
| Ziffer randvoll, kein Rand oben/unten | Rechteck zu eng – der Trommelstreifen fehlt |
| Heller Fleck über der Ziffer | Reflexion, LED-Helligkeit runter oder indirekt beleuchten |
| Ziffer seitlich angeschnitten | Rechteck horizontal daneben |

### Einzelne Stelle springt, die anderen sind stabil

Meistens die **niederwertigste** Stelle, und dann ist es kein Fehler: sie dreht
sich ständig und steht oft zwischen zwei Ziffern. Wenn es eine höhere Stelle
ist, stimmt dort der Ausschnitt nicht.

### Konfidenz bleibt niedrig, obwohl die Ziffern richtig sind

Die Vorlagensammlung ist noch nicht vollständig. *Einrichtung → Vorlagen*
zeigt, welche der zehn Ziffern schon aus echten Bildern stammen (grün, ohne
Sternchen). Bis 10 von 10 erreicht sind, weiter anlernen.

### „Konfidenz zu niedrig“ bei jeder Messung

* Vorlagen noch nicht angelernt → anlernen.
* Beleuchtung schwankt stark → Automatikbelichtung abschalten und feste
  Belichtung einstellen.
* Schwelle zu hoch → *Einstellungen → Erkennung → Mindestkonfidenz*. Aber erst
  die Ausschnitte prüfen; die Schwelle zu senken macht falsche Werte gültig.

### „Wert kleiner als zuvor“

Die Erkennung hat sich verlesen, und die Plausibilitätsprüfung hat es abgefangen
– das System arbeitet wie gedacht. Bleibt es dauerhaft so, steht der gespeicherte
Stand zu hoch: *System → Zählerstand korrigieren*, oder *Verlauf vergessen*,
damit die nächste Messung bedingungslos angenommen wird.

### „Sprung zu groß“

Entweder eine Fehlerkennung, oder `max. Zuwachs je Minute` ist zu knapp
eingestellt. Bei Gartenbewässerung oder einem Poolfüllen den Wert hochsetzen.

### Der Zähler steht auf 0 und wird als 9 gelesen

Das sollte behoben sein (siehe die Toleranzen am Nulldurchgang in
[ALGORITHMUS.md](ALGORITHMUS.md)). Tritt es trotzdem auf: der Nullpunkt-Winkel
des untersten Zeigers stimmt nicht. In der Einrichtung so einstellen, dass die
Zeigerstellung zur abgelesenen Ziffer passt.

### Ein Zeiger wird konstant um eine Ziffer falsch gelesen

Nullpunkt-Winkel oder Drehrichtung dieses Zifferblatts falsch. Manche Zähler
haben abwechselnd im und gegen den Uhrzeigersinn laufende Zeiger.

### Nach einem Stoß / nach dem Putzen ist alles falsch

Die Kamera ist verrutscht. Ausschnitte neu ziehen. Wenn sich der Bildausschnitt
stark geändert hat, zusätzlich *System → Vorlagen zurücksetzen* und neu anlernen.

---

## Netzwerk

**Gerät nicht unter `wasserzaehler.local` erreichbar**
mDNS funktioniert nicht in jedem Netz (und nicht über VLAN-Grenzen). Die IP steht
im seriellen Monitor und in der Router-Oberfläche. Am besten im Router eine feste
IP-Zuordnung vergeben.

**Gerät hängt dauerhaft im eigenen Accesspoint**
SSID oder Passwort falsch, oder der Router funkt nur auf 5 GHz – der ESP32 kann
nur 2,4 GHz. Nach fünf Minuten startet das Gerät automatisch neu und versucht es
erneut.

**MQTT verbindet nicht**
Der Fehler steht in der Weboberfläche unter *System → MQTT-Fehler*:

| Meldung | Bedeutung |
|---|---|
| `Broker nicht erreichbar` | Adresse/Port falsch, oder Firewall |
| `Broker lehnt ab (Code 4)` | Benutzer oder Passwort falsch |
| `Broker lehnt ab (Code 5)` | nicht autorisiert – ACLs im Broker prüfen |
| `keine Antwort vom Broker` | Es antwortet etwas, aber kein MQTT (falscher Port?) |

**Home Assistant zeigt das Gerät nicht**
Discovery war beim Verbinden aus. Discovery anhaken, speichern – die Firmware
sendet sie beim nächsten Verbinden erneut. Notfalls *System → Neustart*.

---

## Weboberfläche

**Bild lädt nicht / bricht ab**
Jeder Bildabruf löst eine echte Aufnahme aus. Wenn parallel eine Messung läuft,
kann das kollidieren – einfach nochmal *Bild neu*.

**Einstellungen sind nach dem Neustart weg**
LittleFS fehlt (Partitionsschema) oder die Konfiguration hat einen CRC-Fehler.
Im seriellen Monitor steht `[cfg] CRC-Fehler` bzw. `[fs] LittleFS ...`.

**OTA-Update schlägt fehl**
Es muss die `.bin` aus *Sketch → Kompilierte Binärdatei exportieren* sein
(`WaterMeterCam.ino.bin`), nicht die `.bootloader.bin` oder `.partitions.bin`.

---

## Wenn gar nichts hilft

`tools/hostcheck/run.sh` prüft die Erkennungslogik auf dem PC gegen
synthetische Bilder. Schlägt der Testlauf fehl, liegt es an der Firmware; läuft
er durch, liegt es an Bild, Ausschnitten oder Beleuchtung.
