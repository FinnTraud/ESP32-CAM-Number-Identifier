# Hardware

## Stückliste

| Teil | Anmerkung |
|---|---|
| ESP32-CAM AI Thinker, **mit PSRAM** | Standardvariante mit OV2640 und SD-Slot. Ohne PSRAM läuft die Firmware nur in QVGA und die Erkennung wird deutlich schlechter. |
| ESP32-CAM-MB Programmieradapter | Oder ein FTDI-Adapter auf **3,3 V** Logik. Der MB-Adapter spart das IO0-Gebrücke. |
| Netzteil 5 V, ≥ 1 A | An `5V` und `GND`. Die Kamera zieht kurze Spitzen; ein schwaches Netzteil äußert sich als „Brownout detector was triggered“ im seriellen Monitor. |
| Gehäuse / Halterung | [jomjols Halterung auf Thingiverse](https://www.thingiverse.com/thing:3860911) passt für die üblichen Hauswasserzähler. |
| Optional: 2 warmweiße LEDs + Widerstand | Indirekte Beleuchtung ist der Onboard-LED deutlich überlegen (siehe unten). |

## Das Objektiv muss umgestellt werden

Das ist der wichtigste Punkt und der häufigste Grund für „es erkennt nichts“.

Die OV2640 ist ab Werk auf etwa 1 m fokussiert. Der Zähler ist 5–10 cm entfernt
– das Bild ist damit hoffnungslos unscharf. Das Objektiv sitzt in einem
Schraubgewinde, oft mit einem Tropfen Kleber gesichert:

1. Den Kleber vorsichtig mit einem Skalpell anritzen.
2. Das Objektiv langsam **heraus**drehen (im Gegenuhrzeigersinn), etwa eine
   halbe bis ganze Umdrehung.
3. Weboberfläche öffnen, *System* → **Bildschärfe** beobachten und beim Drehen
   auf Maximum bringen. Der Wert ist die Varianz des Laplace-Operators – je
   höher, desto schärfer.

Nicht zu weit drehen: irgendwann fällt die Linse heraus, und Staub auf dem
Sensor ist unangenehm.

## Beleuchtung

Die weiße Hochleistungs-LED auf GPIO 4 wird per PWM angesteuert; Standard sind
**40 von 255**. Das ist Absicht:

* Volle Helligkeit überstrahlt die glänzende Zifferblattfolie. Die Reflexion
  landet genau dort, wo die Ziffern stehen.
* Die LED wird bei voller Leistung sehr heiß, und im Dauerbetrieb im Kellerschacht
  ist das keine gute Idee.

Besser als die Onboard-LED ist eine **indirekte** Beleuchtung: zwei kleine LEDs
seitlich, die die Zählerscheibe schräg anleuchten, statt frontal. Reflexionen
wandern dann aus dem Ziffernfenster heraus.

Alternativ die Belichtung fest einstellen (*Einstellungen → Kamera →
Automatikbelichtung aus*). Feste Belichtung liefert von Messung zu Messung
identische Bilder – genau das, was der Vorlagenabgleich braucht. Die
Automatik regelt sonst je nach Tageslicht im Schacht nach.

## Montage

* **Abstand** 5–10 cm, so dass das Zählwerk formatfüllend im Bild liegt. Je
  größer die Ziffern im Bild, desto besser.
* **Fest** montieren. Verrutscht die Kamera, stimmen die Ausschnitte nicht mehr
  und alle angelernten Vorlagen sind wertlos. Heißkleber oder eine geschraubte
  Halterung, kein Klebeband.
* **Leicht schief ist egal** – die Bilddrehung in der Weboberfläche gleicht bis
  ±15° aus. Perfekt ausrichten muss man nicht.
* Feuchtigkeit: in Kellerschächten kondensiert es. Ein Beutel Silikagel im
  Gehäuse hilft mehr, als man denkt.

## Verkabelung zum Flashen

Mit FTDI-Adapter (auf **3,3 V** stellen, 5 V zerstört den ESP32):

```
FTDI          ESP32-CAM
5V     ──────  5V
GND    ──────  GND
TX     ──────  U0R
RX     ──────  U0T
              IO0 ──── GND   (nur zum Flashen!)
```

Ablauf: IO0 auf GND brücken → Reset drücken → in der Arduino-IDE hochladen →
Brücke entfernen → Reset. Mit dem ESP32-CAM-MB-Adapter entfällt das Brücken.

## Stromaufnahme

Grob im Dauerbetrieb mit WLAN: 90–160 mA, Spitzen bis ~300 mA während der
Aufnahme, plus bis zu 150 mA wenn die LED voll aufgedreht ist. Batteriebetrieb
ist damit nicht sinnvoll – die Firmware bleibt bewusst im WLAN, damit die
Weboberfläche und MQTT jederzeit erreichbar sind.

## Boards außer AI Thinker

In `WaterMeterCam.ino` ganz oben ist das Modell auswählbar; `camera_pins.h`
enthält außerdem ESP32-S3-EYE, XIAO ESP32S3 Sense und M5Stack Wide. Auf Boards
ohne LED (`LED_GPIO_NUM -1`) entfällt die Beleuchtungssteuerung – dort ist eine
externe Lichtquelle Pflicht.
