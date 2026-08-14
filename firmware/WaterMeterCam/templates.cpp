#include "templates.h"

#include <LittleFS.h>

#include "default_templates.h"
#include "util.h"

static const char *TPL_PATH = "/templates.bin";
static const uint32_t TPL_MAGIC = 0x54504C31UL;  // "TPL1"

static uint8_t *s_data = nullptr;                        // [TPL_BANKS][NUM_CLASSES][TPL_PIX]
static uint16_t s_weight[TPL_BANKS][NUM_CLASSES] = {{0}};

static inline size_t tplOffset(uint8_t bank, uint8_t cls) {
  return ((size_t)bank * NUM_CLASSES + cls) * TPL_PIX;
}

static void loadDefaultInto(uint8_t bank, uint8_t cls) {
  memcpy_P(s_data + tplOffset(bank, cls), DEFAULT_TEMPLATES[cls], TPL_PIX);
  s_weight[bank][cls] = 0;
}

bool templatesInit() {
  size_t bytes = (size_t)TPL_BANKS * NUM_CLASSES * TPL_PIX;
  s_data = (uint8_t *)(psramFound() ? ps_malloc(bytes) : malloc(bytes));
  if (!s_data) {
    Serial.println("[tpl] Speicher fuer Vorlagen reicht nicht");
    return false;
  }
  templatesResetAll();
  templatesLoad();  // ueberschreibt, was schon angelernt wurde
  return true;
}

uint8_t templateBankFor(uint8_t digitIndex) {
  if (cfg.sharedTemplates) return TPL_SHARED_BANK;
  return digitIndex < MAX_DIGITS ? digitIndex : 0;
}

const uint8_t *templateData(uint8_t bank, uint8_t cls) {
  if (!s_data || bank >= TPL_BANKS || cls >= NUM_CLASSES) return nullptr;
  return s_data + tplOffset(bank, cls);
}

uint16_t templateWeight(uint8_t bank, uint8_t cls) {
  if (bank >= TPL_BANKS || cls >= NUM_CLASSES) return 0;
  return s_weight[bank][cls];
}

void templateLearn(uint8_t bank, uint8_t cls, const uint8_t *patch) {
  if (!s_data || bank >= TPL_BANKS || cls >= NUM_CLASSES || !patch) return;
  uint8_t *dst = s_data + tplOffset(bank, cls);
  uint16_t w = s_weight[bank][cls];

  if (w == 0) {
    // Erste echte Aufnahme: die generische Schriftvorlage ist schlechter als
    // jedes Bild vom eigenen Zaehler, also komplett ersetzen.
    memcpy(dst, patch, TPL_PIX);
    s_weight[bank][cls] = 1;
    return;
  }

  uint16_t n = w < TPL_MAX_WEIGHT ? w : TPL_MAX_WEIGHT;
  for (int i = 0; i < TPL_PIX; i++) {
    dst[i] = (uint8_t)(((uint32_t)dst[i] * n + patch[i]) / (n + 1));
  }
  if (s_weight[bank][cls] < 0xFFFF) s_weight[bank][cls]++;
}

void templateResetClass(uint8_t bank, uint8_t cls) {
  if (!s_data || bank >= TPL_BANKS || cls >= NUM_CLASSES) return;
  loadDefaultInto(bank, cls);
}

void templatesResetAll() {
  if (!s_data) return;
  for (uint8_t b = 0; b < TPL_BANKS; b++) {
    for (uint8_t c = 0; c < NUM_CLASSES; c++) loadDefaultInto(b, c);
  }
}

uint8_t templatesLearnedCount(uint8_t bank) {
  if (bank >= TPL_BANKS) return 0;
  uint8_t n = 0;
  for (uint8_t c = 0; c < NUM_CLASSES; c++)
    if (s_weight[bank][c] > 0) n++;
  return n;
}

struct TplHeader {
  uint32_t magic;
  uint16_t banks;
  uint16_t classes;
  uint16_t pixels;
  uint16_t reserved;
  uint32_t crc;
};

bool templatesSave() {
  if (!s_data) return false;

  size_t dataBytes = (size_t)TPL_BANKS * NUM_CLASSES * TPL_PIX;
  TplHeader h;
  h.magic = TPL_MAGIC;
  h.banks = TPL_BANKS;
  h.classes = NUM_CLASSES;
  h.pixels = TPL_PIX;
  h.reserved = 0;
  h.crc = crc32Update(crc32(s_data, dataBytes), (const uint8_t *)s_weight, sizeof(s_weight));

  File f = LittleFS.open("/templates.tmp", "w");
  if (!f) return false;
  bool ok = f.write((const uint8_t *)&h, sizeof(h)) == sizeof(h) &&
            f.write(s_data, dataBytes) == dataBytes &&
            f.write((const uint8_t *)s_weight, sizeof(s_weight)) == sizeof(s_weight);
  f.close();
  if (!ok) {
    LittleFS.remove("/templates.tmp");
    return false;
  }
  LittleFS.remove(TPL_PATH);
  return LittleFS.rename("/templates.tmp", TPL_PATH);
}

bool templatesLoad() {
  if (!s_data) return false;
  File f = LittleFS.open(TPL_PATH, "r");
  if (!f) return false;

  TplHeader h;
  if (f.read((uint8_t *)&h, sizeof(h)) != sizeof(h)) {
    f.close();
    return false;
  }
  if (h.magic != TPL_MAGIC || h.banks != TPL_BANKS || h.classes != NUM_CLASSES ||
      h.pixels != TPL_PIX) {
    Serial.println("[tpl] Datei passt nicht zum Build - Vorlagen bleiben Standard");
    f.close();
    return false;
  }

  size_t dataBytes = (size_t)TPL_BANKS * NUM_CLASSES * TPL_PIX;
  uint8_t *tmp = (uint8_t *)(psramFound() ? ps_malloc(dataBytes) : malloc(dataBytes));
  if (!tmp) {
    f.close();
    return false;
  }
  uint16_t weights[TPL_BANKS][NUM_CLASSES];
  bool ok = f.read(tmp, dataBytes) == (int)dataBytes &&
            f.read((uint8_t *)weights, sizeof(weights)) == (int)sizeof(weights);
  f.close();

  if (ok) {
    uint32_t crc = crc32Update(crc32(tmp, dataBytes), (const uint8_t *)weights, sizeof(weights));
    ok = (crc == h.crc);
    if (!ok) Serial.println("[tpl] CRC-Fehler - Vorlagen bleiben Standard");
  }
  if (ok) {
    memcpy(s_data, tmp, dataBytes);
    memcpy(s_weight, weights, sizeof(weights));
    Serial.println("[tpl] geladen");
  }
  free(tmp);
  return ok;
}
