// Host-Testlauf der Erkennung.
//
// Laedt ein synthetisches Zaehlerbild (PGM, erzeugt von make_test_image.py),
// laesst die echte Firmware-Logik darauf laufen und vergleicht das Ergebnis
// mit dem bekannten Sollwert. Ohne ESP32, ohne Kamera.
//
// Uebersetzen und starten: ./tools/hostcheck/run.sh

#include <LittleFS.h>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "config.h"
#include "imgproc.h"
#include "meter.h"
#include "recognize.h"
#include "templates.h"

// --- Shim-Globals ---------------------------------------------------------

SerialStub Serial;
EspStub ESP;
LittleFSStub LittleFS;
std::string LittleFSStub::root = "/tmp/wmc_hostfs";

unsigned long g_millis = 0;
unsigned long millis() { return g_millis; }

// --- PGM laden ------------------------------------------------------------

struct Pgm {
  std::vector<uint8_t> data;
  int w = 0, h = 0;
};

static bool loadPgm(const char *path, Pgm &out) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    printf("kann %s nicht oeffnen\n", path);
    return false;
  }
  char magic[3] = {0};
  if (fscanf(f, "%2s", magic) != 1 || strcmp(magic, "P5") != 0) {
    printf("%s ist kein binaeres PGM\n", path);
    fclose(f);
    return false;
  }
  int vals[3], got = 0;
  while (got < 3) {
    int c = fgetc(f);
    if (c == '#') {
      while (c != '\n' && c != EOF) c = fgetc(f);
      continue;
    }
    if (isspace(c)) continue;
    ungetc(c, f);
    if (fscanf(f, "%d", &vals[got]) != 1) {
      fclose(f);
      return false;
    }
    got++;
  }
  fgetc(f);  // genau ein Whitespace nach maxval
  out.w = vals[0];
  out.h = vals[1];
  out.data.resize((size_t)out.w * out.h);
  size_t n = fread(out.data.data(), 1, out.data.size(), f);
  fclose(f);
  return n == out.data.size();
}

// --- Testaufbau -----------------------------------------------------------

// Muss mit make_test_image.py uebereinstimmen.
static void setupGeometry() {
  configSetDefaults(cfg);
  cfg.digitCount = 5;
  for (int i = 0; i < 5; i++) {
    cfg.digits[i].x = 55 + i * 105;
    cfg.digits[i].y = 120;
    cfg.digits[i].w = 72;
    cfg.digits[i].h = 100;
  }
  cfg.decimalDigits = 0;
  cfg.dialCount = 3;
  for (int i = 0; i < 3; i++) {
    cfg.dials[i].roi.x = 90 + i * 150;
    cfg.dials[i].roi.y = 340;
    cfg.dials[i].roi.w = 110;
    cfg.dials[i].roi.h = 110;
    cfg.dials[i].offsetDeg = 0;
    cfg.dials[i].clockwise = 1;
  }
  cfg.rollDirection = 1;
  cfg.shiftSearch = MAX_SHIFT;
  cfg.polarity = 1;  // dunkle Ziffer auf heller Trommel
  cfg.sharedTemplates = 1;
  cfg.minConfidence = 0;
  cfg.mqttEnabled = 0;
}

static int g_pass = 0, g_fail = 0, g_refused = 0;

// Die Mindestkonfidenz, ab der die Firmware einen Wert uebernehmen wuerde.
// Ein falsches Ergebnis *unterhalb* dieser Schwelle ist kein Fehler - genau
// dafuer gibt es die Schwelle. Ein falsches Ergebnis darueber schon.
static const uint8_t ACCEPT_THRESHOLD = 35;

static void check(const char *what, const std::string &got, const std::string &want,
                  uint8_t conf) {
  if (got == want) {
    printf("  OK        %-22s %s\n", what, got.c_str());
    g_pass++;
  } else if (conf < ACCEPT_THRESHOLD) {
    printf("  VERWORFEN %-22s erwartet %s, gelesen %s bei nur %u%% - wird abgelehnt\n", what,
           want.c_str(), got.c_str(), conf);
    g_refused++;
  } else {
    printf("  FEHL      %-22s erwartet %s, gelesen %s bei %u%%\n", what, want.c_str(), got.c_str(),
           conf);
    g_fail++;
  }
}

// Mit fuehrenden Nullen, damit der Vergleich genau so aussieht wie der am
// Zaehler abgelesene Stand.
static std::string fmtValue(double v) {
  int dec = configDecimals();
  int width = cfg.digitCount + (dec ? 1 + dec : 0);
  char b[48];
  snprintf(b, sizeof(b), "%0*.*f", width, dec, v);
  return b;
}

static bool runOn(const char *pgm, std::string *outValue, uint8_t *outConf) {
  Pgm img;
  if (!loadPgm(pgm, img)) return false;
  GrayImage g = {img.data.data(), img.w, img.h};
  bool ok = recognizeRun(g);
  Recognition *r = recognizeResult();
  if (outValue) *outValue = ok ? fmtValue(r->rawValue) : std::string("-");
  if (outConf) *outConf = r->confidence;
  return ok;
}

static void dumpDetail() {
  Recognition *r = recognizeResult();
  printf("       Rollen:");
  for (int i = 0; i < r->digitCount; i++)
    printf(" %d(%.2f/%u%%)", r->digits[i].digit, r->digits[i].score, r->digits[i].confidence);
  printf("\n       Zeiger:");
  for (int i = 0; i < r->dialCount; i++)
    printf(" %d(%.0f°/%u%%)", r->dials[i].digit, r->dials[i].angleDeg, r->dials[i].confidence);
  printf("\n");
}

int main(int argc, char **argv) {
  if (argc < 4) {
    printf("Aufruf: %s <bild.pgm> <sollwert> <drehung> [weitere Tripel ...]\n", argv[0]);
    return 2;
  }
  if (system(("mkdir -p " + LittleFSStub::root).c_str()) ||
      system(("rm -f " + LittleFSStub::root + "/*").c_str())) {
    printf("Testverzeichnis konnte nicht vorbereitet werden\n");
    return 1;
  }

  setupGeometry();
  if (!templatesInit() || !recognizeInit()) {
    printf("Initialisierung fehlgeschlagen\n");
    return 1;
  }

  for (int a = 1; a + 2 < argc; a += 3) {
    const char *pgm = argv[a];
    const char *want = argv[a + 1];
    cfg.rotationDeg = (float)atof(argv[a + 2]);

    // Jeder Fall startet mit den eingebauten Vorlagen, damit das Anlernen des
    // vorherigen Falls das Ergebnis nicht schoenrechnet.
    templatesResetAll();

    printf("\n%s (soll %s, Drehung %.1f°)\n", pgm, want, cfg.rotationDeg);

    std::string got;
    uint8_t conf = 0;
    if (!runOn(pgm, &got, &conf)) {
      printf("  FEHL      Erkennung brach ab: %s\n", recognizeResult()->error);
      g_fail++;
      continue;
    }
    dumpDetail();
    check("mit Standardvorlagen", got, want, conf);

    // Anlernen und danach erneut messen. Das ist der Zustand, in dem das
    // Geraet im Betrieb laeuft - hier muss das Ergebnis stimmen.
    Pgm img;
    if (loadPgm(pgm, img)) {
      GrayImage g = {img.data.data(), img.w, img.h};
      String report;
      recognizeLearnFromReading(g, atof(want), report);
      printf("       anlernen: %s\n", report.c_str());
      std::string after;
      uint8_t confAfter = 0;
      if (runOn(pgm, &after, &confAfter)) {
        check("nach dem Anlernen", after, want, confAfter);
        printf("       Konfidenz %u%% -> %u%%\n", conf, confAfter);
      }
    }
  }

  printf("\n%d richtig, %d korrekt verworfen, %d falsch\n", g_pass, g_refused, g_fail);
  return g_fail ? 1 : 0;
}
