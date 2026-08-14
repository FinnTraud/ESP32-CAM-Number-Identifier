# Wie die Erkennung funktioniert

Kurzfassung: Ausschnitt normieren → gegen zehn Vorlagen abgleichen → mit der
Mechanik des Zählwerks entscheiden.

---

## 1. Ausschnitt holen

Für jede Stelle steht ein Rechteck (ROI) in der Konfiguration. Der Ausschnitt
wird **vertikal über das ROI hinaus** genommen, nämlich `TPL_H + 2·MAX_SHIFT`
Zeilen statt `TPL_H` (konkret 48 statt 24). Der Überstand ist nötig, weil sich
die Rolle dreht: die Ziffer wandert aus dem Fenster heraus und die nächste
kommt nach. Der Suchbereich von einer halben Ziffernhöhe nach oben und unten
reicht aus – dreht die Rolle weiter, passt ohnehin die *nächste* Ziffer besser,
und genau das soll der Abgleich dann auch finden.

Die Abtastung ist bilinear und rechnet eine konfigurierte Bilddrehung gleich
mit heraus (Drehung um die Bildmitte). So lässt sich eine schief montierte
Kamera in Software geraderücken.

Danach:

1. **Kontrastspreizung** auf 2./98. Perzentil (robuster als min/max – ein
   einzelner Glanzpunkt würde den Bereich sonst zerstören).
2. **Polarität bestimmen**: der Rand des Ausschnitts zeigt fast immer
   Trommelfläche, nicht Ziffer. Ist der Rand heller als die Mitte, liegt eine
   dunkle Ziffer auf hellem Grund vor und der Ausschnitt wird invertiert.
   Intern arbeitet alles mit „Ziffer hell auf dunkel“.

## 2. Abgleich

Verglichen wird per **normierter Kreuzkorrelation** (NCC) zwischen dem
`16×24`-Fenster und zehn Vorlagen, für jeden vertikalen Versatz `o` im
Suchbereich:

```
score(c, o) = Σ p_i · (t_i − t̄)  /  ( ‖p − p̄‖ · ‖t − t̄‖ )
```

Weil die Vorlage mittelwertfrei ist, fällt der Mittelwert des Fensters im
Skalarprodukt von selbst heraus – er muss nicht abgezogen werden. NCC ist
unempfindlich gegen Helligkeits- und Kontrastschwankungen, also genau gegen
das, was sich zwischen Tag und Nacht im Keller ändert.

Ergebnis ist eine Matrix `score[10][25]`.

## 3. Die Kaskade – der eigentliche Kern

Ein Rollenzählwerk ist durchgekoppelt: Stelle *i* dreht sich um **genau eine
Ziffer** weiter, während Stelle *i+1* von 9 auf 0 läuft. Also gilt für die
kontinuierliche Rollenstellung:

```
x_i = D_i + x_(i+1) / 10
```

Beispiel: steht die nächstniedrigere Stelle auf 7, dann muss die aktuelle Rolle
70 % ihres Weges zur nächsten Ziffer zurückgelegt haben. Das ist eine harte
mechanische Zwangsbedingung, und sie ist genau das, was die Erkennung von
„rät bei halb gedrehten Ziffern“ auf „weiß es“ hebt.

Aus dem Paar `(c, o)` folgt die Rollenstellung direkt:

```
x(c, o) = c + rollDirection · (OFFSET_CENTER − o) / TPL_H
```

Die Kaskade läuft **von unten nach oben**:

* **Niederwertigstes Glied** (unterster Zeiger, sonst letzte Rolle): hier gibt
  nichts die Drehung vor. Abgelesen wird die getroffene Klasse `c`; nur wenn
  die Rolle deutlich *unter* ihrer Ruhelage sitzt (`δ < −0,10`), ist der
  Übergang `c−1 → c` noch nicht abgeschlossen und es gilt `c−1`.
  Die Schwelle statt eines einfachen `floor()` verhindert, dass ein halbes
  Pixel Rauschen bei 0 einen Rücksprung auf 9 auslöst.

* **Jedes weitere Glied**: mit `f = x_(i+1) / 10` werden nur noch die `(c, o)`
  zugelassen, für die `x(c,o) − f` nahe an einer ganzen Zahl liegt (Toleranz
  0,15 Ziffernhöhen). Unter diesen gewinnt der beste NCC-Wert. Die Ziffer ist
  `D = round(x(c,o) − f) mod 10`, und nach oben weitergereicht wird
  `x_i = D + f`.

**Konfidenz** ist bewusst zweiteilig: es zählt sowohl die absolute Passgenauigkeit
als auch der Abstand zur zweitbesten *Ziffer* (nicht zum zweitbesten Versatz –
mehrere Versätze derselben Ziffer sind kein Widerspruch):

```
conf = 100 · clamp(best, 0, 1) · clamp((best − second) / 0,25, 0, 1)
```

## 4. Zeiger

Für jedes Zifferblatt wird der Ausschnitt auf 48×48 normiert und ein
**radiales Dunkelheitsprofil** über 360° gebildet: entlang jedes Winkels wird
von 0,25 R bis 0,80 R abgetastet und `255 − Pixelwert` aufsummiert, gewichtet
mit dem Radius (damit der lange Zeigerarm gegen ein Gegengewicht gewinnt). Der
äußere Rand bleibt bewusst außen vor – dort stehen Teilstriche und aufgedruckte
Ziffern, die sonst alle 36° einen falschen Peak erzeugen.

Das Maximum wird über eine Parabel durch die drei Nachbarwerte auf Bruchteile
eines Grades genau bestimmt. Winkel → Wert: `x = ((Winkel − Nullpunkt) ·
Drehrichtung mod 360) / 36`. Zeiger gehen in dieselbe Kaskade ein wie die
Rollen.

> **Halbes Pixel, drei Grad.** Alle Koordinaten sind Pixel*indizes*: der
> Mittelpunkt eines `n` Pixel breiten Bereichs liegt bei `(n−1)/2`, nicht bei
> `n/2`. Wird das verwechselt, sitzt der angenommene Zeigermittelpunkt ein
> halbes Pixel daneben – bei Radius 13 sind das schon gut 2°, genug, um am
> Nulldurchgang eine 0 als 9 zu lesen. Der Fehler steckte in der ersten Fassung
> und ist der Grund, warum `unrotate()` und `extractRoi()` die `−0,5`-Terme
> tragen.

## 5. Anlernen

Beim Anlernen tippt man den *wahren* Stand ein. Daraus baut die Firmware die
wahren Rollenstellungen mit derselben Formel `x_i = D_i + x_(i+1)/10` auf und
weiß damit für jede Stelle, an welchem Versatz welche Ziffer stehen **muss**.
Genau dieses Fenster wird als Vorlage übernommen.

Das ist mehr, als es klingt: es funktioniert auch für Stellen, die gerade
mitten im Umsprung stehen. Ist die Rolle mehr als halb weiter gedreht
(`f > 0,5`), zeigt das Fenster überwiegend die *nächste* Ziffer – dann wird
`D+1` bei negativem Versatz gelernt.

Nur für das niederwertigste Glied, wenn es eine Rolle ist, fehlt die Vorgabe
von unten. Dort wird der Versatz gesucht, bei dem die obersten und untersten
Zeilen des Fensters am leersten sind (die Lücke zwischen zwei Ziffern). Ist
diese Lücke nicht deutlich genug, steht die Rolle gerade zwischen zwei Ziffern
und die Stelle wird übersprungen statt falsch gelernt.

**Gemeinsame Vorlagensammlung** ist Standard: alle Rollen eines Zählers tragen
dieselbe Schrift, und die niederwertigste Stelle läuft im Betrieb durch alle
zehn Ziffern. Dadurch füllt sich die Sammlung von allein.

**Automatisches Nachführen** aktualisiert Vorlagen gleitend, aber nur bei
Messungen, die die Plausibilitätsprüfung bestanden haben und über 70 % Konfidenz
liegen – sonst lernt sich die Erkennung ihre eigenen Fehler ein.

## 6. Plausibilität

Eine Einzelmessung darf falsch sein, der Zählerstand nicht. Deshalb prüft
`meter.cpp` jede Erkennung gegen den letzten gültigen Wert:

* Konfidenz unter der Schwelle → verworfen
* kleinerer Wert als zuvor → verworfen (Wasserzähler laufen nur vorwärts)
* Zuwachs größer als `maxRatePerMin · verstrichene Minuten` → verworfen

Nur akzeptierte Werte werden gespeichert, und gespeichert wird nur bei echter
Änderung – das schont den Flash. Der Grund der letzten Ablehnung steht in der
Weboberfläche und im MQTT-Topic `.../error`.

---

## Warum kein neuronales Netz?

Es wäre möglich – TFLite Micro läuft auf dem ESP32. Aber:

* Für eine **fest montierte** Kamera ist die Aufgabe stark eingeschränkt: gleiche
  Position, gleiche Schrift, gleiche Größe. Ein Netz löst ein viel allgemeineres
  Problem, als hier gestellt ist.
* Der Abgleich braucht ~40 ms für acht Stellen und passt in ein paar Kilobyte.
* Er lässt sich **auf genau deinen Zähler anlernen** – ein vortrainiertes Netz
  kann das nicht ohne Trainingsdurchlauf am PC.
* Und er ist debugbar: die Weboberfläche zeigt für jede Stelle den Ausschnitt,
  die gewählte Vorlage, den Versatz und den NCC-Wert. Bei einem Netz stünde da
  nur eine Zahl.

Der Preis ist das Anlernen. Wer das nicht will, ist mit
[AI-on-the-edge-device](https://github.com/jomjol/AI-on-the-edge-device)
besser bedient.
