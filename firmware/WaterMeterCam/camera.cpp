#include "camera.h"

#include "camera_pins.h"
#include "config.h"

// Die LED wird per PWM angesteuert. Volle Helligkeit ueberstrahlt die
// Zifferblattfolie und macht die Erkennung schlechter, nicht besser.
#define FLASH_PWM_CHANNEL 7
#define FLASH_PWM_FREQ 5000
#define FLASH_PWM_BITS 8

static bool s_cameraReady = false;
static bool s_flashReady = false;

bool cameraHasFlash() { return LED_GPIO_NUM >= 0; }

static void flashInit() {
  if (LED_GPIO_NUM < 0) return;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  s_flashReady = ledcAttach(LED_GPIO_NUM, FLASH_PWM_FREQ, FLASH_PWM_BITS);
#else
  ledcSetup(FLASH_PWM_CHANNEL, FLASH_PWM_FREQ, FLASH_PWM_BITS);
  ledcAttachPin(LED_GPIO_NUM, FLASH_PWM_CHANNEL);
  s_flashReady = true;
#endif
  flashSet(0);
}

void flashSet(uint8_t duty) {
  if (LED_GPIO_NUM < 0 || !s_flashReady) return;
#if !LED_ACTIVE_HIGH
  duty = 255 - duty;
#endif
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(LED_GPIO_NUM, duty);
#else
  ledcWrite(FLASH_PWM_CHANNEL, duty);
#endif
}

bool cameraInit() {
  camera_config_t c;
  memset(&c, 0, sizeof(c));

  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;
  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;
  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;
  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;
  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM;
  c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM;
  c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM;
  c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM;
  c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;

  // Durchgaengig Graustufen: ein Farbbild bringt fuer die Erkennung nichts,
  // kostet aber dreimal so viel Speicher. Die Vorschau fuer die Web-UI wird
  // per fmt2jpg() aus demselben Puffer erzeugt.
  c.pixel_format = PIXFORMAT_GRAYSCALE;
  c.frame_size = FRAMESIZE_VGA;

  if (psramFound()) {
    c.fb_location = CAMERA_FB_IN_PSRAM;
    c.fb_count = 2;
    c.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    // Ohne PSRAM passt VGA graustufig (300 KB) nicht in den DRAM.
    Serial.println("[cam] WARNUNG: kein PSRAM gefunden - fallback auf QVGA");
    c.frame_size = FRAMESIZE_QVGA;
    c.fb_location = CAMERA_FB_IN_DRAM;
    c.fb_count = 1;
    c.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("[cam] init fehlgeschlagen: 0x%x\n", err);
    return false;
  }

  s_cameraReady = true;
  flashInit();
  cameraApplySettings();
  Serial.println("[cam] bereit");
  return true;
}

void cameraApplySettings() {
  if (!s_cameraReady) return;
  sensor_t *s = esp_camera_sensor_get();
  if (!s) return;

  s->set_brightness(s, cfg.brightness);
  s->set_contrast(s, cfg.contrast);
  s->set_saturation(s, cfg.saturation);
  s->set_vflip(s, cfg.vflip ? 1 : 0);
  s->set_hmirror(s, cfg.hmirror ? 1 : 0);

  // Weissabgleich und Spezialeffekte stoeren nur.
  s->set_special_effect(s, 0);
  s->set_whitebal(s, 1);
  s->set_awb_gain(s, 1);
  s->set_wb_mode(s, 0);
  s->set_lenc(s, 1);   // Vignettierungskorrektur - hilft am Bildrand
  s->set_bpc(s, 1);
  s->set_wpc(s, 1);
  s->set_raw_gma(s, 1);
  s->set_dcw(s, 1);
  s->set_colorbar(s, 0);

  if (cfg.autoExposure) {
    s->set_exposure_ctrl(s, 1);
    s->set_aec2(s, 1);
    s->set_ae_level(s, cfg.aeLevel);
    s->set_gain_ctrl(s, 1);
    s->set_gainceiling(s, GAINCEILING_4X);  // hohe Gains rauschen zu stark
  } else {
    // Feste Belichtung liefert von Messung zu Messung identische Bilder -
    // genau das, was der Template-Abgleich braucht.
    s->set_exposure_ctrl(s, 0);
    s->set_aec2(s, 0);
    s->set_aec_value(s, cfg.manualExposure);
    s->set_gain_ctrl(s, 0);
    s->set_agc_gain(s, cfg.agcGain);
  }
}

camera_fb_t *cameraGrab() {
  if (!s_cameraReady) return nullptr;

  flashSet(cfg.flashBrightness);
  if (cfg.flashBrightness > 0) delay(120);  // LED und Belichtung einschwingen lassen

  // Alte Puffer verwerfen: der Treiber haelt sonst ein Bild von vor dem
  // Einschalten der LED bereit.
  uint8_t warmup = cfg.warmupFrames;
  if (warmup > 10) warmup = 10;
  for (uint8_t i = 0; i < warmup; i++) {
    camera_fb_t *tmp = esp_camera_fb_get();
    if (tmp) esp_camera_fb_return(tmp);
  }

  camera_fb_t *fb = esp_camera_fb_get();
  flashSet(0);

  if (!fb) {
    Serial.println("[cam] Aufnahme fehlgeschlagen");
    return nullptr;
  }
  if (fb->format != PIXFORMAT_GRAYSCALE) {
    Serial.println("[cam] unerwartetes Pixelformat");
    esp_camera_fb_return(fb);
    return nullptr;
  }
  return fb;
}

void cameraRelease(camera_fb_t *fb) {
  if (fb) esp_camera_fb_return(fb);
}
