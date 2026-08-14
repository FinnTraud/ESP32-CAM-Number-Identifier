#include "recognize.h"

#include <math.h>

#include "templates.h"
#include "util.h"

#define DIAL_N 48

static Recognition *s_res = nullptr;
static uint8_t s_dialBuf[DIAL_N * DIAL_N];

// Wie weit die gemessene Rollenstellung von der mechanisch erzwungenen
// abweichen darf, in Bruchteilen einer Ziffernhoehe.
static const float CASCADE_TOL = 0.15f;

bool recognizeInit() {
  if (s_res) return true;
  s_res = (Recognition *)(psramFound() ? ps_malloc(sizeof(Recognition)) : malloc(sizeof(Recognition)));
  if (!s_res) {
    Serial.println("[rec] Speicher fuer Ergebnisse reicht nicht");
    return false;
  }
  memset(s_res, 0, sizeof(Recognition));
  return true;
}

Recognition *recognizeResult() { return s_res; }

// ---------------------------------------------------------------------------
// Ausschnitt holen und normieren
// ---------------------------------------------------------------------------

static void extractPatch(const GrayImage &img, const Roi &roi, uint8_t *dst, bool *darkOnLight) {
  // Vertikal ueber die ROI hinaus greifen, damit der Abgleich die Rolle beim
  // Weiterdrehen verfolgen kann.
  float expandH = (float)roi.h * (float)PATCH_H / (float)TPL_H;
  float ey = roi.y + roi.h * 0.5f - expandH * 0.5f;
  extractRoi(img, roi.x, ey, roi.w, expandH, cfg.rotationDeg, dst, TPL_W, PATCH_H);
  normalizeContrast(dst, PATCH_PIX);

  bool dol;
  if (cfg.polarity == 1) {
    dol = true;
  } else if (cfg.polarity == 2) {
    dol = false;
  } else {
    dol = isDarkOnLight(dst, TPL_W, PATCH_H);
  }
  // Intern arbeitet alles mit "Ziffer hell auf dunkel".
  if (dol) invertBuf(dst, PATCH_PIX);
  if (darkOnLight) *darkOnLight = dol;
}

void recognizeExtractDigitPatch(const GrayImage &img, uint8_t index, uint8_t *dst,
                                bool *darkOnLight) {
  if (index >= cfg.digitCount) return;
  extractPatch(img, cfg.digits[index], dst, darkOnLight);
}

void recognizeWindow(const uint8_t *patch, int offset, uint8_t *dst) {
  offset = clampT(offset, 0, 2 * MAX_SHIFT);
  memcpy(dst, patch + (size_t)offset * TPL_W, TPL_PIX);
}

int recognizeCenterOffset(const uint8_t *patch, float *quality) {
  // Zeilenweise "Tinte" (die Ziffer ist hell).
  float ink[PATCH_H];
  for (int y = 0; y < PATCH_H; y++) {
    uint32_t s = 0;
    const uint8_t *row = patch + (size_t)y * TPL_W;
    for (int x = 0; x < TPL_W; x++) s += row[x];
    ink[y] = (float)s / TPL_W;
  }

  // Gesucht ist der Offset, bei dem die obersten und untersten Zeilen des
  // Fensters moeglichst leer sind - dann liegt genau eine Ziffer darin.
  int best = OFFSET_CENTER;
  float bestCost = 1e30f;
  float sum = 0;
  int count = 0;
  for (int o = 0; o <= 2 * MAX_SHIFT; o++) {
    float cost = ink[o] + ink[o + 1] + ink[o + TPL_H - 2] + ink[o + TPL_H - 1];
    sum += cost;
    count++;
    if (cost < bestCost) {
      bestCost = cost;
      best = o;
    }
  }
  if (quality) {
    float mean = count ? sum / count : 0;
    *quality = mean > 1e-3f ? (mean - bestCost) / mean : 0.0f;
  }
  return best;
}

// ---------------------------------------------------------------------------
// Abgleich
// ---------------------------------------------------------------------------

static void computeScores(DigitResult &d, uint8_t bank) {
  const int S = clampT((int)cfg.shiftSearch, 1, MAX_SHIFT);
  const int oLo = OFFSET_CENTER - S;
  const int oHi = OFFSET_CENTER + S;

  for (int c = 0; c < NUM_CLASSES; c++)
    for (int o = 0; o < MAX_OFFSETS; o++) d.scores[c][o] = -2.0f;

  // Norm des mittelwertfreien Fensters je Offset. Weil die Vorlage
  // mittelwertfrei ist, faellt der Mittelwert des Fensters im Skalarprodukt
  // von allein heraus - er muss nicht abgezogen werden.
  float norm[MAX_OFFSETS];
  for (int o = oLo; o <= oHi; o++) {
    const uint8_t *p = d.patch + (size_t)o * TPL_W;
    float s = 0, s2 = 0;
    for (int i = 0; i < TPL_PIX; i++) {
      s += p[i];
      s2 += (float)p[i] * p[i];
    }
    float v = s2 - s * s / TPL_PIX;
    norm[o] = v > 1e-3f ? sqrtf(v) : 0.0f;
  }

  static float tdev[TPL_PIX];
  for (int c = 0; c < NUM_CLASSES; c++) {
    const uint8_t *t = templateData(bank, (uint8_t)c);
    if (!t) continue;
    float ts = 0;
    for (int i = 0; i < TPL_PIX; i++) ts += t[i];
    float tm = ts / TPL_PIX;
    float tn2 = 0;
    for (int i = 0; i < TPL_PIX; i++) {
      tdev[i] = t[i] - tm;
      tn2 += tdev[i] * tdev[i];
    }
    if (tn2 < 1e-3f) continue;
    float tn = sqrtf(tn2);

    for (int o = oLo; o <= oHi; o++) {
      if (norm[o] <= 0.0f) {
        d.scores[c][o] = 0.0f;
        continue;
      }
      const uint8_t *p = d.patch + (size_t)o * TPL_W;
      float dot = 0;
      for (int i = 0; i < TPL_PIX; i++) dot += (float)p[i] * tdev[i];
      d.scores[c][o] = dot / (norm[o] * tn);
    }
  }

  d.rawClass = -1;
  d.rawOffset = OFFSET_CENTER;
  d.rawScore = -2.0f;
  for (int c = 0; c < NUM_CLASSES; c++) {
    for (int o = oLo; o <= oHi; o++) {
      if (d.scores[c][o] > d.rawScore) {
        d.rawScore = d.scores[c][o];
        d.rawClass = (int8_t)c;
        d.rawOffset = (int8_t)o;
      }
    }
  }
}

// Kontinuierliche Rollenstellung, die zu Klasse c bei Offset o gehoert.
static inline float digitPosition(int c, int o) {
  return (float)c + (float)cfg.rollDirection * (float)(OFFSET_CENTER - o) / (float)TPL_H;
}

static inline float wrap10(float x) {
  while (x < 0) x += 10.0f;
  while (x >= 10.0f) x -= 10.0f;
  return x;
}

static uint8_t confidenceFrom(float best, float second) {
  if (best <= 0) return 0;
  float quality = clampT(best, 0.0f, 1.0f);
  float margin = clampT((best - second) / 0.25f, 0.0f, 1.0f);
  return (uint8_t)clampT(100.0f * quality * margin + 0.5f, 0.0f, 100.0f);
}

// Waehlt die Ziffer einer Stelle. hasConstraint == false gilt fuer die
// niederwertigste Stelle, die von unten nichts vorgeschrieben bekommt.
static void decideDigit(DigitResult &d, bool hasConstraint, float lowerFraction) {
  const int S = clampT((int)cfg.shiftSearch, 1, MAX_SHIFT);
  const int oLo = OFFSET_CENTER - S;
  const int oHi = OFFSET_CENTER + S;

  float bestPerD[NUM_CLASSES];
  int8_t coO[NUM_CLASSES];
  float posPerD[NUM_CLASSES];
  for (int i = 0; i < NUM_CLASSES; i++) {
    bestPerD[i] = -2.0f;
    coO[i] = OFFSET_CENTER;
    posPerD[i] = 0;
  }

  for (int c = 0; c < NUM_CLASSES; c++) {
    for (int o = oLo; o <= oHi; o++) {
      float sc = d.scores[c][o];
      if (sc < -1.5f) continue;
      float pos = digitPosition(c, o);
      int D;
      float storePos;
      if (hasConstraint) {
        // Mechanik: pos muss lowerFraction + ganze Zahl sein.
        float y = pos - lowerFraction;
        float r = y - roundf(y);
        if (fabsf(r) > CASCADE_TOL) continue;
        D = ((int)lroundf(y) % 10 + 10) % 10;
        storePos = D + lowerFraction;
      } else {
        // Niederwertigste Stelle: nichts gibt die Drehung vor. Abgelesen wird
        // die Ziffer, die im Fenster steht - also die getroffene Klasse c.
        // Nur wenn die Rolle deutlich *unter* ihrer Ruhelage sitzt, ist der
        // Uebergang c-1 -> c noch nicht abgeschlossen und es gilt c-1.
        // Die Schwelle von 0,10 statt 0 verhindert, dass ein halbes Pixel
        // Rauschen bei 0 einen Ruecksprung auf 9 ausloest.
        float delta = pos - (float)c;
        D = (delta < -0.10f) ? (c + 9) % 10 : c;
        storePos = wrap10(pos);
      }
      if (sc > bestPerD[D]) {
        bestPerD[D] = sc;
        coO[D] = (int8_t)o;
        posPerD[D] = storePos;
      }
    }
  }

  int bestD = -1, secondD = -1;
  for (int i = 0; i < NUM_CLASSES; i++) {
    if (bestD < 0 || bestPerD[i] > bestPerD[bestD]) {
      secondD = bestD;
      bestD = i;
    } else if (secondD < 0 || bestPerD[i] > bestPerD[secondD]) {
      secondD = i;
    }
  }

  if (bestD < 0 || bestPerD[bestD] < -1.5f) {
    // Keine mechanisch moegliche Deutung gefunden - auf den freien Treffer
    // zurueckfallen und das ueber die Konfidenz melden.
    d.digit = d.rawClass;
    d.offset = d.rawOffset;
    d.score = d.rawScore;
    d.position = wrap10(digitPosition(d.rawClass, d.rawOffset));
    d.confidence = 0;
    return;
  }

  d.digit = (int8_t)bestD;
  d.offset = coO[bestD];
  d.score = bestPerD[bestD];
  d.position = posPerD[bestD];
  float second = (secondD >= 0) ? bestPerD[secondD] : -1.0f;
  if (second < -1.5f) second = -1.0f;
  d.confidence = confidenceFrom(d.score, second);
}

// ---------------------------------------------------------------------------
// Zeiger (analoge Rollen)
// ---------------------------------------------------------------------------

static void readDial(const GrayImage &img, const DialCfg &dc, DialResult &r) {
  extractRoi(img, dc.roi.x, dc.roi.y, dc.roi.w, dc.roi.h, cfg.rotationDeg, s_dialBuf, DIAL_N,
             DIAL_N);
  normalizeContrast(s_dialBuf, DIAL_N * DIAL_N);
  GrayImage dial = {s_dialBuf, DIAL_N, DIAL_N};

  // Mittelpunkt in Pixelindizes (siehe unrotate() in imgproc.cpp).
  const float cx = (DIAL_N - 1) * 0.5f;
  const float cy = (DIAL_N - 1) * 0.5f;
  const float R = DIAL_N * 0.5f;
  // Nur der innere Bereich wird abgetastet: am Rand stehen Teilstriche und
  // aufgedruckte Ziffern, die sonst alle 36 Grad einen falschen Peak erzeugen.
  const float rIn = 0.25f * R;
  const float rOut = 0.80f * R;

  // Static, nicht auf dem Stack: 1,4 kB sind auf dem Loop-Task zu viel.
  static float prof[360];
  float sum = 0, sum2 = 0;
  for (int a = 0; a < 360; a++) {
    float th = a * (float)M_PI / 180.0f;
    float sa = sinf(th), ca = cosf(th);
    float acc = 0, wacc = 0;
    for (float rr = rIn; rr <= rOut; rr += 1.0f) {
      // 0 Grad = 12 Uhr, positive Winkel im Uhrzeigersinn.
      float v = sampleBilinear(dial, cx + rr * sa, cy - rr * ca);
      // Aussen liegende Abtastpunkte staerker gewichten: der lange Arm des
      // Zeigers soll gegen ein evtl. Gegengewicht gewinnen.
      acc += (255.0f - v) * rr;
      wacc += rr;
    }
    prof[a] = wacc > 0 ? acc / wacc : 0;
    sum += prof[a];
    sum2 += prof[a] * prof[a];
  }

  int peak = 0;
  for (int a = 1; a < 360; a++)
    if (prof[a] > prof[peak]) peak = a;

  // Subpixelgenauigkeit ueber eine Parabel durch die drei Nachbarwerte.
  float y0 = prof[(peak + 359) % 360];
  float y1 = prof[peak];
  float y2 = prof[(peak + 1) % 360];
  float denom = (y0 - 2 * y1 + y2);
  float delta = fabsf(denom) > 1e-6f ? 0.5f * (y0 - y2) / denom : 0.0f;
  delta = clampT(delta, -1.0f, 1.0f);
  float angle = peak + delta;
  if (angle < 0) angle += 360.0f;
  if (angle >= 360.0f) angle -= 360.0f;

  r.angleDeg = angle;

  float mean = sum / 360.0f;
  float var = sum2 / 360.0f - mean * mean;
  float sd = var > 0 ? sqrtf(var) : 0.0f;
  float z = sd > 1e-3f ? (y1 - mean) / sd : 0.0f;
  r.confidence = (uint8_t)clampT((z - 1.5f) / 2.0f * 100.0f, 0.0f, 100.0f);

  float rel = (float)dc.clockwise * (angle - dc.offsetDeg);
  rel = fmodf(fmodf(rel, 360.0f) + 360.0f, 360.0f);
  r.position = rel / 36.0f;
  r.digit = (int8_t)clampT((int)floorf(r.position), 0, 9);

  float pmin = prof[0], pmax = prof[0];
  for (int a = 1; a < 360; a++) {
    if (prof[a] < pmin) pmin = prof[a];
    if (prof[a] > pmax) pmax = prof[a];
  }
  float span = pmax - pmin;
  for (int k = 0; k < 72; k++) {
    float v = span > 1e-3f ? (prof[k * 5] - pmin) / span : 0.0f;
    r.profile[k] = (uint8_t)clampT(v * 255.0f, 0.0f, 255.0f);
  }
}

void recognizeRenderDial(const GrayImage &img, uint8_t index, uint8_t *dst, int n) {
  if (index >= cfg.dialCount || n <= 0) return;
  const DialCfg &dc = cfg.dials[index];
  extractRoi(img, dc.roi.x, dc.roi.y, dc.roi.w, dc.roi.h, cfg.rotationDeg, dst, n, n);
  normalizeContrast(dst, n * n);

  // Erkannten Winkel als helle Linie einzeichnen, damit in der Web-UI sofort
  // sichtbar ist, ob der Zeiger richtig getroffen wurde.
  DialResult r;
  readDial(img, dc, r);
  float cx = (n - 1) * 0.5f, cy = (n - 1) * 0.5f;
  float th = r.angleDeg * (float)M_PI / 180.0f;
  float sa = sinf(th), ca = cosf(th);
  for (float rr = 0; rr <= n * 0.45f; rr += 0.5f) {
    int px = (int)lroundf(cx + rr * sa);
    int py = (int)lroundf(cy - rr * ca);
    if (px >= 0 && px < n && py >= 0 && py < n) dst[py * n + px] = 255;
  }
}

// ---------------------------------------------------------------------------
// Gesamtlauf
// ---------------------------------------------------------------------------

bool recognizeRun(const GrayImage &img) {
  if (!s_res) return false;
  uint32_t t0 = millis();

  Recognition &R = *s_res;
  R.valid = false;
  R.error[0] = 0;
  R.digitCount = cfg.digitCount;
  R.dialCount = cfg.dialCount;
  R.rawValue = 0;
  R.confidence = 0;
  R.sharpness = sharpness(img);

  if (cfg.digitCount == 0 && cfg.dialCount == 0) {
    strncpy(R.error, "Keine Stellen konfiguriert", sizeof(R.error) - 1);
    return false;
  }

  for (uint8_t i = 0; i < cfg.digitCount; i++) {
    DigitResult &d = R.digits[i];
    extractPatch(img, cfg.digits[i], d.patch, &d.darkOnLight);
    computeScores(d, templateBankFor(i));
    d.digit = -1;
  }
  for (uint8_t i = 0; i < cfg.dialCount; i++) {
    readDial(img, cfg.dials[i], R.dials[i]);
  }

  // Kaskade von der niederwertigsten Stelle nach oben.
  const int n = configElements();
  float xLower = 0.0f;
  bool haveLower = false;
  uint8_t minConf = 100;

  for (int e = n - 1; e >= 0; e--) {
    if (e >= cfg.digitCount) {
      DialResult &dl = R.dials[e - cfg.digitCount];
      if (!haveLower) {
        // Unterster Zeiger: die Analoganzeige ist selbst schon exakt. Die
        // kleine Toleranz sorgt dafuer, dass ein Zeiger knapp vor der Null
        // nicht als 9 gelesen wird (rund 0,7 Grad - weit unter dem, was ein
        // Zeiger ueberhaupt aufloest).
        dl.digit = (int8_t)(((int)floorf(dl.position + 0.02f)) % 10);
        xLower = dl.position;
      } else {
        float f = xLower / 10.0f;
        int D = ((int)lroundf(dl.position - f) % 10 + 10) % 10;
        dl.digit = (int8_t)D;
        xLower = D + f;
      }
      haveLower = true;
      if (dl.confidence < minConf) minConf = dl.confidence;
    } else {
      DigitResult &d = R.digits[e];
      decideDigit(d, haveLower, haveLower ? xLower / 10.0f : 0.0f);
      xLower = d.position;
      haveLower = true;
      if (d.confidence < minConf) minConf = d.confidence;
    }
  }

  double value = 0;
  for (uint8_t i = 0; i < cfg.digitCount; i++) {
    int8_t dg = R.digits[i].digit;
    if (dg < 0) {
      strncpy(R.error, "Stelle nicht erkannt", sizeof(R.error) - 1);
      R.confidence = 0;
      R.durationMs = millis() - t0;
      return false;
    }
    value = value * 10 + dg;
  }
  for (uint8_t i = 0; i < cfg.dialCount; i++) {
    value = value * 10 + R.dials[i].digit;
  }
  for (uint8_t k = 0; k < configDecimals(); k++) value /= 10.0;

  R.rawValue = value;
  R.confidence = minConf;
  R.valid = true;
  R.durationMs = millis() - t0;
  return true;
}

// ---------------------------------------------------------------------------
// Anlernen
// ---------------------------------------------------------------------------

bool recognizeLearnFromReading(const GrayImage &img, double reading, String &report) {
  if (cfg.digitCount == 0) {
    report = "Keine Ziffernstellen konfiguriert.";
    return false;
  }

  const int n = configElements();
  const uint8_t dec = configDecimals();

  double scaledD = reading;
  for (uint8_t k = 0; k < dec; k++) scaledD *= 10.0;
  long long scaled = (long long)llround(scaledD);
  if (scaled < 0) {
    report = "Negativer Zaehlerstand.";
    return false;
  }

  int trueDigit[MAX_DIGITS + MAX_DIALS];
  long long rest = scaled;
  for (int e = n - 1; e >= 0; e--) {
    trueDigit[e] = (int)(rest % 10);
    rest /= 10;
  }
  if (rest != 0) {
    report = "Zaehlerstand hat mehr Stellen als konfiguriert.";
    return false;
  }

  // Wahre kontinuierliche Stellung von unten nach oben aufbauen.
  float x[MAX_DIGITS + MAX_DIALS];
  bool lastIsDial = (cfg.dialCount > 0);
  float lastFrac = 0.5f;
  float centerQuality = 1.0f;
  int lastCenterOffset = OFFSET_CENTER;

  static uint8_t patch[MAX_DIGITS][PATCH_PIX];
  for (uint8_t i = 0; i < cfg.digitCount; i++) {
    extractPatch(img, cfg.digits[i], patch[i], nullptr);
  }

  if (lastIsDial) {
    DialResult dl;
    readDial(img, cfg.dials[cfg.dialCount - 1], dl);
    lastFrac = dl.position - floorf(dl.position);
  } else {
    lastCenterOffset = recognizeCenterOffset(patch[cfg.digitCount - 1], &centerQuality);
    lastFrac = (float)cfg.rollDirection * (float)(OFFSET_CENTER - lastCenterOffset) / (float)TPL_H;
    if (lastFrac < 0) lastFrac += 1.0f;
  }
  x[n - 1] = trueDigit[n - 1] + lastFrac;
  for (int e = n - 2; e >= 0; e--) x[e] = trueDigit[e] + x[e + 1] / 10.0f;

  int learned = 0, skipped = 0;
  report = "";
  static uint8_t window[TPL_PIX];

  for (uint8_t i = 0; i < cfg.digitCount; i++) {
    float frac = x[i] - trueDigit[i];
    int cls = trueDigit[i];
    if (frac > 0.5f) {
      // Die Rolle ist schon mehr als halb weiter - sichtbar ist im Fenster
      // ueberwiegend die naechste Ziffer.
      frac -= 1.0f;
      cls = (cls + 1) % 10;
    }
    int o;
    if (i == cfg.digitCount - 1 && !lastIsDial) {
      o = lastCenterOffset;
      if (centerQuality < 0.30f) {
        skipped++;
        report += "Stelle " + String(i + 1) + ": Rolle steht zwischen zwei Ziffern, uebersprungen. ";
        continue;
      }
    } else {
      o = (int)lroundf(OFFSET_CENTER - (float)cfg.rollDirection * frac * (float)TPL_H);
    }
    if (o < 0 || o > 2 * MAX_SHIFT) {
      skipped++;
      report += "Stelle " + String(i + 1) + ": ausserhalb des Suchbereichs, uebersprungen. ";
      continue;
    }

    recognizeWindow(patch[i], o, window);
    templateLearn(templateBankFor(i), (uint8_t)cls, window);
    learned++;
  }

  if (learned > 0) templatesSave();
  report = "Angelernt: " + String(learned) + " von " + String(cfg.digitCount) + " Stellen. " +
           (skipped ? report : String(""));
  return learned > 0;
}
