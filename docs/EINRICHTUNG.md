# Einrichtung Schritt für Schritt

Voraussetzung: die Firmware ist geflasht, das Objektiv ist auf Nahdistanz
gestellt (siehe [HARDWARE.md](HARDWARE.md)) und die Kamera hängt fest vor dem
Zähler.

---

## 1. Ins WLAN bringen

Beim ersten Start – oder wenn das gespeicherte WLAN nicht erreichbar ist –
öffnet das Gerät einen eigenen Accesspoint:

```
SSID:     WaterMeterCam-XXXX
Passwort: wasserzaehler
Adresse:  http://192.168.4.1/
```

Dort *Einstellungen → WLAN* ausfüllen, **Alles speichern**, dann *System →
Neustart*. Danach ist das Gerät unter `http://wasserzaehler.local/` erreichbar
(oder unter der IP, die der Router vergeben hat – sie steht auch im seriellen
Monitor bei 115200 Baud).

Klappt die Verbindung später einmal nicht, startet das Gerät wieder seinen
eigenen Accesspoint und versucht es alle fünf Minuten erneut.

## 2. Bild beurteilen

*Ansicht → Livebild*. Prüfe:

* **Ist das Zählwerk formatfüllend?** Je größer die Ziffern im Bild, desto besser.
* **Ist es scharf?** *System* zeigt einen Schärfewert. Beim Drehen am Objektiv
  maximieren.
* **Spiegelt es?** Wenn ein heller Fleck über den Ziffern liegt: LED-Helligkeit
  runter (*Einstellungen → Kamera*), oder indirekt beleuchten.
* **Steht es auf dem Kopf?** *vertikal / horizontal spiegeln* anhaken.

## 3. Aufbau des Zählwerks eintragen

*Einrichtung → Aufbau des Zählwerks*:

* **Schwarze Rollen**: die Anzahl der Ziffern im schwarzen Zählwerk.
  Typischer deutscher Hauswasserzähler: **5**.
* **davon hinter dem Komma**: bei den meisten Zählern **0** – die schwarzen
  Rollen sind ganze m³. Manche Zähler haben rote Ziffernrollen für die
  Nachkommastellen; dann diese mitzählen und hier eintragen.
* **Rote Zeiger**: die runden Zifferblätter, meist **3** (0,1 / 0,01 / 0,001 m³).
  Wer die nicht braucht, trägt 0 ein und bekommt nur ganze m³.
* **Bilddrehung**: schiebt, bis die Ziffernreihe waagerecht steht.

## 4. Ausschnitte ziehen

Das ist der Teil, der über Erfolg oder Misserfolg entscheidet.

Im Bild liegen jetzt blaue Rechtecke (Rollen) und grüne Kreise (Zeiger).

* **Verschieben**: irgendwo im Rechteck anfassen und ziehen.
* **Größe ändern**: die kleine Ecke unten rechts anfassen.
* **Rollen gleichmäßig verteilen**: erstes und letztes Rechteck passend setzen,
  dann den Knopf drücken – die dazwischen werden gleichmäßig verteilt und auf
  dieselbe Größe gebracht.

Für die Rechtecke gilt:

> Ein Rechteck umfasst **genau eine Ziffer**, mit einem schmalen Streifen
> Trommelfläche darüber und darunter.

Der Streifen ist wichtig: an ihm erkennt die Firmware, wie weit die Rolle
gedreht ist. Zu eng gesetzt (Ziffer randvoll im Rechteck) verliert man diese
Information; zu weit gesetzt (zwei Ziffern im Rechteck) auch.

Für die Zeiger: den Kreis so legen, dass er das Zifferblatt umschließt und der
**Drehpunkt in der Mitte** liegt. Der Nullpunkt-Winkel steht normalerweise auf
0° (die 0 oben, 12 Uhr). Läuft ein Zeigerblatt gegen den Uhrzeigersinn – das
kommt bei manchen Zählern abwechselnd vor – die Drehrichtung umstellen.

Danach **Geometrie speichern**.

## 5. Prüfen

*Ansicht → Jetzt erkennen*. Unter „Erkannte Stellen“ steht für jede Stelle:

* der Ausschnitt, so wie ihn die Erkennung sieht (weiße Linien = das gewählte
  Fenster),
* die erkannte Ziffer,
* Konfidenz und NCC-Wert.

Was du hier sehen willst: jede Ziffer sauber im Fenster, mit etwas Luft oben und
unten. Sieht ein Ausschnitt komisch aus – halbe Ziffer, verschoben, zu eng –
zurück zu Schritt 4.

Die Konfidenzen sind an dieser Stelle noch niedrig (oft unter 35 %). Das ist
normal: die Vorlagen stammen noch aus einer Standardschrift.

## 6. Anlernen

Lies den Zählerstand am Gerät ab – **mit allen Stellen**, so wie er konfiguriert
ist. Bei 5 Rollen und 3 Zeigern also z. B. `01234.567`.

Eintippen, **Anlernen** drücken. Die Firmware
schneidet daraufhin die passenden Ausschnitte heraus, legt sie als Vorlagen ab
und setzt gleichzeitig den gespeicherten Zählerstand auf den eingetippten Wert.

Die Meldung sagt, wie viele Stellen gelernt wurden. Stellen, die gerade
zwischen zwei Ziffern stehen, werden übersprungen – das ist richtig so.

Unter *Vorlagen* siehst du die zehn Ziffern. Grün umrandet und ohne Sternchen
heißt „stammt aus einem echten Bild deines Zählers“. Ziel ist 10 von 10.

## 7. Vollständig machen

Es gibt zwei Wege, und beide funktionieren nebenher:

* **Warten.** Bei aktiviertem „Vorlagen automatisch nachführen“ verbessert das
  Gerät die Vorlagen bei jeder sicheren Messung selbst, und die niederwertigste
  Rolle läuft im Betrieb durch alle zehn Ziffern.
* **Nachhelfen.** Nach ein paar Litern Wasserverbrauch erneut ablesen und
  anlernen. Weil die gemeinsame Vorlagensammlung aktiv ist, liefert jeder
  Durchgang bis zu fünf verschiedene Ziffern auf einmal.

Sobald 10 von 10 gelernt sind, steigen die Konfidenzen typisch auf 70–100 %.

## 8. Messbetrieb einstellen

*Einstellungen → Messung & Plausibilität*:

* **Messintervall**: 300 s ist ein guter Wert. Kürzer bringt bei einem
  Wasserzähler wenig und heizt das Board auf.
* **max. Zuwachs je Minute**: in Zählereinheiten, also m³/min. `0.05` entspricht
  50 l/min – deutlich über jedem Haushaltsverbrauch, fängt aber Fehlerkennungen
  ab. Wer eine Gartenbewässerung hat, setzt den Wert höher.
* **Mindestkonfidenz** (unter *Erkennung*): 35 ist der Standard. Zu hoch → viele
  Messungen fallen aus; zu niedrig → Fehlerkennungen kommen durch. Wenn im
  Betrieb häufig „Konfidenz zu niedrig“ steht, ist meist der Ausschnitt schuld,
  nicht die Schwelle.

## 9. MQTT / Home Assistant

*Einstellungen → MQTT*: Broker, Port, ggf. Benutzer und Passwort, Basis-Topic.
**Home-Assistant-Discovery** anhaken.

Nach dem Speichern verbindet sich das Gerät und meldet fünf Entitäten an
(Zählerstand, letzte Erkennung, Konfidenz, Fehler, WLAN-Pegel). In Home
Assistant taucht ein Gerät „Wasserzähler“ auf.

Der Zählerstand ist als `total_increasing` deklariert und lässt sich damit
direkt im Energie-/Wasserdashboard verwenden.

## 10. Kontrolle nach ein paar Tagen

*Ansicht* zeigt „x Messungen, y verworfen“ und den Grund der letzten Ablehnung.
Ein paar Prozent Ausschuss sind normal (Rollen im Umsprung, kurzzeitige
Reflexionen). Werden dauerhaft mehr als etwa ein Fünftel verworfen, lohnt ein
Blick in [FEHLERSUCHE.md](FEHLERSUCHE.md).
