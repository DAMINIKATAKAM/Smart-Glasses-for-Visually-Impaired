/*
 * ESP32-CAM + OLED Display + Buzzer Object Detection (Edge Impulse)
 * Modified by Shanthala — based on Edge Impulse Example
 * Tested on AI-Thinker module (with PSRAM)
 * 
 * Connect OLED: 
 *   SDA → GPIO15
 *   SCL → GPIO14
 *   VCC → 3.3V
 *   GND → GND
 * Connect Buzzer:
 *   + → GPIO12
 *   - → GND
 */

#include <objectdetection_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"
#include "esp_camera.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

/* CAMERA CONFIGURATION --------------------------------------------------- */
#define CAMERA_MODEL_AI_THINKER  // Has PSRAM

#if defined(CAMERA_MODEL_AI_THINKER)
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22
#else
#error "Camera model not selected"
#endif

/* OLED CONFIGURATION --------------------------------------------------- */
#define I2C_SDA 15
#define I2C_SCL 14
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

TwoWire I2Cbus = TwoWire(0);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &I2Cbus, OLED_RESET);

/* BUZZER CONFIGURATION -------------------------------------------------- */
#define BUZZER_PIN 12  // Active buzzer pin

/* CONSTANTS ------------------------------------------------------------- */
#define EI_CAMERA_RAW_FRAME_BUFFER_COLS 320
#define EI_CAMERA_RAW_FRAME_BUFFER_ROWS 240
#define EI_CAMERA_FRAME_BYTE_SIZE 3

/* VARIABLES -------------------------------------------------------------- */
static bool debug_nn = false;
static bool is_initialised = false;
uint8_t *snapshot_buf;

static camera_config_t camera_config = {
    .pin_pwdn = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,
    .pin_xclk = XCLK_GPIO_NUM,
    .pin_sscb_sda = SIOD_GPIO_NUM,
    .pin_sscb_scl = SIOC_GPIO_NUM,
    .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM,
    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_QVGA,
    .jpeg_quality = 12,
    .fb_count = 1,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
};

/* FUNCTION DECLARATIONS -------------------------------------------------- */
bool ei_camera_init(void);
bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf);
void ei_camera_deinit(void);
static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr);

/* SETUP ------------------------------------------------------------------ */
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize OLED
  I2Cbus.begin(I2C_SDA, I2C_SCL, 100000);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("OLED init failed. Check wiring!");
    while (true);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Initializing...");
  display.display();

  // Initialize Camera
  if (!ei_camera_init()) {
    Serial.println("Camera init failed!");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Camera failed!");
    display.display();
    while (true);
  }
  Serial.println("Camera initialized");
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Camera OK!");
  display.display();

  // Initialize buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  ei_printf("Starting inference...\n");
  delay(2000);
}

/* MAIN LOOP -------------------------------------------------------------- */
void loop() {
  if (ei_sleep(5) != EI_IMPULSE_OK) return;

  snapshot_buf = (uint8_t *)malloc(EI_CAMERA_RAW_FRAME_BUFFER_COLS * EI_CAMERA_RAW_FRAME_BUFFER_ROWS * EI_CAMERA_FRAME_BYTE_SIZE);
  if (!snapshot_buf) {
    ei_printf("Failed to allocate snapshot buffer!\n");
    return;
  }

  if (!ei_camera_capture(EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT, snapshot_buf)) {
    ei_printf("Capture failed\n");
    free(snapshot_buf);
    return;
  }

  ei::signal_t signal;
  signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
  signal.get_data = &ei_camera_get_data;

  ei_impulse_result_t result = {0};
  EI_IMPULSE_ERROR err = run_classifier(&signal, &result, debug_nn);
  if (err != EI_IMPULSE_OK) {
    ei_printf("Classifier failed (%d)\n", err);
    free(snapshot_buf);
    return;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

#if EI_CLASSIFIER_OBJECT_DETECTION == 1
  bool object_found = false;
  for (uint32_t i = 0; i < result.bounding_boxes_count; i++) {
    auto bb = result.bounding_boxes[i];
    if (bb.value == 0) continue;
    object_found = true;

    Serial.printf("%s (%.2f) [x:%u, y:%u, w:%u, h:%u]\n",
                  bb.label, bb.value, bb.x, bb.y, bb.width, bb.height);

    display.setCursor(0, i * 10);
    display.print(bb.label);
    display.print(": ");
    display.print(int(bb.value * 100));
    display.print("%");
  }

  if (object_found) {
    // 🔔 Buzz for detected object
    digitalWrite(BUZZER_PIN, HIGH);
    delay(300);
    digitalWrite(BUZZER_PIN, LOW);
  } else {
    Serial.println("No objects found");
    display.setCursor(0, 0);
    display.print("No objects found");
    digitalWrite(BUZZER_PIN, LOW);
  }

#else
  for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    Serial.printf("%s: %.5f\n", result.classification[i].label, result.classification[i].value);
    display.setCursor(0, i * 10);
    display.print(result.classification[i].label);
    display.print(": ");
    display.print(int(result.classification[i].value * 100));
    display.print("%");
  }
#endif

  display.display();
  free(snapshot_buf);
}

/* CAMERA FUNCTIONS ------------------------------------------------------- */
bool ei_camera_init(void) {
  if (is_initialised) return true;
  esp_err_t err = esp_camera_init(&camera_config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, 0);
  }

  is_initialised = true;
  return true;
}

bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf) {
  if (!is_initialised) return false;
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return false;

  bool converted = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, snapshot_buf);
  esp_camera_fb_return(fb);
  if (!converted) return false;

  if (img_width != EI_CAMERA_RAW_FRAME_BUFFER_COLS || img_height != EI_CAMERA_RAW_FRAME_BUFFER_ROWS) {
    ei::image::processing::crop_and_interpolate_rgb888(
        out_buf,
        EI_CAMERA_RAW_FRAME_BUFFER_COLS,
        EI_CAMERA_RAW_FRAME_BUFFER_ROWS,
        out_buf,
        img_width,
        img_height);
  }

  return true;
}

static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr) {
  size_t pixel_ix = offset * 3;
  size_t pixels_left = length;
  size_t out_ptr_ix = 0;
  while (pixels_left--) {
    out_ptr[out_ptr_ix++] = (snapshot_buf[pixel_ix + 2] << 16) |
                            (snapshot_buf[pixel_ix + 1] << 8) |
                            snapshot_buf[pixel_ix];
    pixel_ix += 3;
  }
  return 0;
}

void ei_camera_deinit(void) {
  esp_camera_deinit();
  is_initialised = false;
}
