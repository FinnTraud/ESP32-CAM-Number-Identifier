#include "imgproc.h"

#include <math.h>

#include "util.h"

float sampleBilinear(const GrayImage &img, float x, float y) {
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x > img.w - 1) x = img.w - 1;
  if (y > img.h - 1) y = img.h - 1;

  int x0 = (int)x;
  int y0 = (int)y;
  int x1 = x0 + 1 < img.w ? x0 + 1 : x0;
  int y1 = y0 + 1 < img.h ? y0 + 1 : y0;
  float fx = x - x0;
  float fy = y - y0;

  const uint8_t *row0 = img.data + (size_t)y0 * img.w;
  const uint8_t *row1 = img.data + (size_t)y1 * img.w;
  float a = row0[x0] + (row0[x1] - row0[x0]) * fx;
  float b = row1[x0] + (row1[x1] - row1[x0]) * fx;
  return a + (b - a) * fy;
}

// Rechnet eine Koordinate des rotationskorrigierten Bildes auf die Rohkoordinate
// zurueck (Drehung um die Bildmitte).
//
// Alle Koordinaten sind Pixel*indizes*: x = 0 ist die Mitte des ersten Pixels.
// Die Bildmitte liegt deshalb bei (w-1)/2, nicht bei w/2 - ein halbes Pixel
// Unterschied, das bei den Zeigern schon rund drei Grad Winkelfehler ausmacht.
static inline void unrotate(const GrayImage &src, float sinA, float cosA, float fx, float fy,
                            float *outX, float *outY) {
  float cx = (src.w - 1) * 0.5f;
  float cy = (src.h - 1) * 0.5f;
  float dx = fx - cx;
  float dy = fy - cy;
  *outX = cx + dx * cosA - dy * sinA;
  *outY = cy + dx * sinA + dy * cosA;
}

void extractRoi(const GrayImage &src, float rx, float ry, float rw, float rh, float rotDeg,
                uint8_t *dst, int dw, int dh) {
  float a = rotDeg * (float)M_PI / 180.0f;
  float sinA = sinf(a);
  float cosA = cosf(a);

  // Das ROI deckt die Pixel rx .. rx+rw-1 ab; sein Mittelpunkt liegt also bei
  // rx + (rw-1)/2 = rx - 0.5 + rw/2. Der Term -0.5 haelt Ein- und Ausgabe
  // deckungsgleich, statt alles um ein halbes Pixel zu verschieben.
  for (int j = 0; j < dh; j++) {
    float v = ry - 0.5f + (j + 0.5f) * rh / dh;
    for (int i = 0; i < dw; i++) {
      float u = rx - 0.5f + (i + 0.5f) * rw / dw;
      float sx, sy;
      unrotate(src, sinA, cosA, u, v, &sx, &sy);
      float p = sampleBilinear(src, sx, sy);
      dst[j * dw + i] = (uint8_t)clampT(p + 0.5f, 0.0f, 255.0f);
    }
  }
}

void rotateFrame(const GrayImage &src, uint8_t *dst, float rotDeg) {
  if (fabsf(rotDeg) < 0.01f) {
    memcpy(dst, src.data, (size_t)src.w * src.h);
    return;
  }
  float a = rotDeg * (float)M_PI / 180.0f;
  float sinA = sinf(a);
  float cosA = cosf(a);
  for (int y = 0; y < src.h; y++) {
    for (int x = 0; x < src.w; x++) {
      float sx, sy;
      unrotate(src, sinA, cosA, (float)x, (float)y, &sx, &sy);
      dst[(size_t)y * src.w + x] = (uint8_t)clampT(sampleBilinear(src, sx, sy) + 0.5f, 0.0f, 255.0f);
    }
  }
}

void normalizeContrast(uint8_t *buf, int n) {
  if (n <= 0) return;
  uint32_t hist[256];
  memset(hist, 0, sizeof(hist));
  for (int i = 0; i < n; i++) hist[buf[i]]++;

  uint32_t loCount = (uint32_t)(n * 0.02f);
  uint32_t hiCount = (uint32_t)(n * 0.02f);

  int lo = 0, hi = 255;
  uint32_t acc = 0;
  for (int v = 0; v < 256; v++) {
    acc += hist[v];
    if (acc > loCount) {
      lo = v;
      break;
    }
  }
  acc = 0;
  for (int v = 255; v >= 0; v--) {
    acc += hist[v];
    if (acc > hiCount) {
      hi = v;
      break;
    }
  }
  if (hi - lo < 8) return;  // zu flach - lieber gar nicht spreizen als Rauschen aufblasen

  float scale = 255.0f / (float)(hi - lo);
  for (int i = 0; i < n; i++) {
    float v = (buf[i] - lo) * scale;
    buf[i] = (uint8_t)clampT(v + 0.5f, 0.0f, 255.0f);
  }
}

bool isDarkOnLight(const uint8_t *buf, int w, int h) {
  // Mittelwert des 2 px breiten Randes gegen den Mittelwert der inneren Flaeche.
  double border = 0;
  int borderN = 0;
  double inner = 0;
  int innerN = 0;
  const int m = 2;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      bool isBorder = (x < m || y < m || x >= w - m || y >= h - m);
      if (isBorder) {
        border += buf[y * w + x];
        borderN++;
      } else {
        inner += buf[y * w + x];
        innerN++;
      }
    }
  }
  if (!borderN || !innerN) return true;
  return (border / borderN) > (inner / innerN);
}

void invertBuf(uint8_t *buf, int n) {
  for (int i = 0; i < n; i++) buf[i] = 255 - buf[i];
}

uint8_t otsuThreshold(const uint8_t *buf, int n) {
  uint32_t hist[256];
  memset(hist, 0, sizeof(hist));
  for (int i = 0; i < n; i++) hist[buf[i]]++;

  double sum = 0;
  for (int v = 0; v < 256; v++) sum += (double)v * hist[v];

  double sumB = 0;
  uint32_t wB = 0;
  double maxVar = -1;
  uint8_t best = 128;
  for (int t = 0; t < 256; t++) {
    wB += hist[t];
    if (!wB) continue;
    uint32_t wF = (uint32_t)n - wB;
    if (!wF) break;
    sumB += (double)t * hist[t];
    double mB = sumB / wB;
    double mF = (sum - sumB) / wF;
    double var = (double)wB * wF * (mB - mF) * (mB - mF);
    if (var > maxVar) {
      maxVar = var;
      best = (uint8_t)t;
    }
  }
  return best;
}

void meanStd(const uint8_t *buf, int n, float *mean, float *std) {
  double s = 0, s2 = 0;
  for (int i = 0; i < n; i++) {
    s += buf[i];
    s2 += (double)buf[i] * buf[i];
  }
  double m = s / n;
  double var = s2 / n - m * m;
  if (var < 0) var = 0;
  *mean = (float)m;
  *std = (float)sqrt(var);
}

float sharpness(const GrayImage &img) {
  // Varianz des Laplace-Filters, auf jedem 2. Pixel gerechnet - reicht als
  // relatives Mass und ist schnell genug fuer die Live-Vorschau.
  double sum = 0, sum2 = 0;
  long n = 0;
  for (int y = 1; y < img.h - 1; y += 2) {
    const uint8_t *r0 = img.data + (size_t)(y - 1) * img.w;
    const uint8_t *r1 = img.data + (size_t)y * img.w;
    const uint8_t *r2 = img.data + (size_t)(y + 1) * img.w;
    for (int x = 1; x < img.w - 1; x += 2) {
      int lap = 4 * r1[x] - r1[x - 1] - r1[x + 1] - r0[x] - r2[x];
      sum += lap;
      sum2 += (double)lap * lap;
      n++;
    }
  }
  if (n < 2) return 0;
  double m = sum / n;
  double var = sum2 / n - m * m;
  return var > 0 ? (float)var : 0.0f;
}
