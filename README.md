# WaterMeterCam

Zählerstandserkennung für mechanische Wasserzähler mit einer ESP32-CAM.
Die Erkennung läuft **komplett auf dem Gerät** – kein Server, keine Cloud,
keine nachzuinstallierende Arduino-Bibliothek. Eingerichtet wird alles über
eine eingebaute Weboberfläche.

```
ESP32-CAM  ──►  Bild  ──►  Ziffern + Zeiger  ──►  MQTT / REST / Prometheus
                          (auf dem Gerät)
```

---

## Erst lesen: welche Lösung passt dir?

Für genau diese Aufgabe gibt es ein sehr ausgereiftes Projekt, und es wäre
unredlich, es hier zu verschweigen:

**[jomjol/AI-on-the-edge-device](https://github.com/jomjol/AI-on-the-edge-device)**
ist der De-facto-Standard. Es benutzt ein neuronales Netz (CNN) auf demselben
ESP32-CAM-Board, hat eine große Community, fertige 3D-Gehäuse und ist über
Home Assistant sofort einsatzbereit.

| | **AI-on-the-edge-device** | **WaterMeterCam** (dieses Repo) |
|---|---|---|
| Verfahren | Neuronales Netz (TFLite) | Normierter Vorlagenabgleich (NCC) |
| Bauen | ESP-IDF / PlatformIO, meist fertiges Binary flashen | **Arduino IDE, Sketch öffnen und hochladen** |
| Abhängigkeiten | ESP-IDF-Toolchain | keine – nur der ESP32-Arduino-Core |
| SD-Karte | erforderlich | nicht nötig |
| Anlernen | keins nötig (Netz ist vortrainiert) | einmal den Zählerstand eintippen |
| Quelltext | groß, C++/Python/TFLite | ~2500 Zeilen, in einem Nachmittag lesbar |
| Genauigkeit fremder Zähler | sehr gut ab Werk | erst nach dem Anlernen gut |

**Nimm AI-on-the-edge-device**, wenn du einfach nur willst, dass es läuft.

**Nimm dieses Repo**, wenn du – wie gefragt – eine *in der Arduino-IDE
kompilierbare* Variante brauchst, den Quelltext verstehen und anpassen willst,
oder wenn dir das Verfahren ohne KI lieber ist, weil es nachvollziehbar ist und
sich für genau deinen Zähler anlernen lässt.

---

## Warum Vorlagenabgleich statt KI

Die Kamera hängt fest vor dem Zähler. Jede Rolle erscheint deshalb **immer an
derselben Bildstelle**, in **derselben Schrift**, in **derselben Größe**. Das
ist ein viel einfacheres Problem als allgemeine Zeichenerkennung – und für den
Sonderfall reicht ein Verfahren, das man auf einer Serviette herleiten kann.

Der eigentliche Trick steckt nicht im Vergleich einzelner Ziffern, sondern in
der **Kaskade**. Bei einem Rollenzählwerk dreht sich eine Stelle genau um eine
Ziffer weiter, während die nächstniedrigere von 9 auf 0 läuft:

```
x_i  =  D_i  +  x_(i+1) / 10
```

Steht die 1000er-Stelle auf 4, dann *muss* die 10000er-Rolle 40 % ihres Wegs
zur nächsten Ziffer zurückgelegt haben. Diese Zwangsbedingung löst die
klassische „steht zwischen 3 und 4“-Mehrdeutigkeit auf, die bei Rollenzählern
sonst die häufigste Fehlerquelle ist. Details: [docs/ALGORITHMUS.md](docs/ALGORITHMUS.md).

---

## Was das Gerät kann

* Schwarze Ziffernrollen **und** rote Zeiger-Zifferblätter (0,1 / 0,01 / 0,001 m³)
* Weboberfläche zum Ziehen der Ausschnitte, Anlernen und Einstellen
* Korrektur einer schief montierten Kamera (Bilddrehung im Bild, nicht am Halter)
* Plausibilitätsprüfung: rückwärts laufende oder zu große Sprünge werden verworfen
* MQTT inklusive **Home-Assistant-Autodiscovery** und Last-Will
* REST (`/value`, `/api/status`), **Prometheus** (`/metrics`)
* Firmware-Update über die Weboberfläche (OTA)
* Fällt auf einen eigenen WLAN-Accesspoint zurück, wenn der Router nicht da ist

---

## Hardware

| Teil | Hinweis |
|---|---|
| ESP32-CAM (AI Thinker) | **mit PSRAM** – ohne geht nur QVGA und die Erkennung wird schlecht |
| FTDI-Adapter 5 V/3,3 V | zum Flashen, oder ein ESP32-CAM-MB-Programmieradapter |
| Netzteil 5 V / ≥ 1 A | die Kamera zieht Spitzen; USB am PC reicht im Dauerbetrieb oft nicht |
| Gehäuse | z. B. [jomjols Halterung auf Thingiverse](https://www.thingiverse.com/thing:3860911) |

**Das Objektiv muss auf Nahdistanz gestellt werden.** Ab Werk ist die OV2640
auf ~1 m fokussiert; der Zähler ist 5–10 cm entfernt. Die Linse lässt sich (oft
gegen einen kleinen Klebepunkt) herausdrehen. Die Weboberfläche zeigt unter
*System → Bildschärfe* einen Zahlenwert – beim Drehen einfach maximieren.

Mehr dazu: [docs/HARDWARE.md](docs/HARDWARE.md).

---

## Schnellstart

### 1. Arduino IDE vorbereiten

* **Boardverwalter-URL** hinzufügen (Datei → Einstellungen):
  `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
* Boardverwalter → **esp32 by Espressif Systems** installieren (Version 2.x oder 3.x)
* Board: **AI Thinker ESP32-CAM**
* **Partition Scheme: `Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)`**
  – wichtig, sonst passt die Firmware nicht bzw. es gibt kein Dateisystem
* **PSRAM: Enabled**

Es sind **keine** zusätzlichen Bibliotheken zu installieren.

### 2. Hochladen

`firmware/WaterMeterCam/WaterMeterCam.ino` öffnen und hochladen.
(Beim AI-Thinker-Board zum Flashen `IO0` auf `GND` brücken und einmal Reset drücken.)

Anderes Board? Das Kameramodell steht in **`board_config.h`** – nicht in der
`.ino`, weil die Arduino-IDE jede `.cpp` einzeln übersetzt und ein `#define` im
Sketch dort nicht ankommt.

### 3. Verbinden

Beim ersten Start öffnet das Gerät einen eigenen WLAN-Accesspoint:

```
SSID:     WaterMeterCam-XXXX
Passwort: wasserzaehler
Adresse:  http://192.168.4.1/
```

Dort unter *Einstellungen → WLAN* das Heimnetz eintragen und neu starten.
Danach ist das Gerät unter `http://wasserzaehler.local/` erreichbar.

### 4. Einrichten

1. **Einrichtung** öffnen, *Bild neu* drücken.
2. Anzahl der schwarzen Rollen und roten Zeiger eintragen.
3. Die Rechtecke auf die Ziffern ziehen – ein Rechteck = **genau eine Ziffer**,
   oben und unten ein schmaler Streifen Trommelfläche mit im Bild.
   *Rollen gleichmäßig verteilen* nimmt dir die Fleißarbeit ab.
4. *Geometrie speichern*.
5. Den am Zähler abgelesenen Stand eintippen und **Anlernen** drücken.
6. Nach ein paar Tagen (oder ein paar Anlernvorgängen mit unterschiedlichen
   Endziffern) ist die Vorlagensammlung vollständig – die Anzeige *„x von 10 aus
   eigenen Bildern“* zeigt den Fortschritt.

Schritt für Schritt mit Bildern: [docs/EINRICHTUNG.md](docs/EINRICHTUNG.md).

> **Vor dem Anlernen ist die Erkennung absichtlich zurückhaltend.** Die
> eingebauten Vorlagen stammen aus einer Standardschrift, nicht von deinem
> Zähler; die Konfidenz bleibt niedrig und die Werte werden verworfen. Das ist
> gewollt – lieber kein Wert als ein falscher.

---

## Anbindung

### MQTT / Home Assistant

Unter *Einstellungen → MQTT* Broker eintragen und Discovery aktiviert lassen.
Home Assistant legt dann von allein ein Gerät mit fünf Entitäten an
(Zählerstand, letzte Erkennung, Konfidenz, Fehler, WLAN-Pegel).

Topics unterhalb des Basis-Topics (Standard `wasserzaehler`):

| Topic | Inhalt |
|---|---|
| `.../status` | `online` / `offline` (Last Will, retained) |
| `.../value` | akzeptierter Zählerstand |
| `.../raw` | letzte Erkennung, auch wenn verworfen |
| `.../confidence` | 0–100 |
| `.../error` | leer oder Grund der Ablehnung |
| `.../json` | alles zusammen als JSON |

### HTTP

| Endpunkt | Zweck |
|---|---|
| `GET /value` | nur die Zahl, als Text – ideal für Shell-Skripte |
| `GET /api/status` | Zustand als JSON |
| `GET /api/recognize` | erzwingt eine Messung, liefert alle Details je Stelle |
| `GET /metrics` | Prometheus |
| `GET /snapshot.jpg` | aktuelles Kamerabild |
| `GET /digit.jpg?i=N` | der Ausschnitt einer Stelle, wie ihn die Erkennung sieht |

---

## Aufbau des Repos

```
firmware/WaterMeterCam/     Arduino-Sketch (das ist alles, was auf den ESP32 kommt)
  WaterMeterCam.ino         Setup, Loop, WLAN
  board_config.h            Kameramodell – die einzige Datei, die man ggf. anfasst
  config.*                  persistente Einstellungen (LittleFS, CRC-gesichert)
  camera.*                  Kamera und Beleuchtung
  imgproc.*                 ROI-Ausschnitt, Drehung, Kontrast, Otsu
  templates.*               Vorlagensammlung + Anlernen
  recognize.*               Kernstück: Abgleich, Kaskade, Zeiger
  meter.*                   Plausibilität und gespeicherter Stand
  mqtt.*                    eigener MQTT-Client (~200 Zeilen, statt PubSubClient)
  webui.*, webui_html.h     Weboberfläche und JSON-API
tools/
  make_default_templates.py erzeugt die eingebauten Start-Vorlagen
  hostcheck/                Testlauf der Erkennung auf dem PC
docs/                       Hardware, Einrichtung, Algorithmus, Fehlersuche
```

---

## Testen ohne ESP32

Die Erkennungslogik lässt sich auf dem PC übersetzen und gegen synthetische,
mechanisch korrekt gerenderte Zählerbilder laufen lassen:

```bash
pip install pillow
./tools/hostcheck/run.sh
```

Das deckt den Nulldurchgang, gleichzeitig umspringende Stellen, schief montierte
Kameras und verrauschte Bilder ab:

```
/tmp/wmc-hostcheck/00000000_0.pgm (soll 00000.000, Drehung 0.0°)
       Rollen: 0(0.95/53%) 0(0.95/53%) 0(0.95/53%) 0(0.95/53%) 0(0.95/53%)
       Zeiger: 0(1°/100%) 0(359°/100%) 0(2°/100%)
  OK        mit Standardvorlagen   00000.000
  OK        nach dem Anlernen      00000.000

14 richtig, 0 korrekt verworfen, 0 falsch
```

Ein falsch gelesener Wert **unterhalb** der Konfidenzschwelle zählt dabei als
„korrekt verworfen“ und nicht als Fehler – genau dafür gibt es die Schwelle.

---

## Grenzen

* **Getestet ist bisher nur der Host-Prüfstand**, nicht die Hardware. Der
  Prüfstand deckt die Erkennung ab; Kamera, WLAN, MQTT und OTA sind sorgfältig
  geschrieben, aber noch nicht auf einem echten Gerät gelaufen.
* Vor dem Anlernen sind die Vorlagen generisch – rechne mit ein paar Tagen, bis
  alle zehn Ziffern aus eigenen Bildern stammen.
* Zähler mit springendem (statt durchlaufendem) Zählwerk funktionieren, aber die
  Kaskade hilft dort weniger; notfalls die Mindestkonfidenz senken.
* Kein Batteriebetrieb: das Gerät bleibt im WLAN und misst zyklisch. Für
  Deep-Sleep müsste die Weboberfläche weichen.
* Ziffernblatt-Spiegelungen sind der häufigste Praxisärger – lieber indirekt
  beleuchten als die LED aufdrehen (Standard ist deshalb nur 40 von 255).

---

## Dank

Die Idee, den Zähler mit einer ESP32-CAM zu digitalisieren, und die
Ziffernübergangs-Logik gehen auf
[jomjol/AI-on-the-edge-device](https://github.com/jomjol/AI-on-the-edge-device)
zurück. Dieses Repo ist eine unabhängige, bewusst kleine Neuimplementierung für
die Arduino-IDE.

## Lizenz

MIT – siehe [LICENSE](LICENSE).
