#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ================= Pins (ESP8266 / NodeMCU) =================
constexpr uint8_t PIN_ENC_A = D5;   // GPIO14 (CLK)
constexpr uint8_t PIN_ENC_B = D6;   // GPIO12 (DT)
constexpr uint8_t PIN_BTN   = D7;   // GPIO13 (SW, to GND when pressed)

constexpr uint8_t I2C_SDA = D2;     // GPIO4
constexpr uint8_t I2C_SCL = D1;     // GPIO5

// ================= OLED =================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ================= Menu (fixed) =================
const char* MENU_ITEMS[] = {"Status", "Settings", "About", "Restart", "WiFi", "Debug"};
constexpr int MENU_LEN = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);
int cursor = 0;
int windowStart = 0;

// ================= UI constants =================
constexpr int TITLE_H = 12;       // title bar height
constexpr int LINE_H  = 10;       // line height below title
constexpr int MAX_LINES = (SCREEN_HEIGHT - TITLE_H) / LINE_H;  // 5 lines
constexpr int PAD_X = 2;

// ================= Rotary (interrupts) =================
volatile int32_t enc_delta = 0;
volatile uint8_t enc_state = 0;

static const int8_t QDEC_LUT[16] = {
  0, -1, +1,  0,
  +1,  0,  0, -1,
  -1,  0,  0, +1,
   0, +1, -1,  0
};

constexpr int STEPS_PER_DETENT = 4;  // adjust to encoder

IRAM_ATTR static inline uint8_t readAB() {
  return (digitalRead(PIN_ENC_A) << 1) | digitalRead(PIN_ENC_B);
}

IRAM_ATTR void encoderISR() {
  uint8_t newAB = readAB();
  uint8_t idx = ((enc_state & 0x03) << 2) | (newAB & 0x03);
  int8_t step = QDEC_LUT[idx];
  enc_state = newAB & 0x03;
  if (step) enc_delta += step;
}

int32_t fetchDelta() {
  noInterrupts();
  int32_t d = enc_delta;
  enc_delta = 0;
  interrupts();
  return d;
}

// ================= Button (simple debounce) =================
bool btnPrev = true;              
uint32_t btnLastChangeMs = 0;
constexpr uint16_t BTN_DEBOUNCE_MS = 30;

// ================= Drawing =================
void drawMenu() {
  display.clearDisplay();

  // --- Title bar ---
  display.fillRect(0, 0, SCREEN_WIDTH, TITLE_H, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 2);
  display.print(F("Main Menu"));

  // --- Menu items ---
  display.setTextColor(SSD1306_WHITE);
  for (int i = 0; i < MAX_LINES; i++) {
    int idx = windowStart + i;
    if (idx >= MENU_LEN) break;
    int y = TITLE_H + i * LINE_H;

    if (idx == cursor) {
      display.fillRect(0, y, SCREEN_WIDTH, LINE_H, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(PAD_X, y + 2);
    display.print(MENU_ITEMS[idx]);
  }

  // --- Scrollbar (right edge) ---
  if (MENU_LEN > MAX_LINES) {
    int barArea = SCREEN_HEIGHT - TITLE_H;
    int barH = max(8, (int)((float)MAX_LINES / MENU_LEN * barArea));
    float pct = (float)windowStart / max(1, MENU_LEN - MAX_LINES);
    int barY = TITLE_H + (int)(pct * (barArea - barH));

    display.drawRect(SCREEN_WIDTH - 3, TITLE_H, 3, barArea, SSD1306_WHITE);
    display.fillRect(SCREEN_WIDTH - 3, barY, 3, barH, SSD1306_WHITE);
  }

  display.display();
}

void confirmSelection(const char* text) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Selected:"));
  display.println(text);
  display.display();

  if (strcmp(text, "Restart") == 0) {
    display.println(F("\nRestarting..."));
    display.display();
    delay(500);
    // ESP.restart();
  }

  delay(700);
  drawMenu();
}

// ================= Setup / Loop =================
void setup() {
  Serial.begin(115200);
  delay(100);

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("SSD1306 init failed"));
    for(;;) yield();
  }

  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  pinMode(PIN_BTN, INPUT_PULLUP);

  enc_state = readAB();
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encoderISR, CHANGE);

  drawMenu();
}

void loop() {
  // --- Rotary ---
  int32_t d = fetchDelta();
  if (d) {
    static int32_t accum = 0;
    accum += d;

    while (accum >= STEPS_PER_DETENT) { accum -= STEPS_PER_DETENT; cursor = min(cursor + 1, MENU_LEN - 1); }
    while (accum <= -STEPS_PER_DETENT){ accum += STEPS_PER_DETENT; cursor = max(cursor - 1, 0); }

    if (cursor < windowStart) windowStart = cursor;
    else if (cursor >= windowStart + MAX_LINES) windowStart = cursor - (MAX_LINES - 1);

    drawMenu();
  }

  // --- Button ---
  bool btnNow = digitalRead(PIN_BTN);
  uint32_t now = millis();
  if (btnNow != btnPrev && (now - btnLastChangeMs) > BTN_DEBOUNCE_MS) {
    btnLastChangeMs = now;
    btnPrev = btnNow;
    if (!btnNow) { // pressed
      confirmSelection(MENU_ITEMS[cursor]);
    }
  }

  yield();
}
