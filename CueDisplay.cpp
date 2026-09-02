#include "CueDisplay.h"

#include "CueIO.h"
#include "CueLora.h"
#include "PeerSync.h"
#include "config.h"

#if !CUE_HAS_OLED

void cueDisplayBegin() {}
void cueDisplayRefresh() {}
void cueDisplaySplash() {}
void cueDisplayShowOff() {}
void cueDisplayShowWifiWiped() {}
void cueDisplayPowerDown() {}

#else

#include <Wire.h>
#include <WiFi.h>

namespace {

constexpr uint8_t kWidth = 128;
constexpr uint8_t kPages = 8;
constexpr uint16_t kBufSize = 1024;

uint8_t g_buf[kBufSize];
bool g_ready = false;

// 5x7 ASCII 0x20-0x5A (space through Z). Column-major, LSB = top.
const uint8_t kFont[][5] PROGMEM = {
    {0x00, 0x00, 0x00, 0x00, 0x00},  // space
    {0x00, 0x00, 0x5F, 0x00, 0x00},  // !
    {0x00, 0x07, 0x00, 0x07, 0x00},  // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14},  // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12},  // $
    {0x23, 0x13, 0x08, 0x64, 0x62},  // %
    {0x36, 0x49, 0x55, 0x22, 0x50},  // &
    {0x00, 0x05, 0x03, 0x00, 0x00},  // '
    {0x00, 0x1C, 0x22, 0x41, 0x00},  // (
    {0x00, 0x41, 0x22, 0x1C, 0x00},  // )
    {0x08, 0x2A, 0x1C, 0x2A, 0x08},  // *
    {0x08, 0x08, 0x3E, 0x08, 0x08},  // +
    {0x00, 0x50, 0x30, 0x00, 0x00},  // ,
    {0x08, 0x08, 0x08, 0x08, 0x08},  // -
    {0x00, 0x60, 0x60, 0x00, 0x00},  // .
    {0x20, 0x10, 0x08, 0x04, 0x02},  // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E},  // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00},  // 1
    {0x42, 0x61, 0x51, 0x49, 0x46},  // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31},  // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10},  // 4
    {0x27, 0x45, 0x45, 0x45, 0x39},  // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30},  // 6
    {0x01, 0x71, 0x09, 0x05, 0x03},  // 7
    {0x36, 0x49, 0x49, 0x49, 0x36},  // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E},  // 9
    {0x00, 0x36, 0x36, 0x00, 0x00},  // :
    {0x00, 0x56, 0x36, 0x00, 0x00},  // ;
    {0x00, 0x08, 0x14, 0x22, 0x41},  // <
    {0x14, 0x14, 0x14, 0x14, 0x14},  // =
    {0x41, 0x22, 0x14, 0x08, 0x00},  // >
    {0x02, 0x01, 0x51, 0x09, 0x06},  // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E},  // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E},  // A
    {0x7F, 0x49, 0x49, 0x49, 0x36},  // B
    {0x3E, 0x41, 0x41, 0x41, 0x22},  // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C},  // D
    {0x7F, 0x49, 0x49, 0x49, 0x41},  // E
    {0x7F, 0x09, 0x09, 0x09, 0x01},  // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A},  // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F},  // H
    {0x00, 0x41, 0x7F, 0x41, 0x00},  // I
    {0x20, 0x40, 0x41, 0x3F, 0x01},  // J
    {0x7F, 0x08, 0x14, 0x22, 0x41},  // K
    {0x7F, 0x40, 0x40, 0x40, 0x40},  // L
    {0x7F, 0x02, 0x04, 0x02, 0x7F},  // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F},  // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E},  // O
    {0x7F, 0x09, 0x09, 0x09, 0x06},  // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E},  // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46},  // R
    {0x46, 0x49, 0x49, 0x49, 0x31},  // S
    {0x01, 0x01, 0x7F, 0x01, 0x01},  // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F},  // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F},  // V
    {0x7F, 0x20, 0x18, 0x20, 0x7F},  // W
    {0x63, 0x14, 0x08, 0x14, 0x63},  // X
    {0x03, 0x04, 0x78, 0x04, 0x03},  // Y
    {0x61, 0x51, 0x49, 0x45, 0x43},  // Z
};

void sendCmd(uint8_t cmd) {
  Wire.beginTransmission(OLED_I2C_ADDR);
  Wire.write(0x00);
  Wire.write(cmd);
  Wire.endTransmission();
}

void setPixel(int x, int y, bool on) {
  if (x < 0 || x >= kWidth || y < 0 || y >= 64) {
    return;
  }
  const uint16_t i = (uint16_t)x + ((uint16_t)(y / 8) * kWidth);
  const uint8_t bit = 1u << (y & 7);
  if (on) {
    g_buf[i] |= bit;
  } else {
    g_buf[i] &= ~bit;
  }
}

void fillRect(int x, int y, int w, int h, bool on) {
  for (int yy = y; yy < y + h; ++yy) {
    for (int xx = x; xx < x + w; ++xx) {
      setPixel(xx, yy, on);
    }
  }
}

void drawRect(int x, int y, int w, int h, bool on) {
  for (int i = 0; i < w; ++i) {
    setPixel(x + i, y, on);
    setPixel(x + i, y + h - 1, on);
  }
  for (int i = 0; i < h; ++i) {
    setPixel(x, y + i, on);
    setPixel(x + w - 1, y + i, on);
  }
}

void drawChar(int x, int y, char c, bool on) {
  if (c >= 'a' && c <= 'z') {
    c = (char)(c - 32);
  }
  if (c < 0x20 || c > 0x5A) {
    c = '?';
  }
  const uint8_t* glyph = kFont[c - 0x20];
  for (uint8_t col = 0; col < 5; ++col) {
    const uint8_t bits = pgm_read_byte(&glyph[col]);
    for (uint8_t row = 0; row < 7; ++row) {
      if (bits & (1u << row)) {
        setPixel(x + col, y + row, on);
      }
    }
  }
}

void drawText(int x, int y, const char* text, bool on) {
  while (*text) {
    drawChar(x, y, *text, on);
    x += 6;
    ++text;
  }
}

void flush() {
  sendCmd(0x21);
  sendCmd(0);
  sendCmd(kWidth - 1);
  sendCmd(0x22);
  sendCmd(0);
  sendCmd(kPages - 1);

  for (uint16_t i = 0; i < kBufSize; i += 16) {
    Wire.beginTransmission(OLED_I2C_ADDR);
    Wire.write(0x40);
    Wire.write(&g_buf[i], 16);
    Wire.endTransmission();
  }
}

void clear() { memset(g_buf, 0, sizeof(g_buf)); }

bool initOled() {
  pinMode(PIN_VEXT, OUTPUT);
  digitalWrite(PIN_VEXT, LOW);
  delay(80);

  pinMode(PIN_OLED_RST, OUTPUT);
  digitalWrite(PIN_OLED_RST, LOW);
  delay(20);
  digitalWrite(PIN_OLED_RST, HIGH);
  delay(20);

  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  Wire.setClock(400000);

  Wire.beginTransmission(OLED_I2C_ADDR);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  sendCmd(0xAE);
  sendCmd(0xD5);
  sendCmd(0x80);
  sendCmd(0xA8);
  sendCmd(0x3F);
  sendCmd(0xD3);
  sendCmd(0x00);
  sendCmd(0x40);
  sendCmd(0x8D);
  sendCmd(0x14);
  sendCmd(0x20);
  sendCmd(0x00);
  sendCmd(0xA1);
  sendCmd(0xC8);
  sendCmd(0xDA);
  sendCmd(0x12);
  sendCmd(0x81);
  sendCmd(0xCF);
  sendCmd(0xD9);
  sendCmd(0xF1);
  sendCmd(0xDB);
  sendCmd(0x40);
  sendCmd(0xA4);
  sendCmd(0xA6);
  sendCmd(0xAF);
  return true;
}

int voltageToPercent(float volts) {
  struct Pt {
    float v;
    uint8_t p;
  };
  static const Pt curve[] = {
      {3.30f, 0},  {3.55f, 5},  {3.65f, 10}, {3.70f, 20}, {3.74f, 30},
      {3.77f, 40}, {3.79f, 50}, {3.82f, 60}, {3.87f, 70}, {3.92f, 80},
      {4.02f, 90}, {4.12f, 95}, {4.20f, 100}};
  if (volts <= curve[0].v) {
    return 0;
  }
  const int n = (int)(sizeof(curve) / sizeof(curve[0]));
  if (volts >= curve[n - 1].v) {
    return 100;
  }
  for (int i = 1; i < n; ++i) {
    if (volts <= curve[i].v) {
      const float t =
          (volts - curve[i - 1].v) / (curve[i].v - curve[i - 1].v);
      return (int)(curve[i - 1].p + t * (curve[i].p - curve[i - 1].p) + 0.5f);
    }
  }
  return 100;
}

uint32_t sampleVbatRaw(int samples) {
  uint32_t sum = 0;
  analogRead(PIN_VBAT);
  delay(2);
  for (int i = 0; i < samples; ++i) {
    sum += analogRead(PIN_VBAT);
  }
  return sum / (uint32_t)samples;
}

float readBatteryVolts() {
  static bool inited = false;
  static uint8_t ctrlOn = LOW;

  if (!inited) {
    pinMode(PIN_VBAT, INPUT);
    pinMode(PIN_ADC_CTRL, OUTPUT);
    analogReadResolution(12);
    analogSetAttenuation(ADC_2_5db);
    analogSetPinAttenuation(PIN_VBAT, ADC_2_5db);

    digitalWrite(PIN_ADC_CTRL, LOW);
    delay(20);
    const uint32_t rawLow = sampleVbatRaw(8);

    digitalWrite(PIN_ADC_CTRL, HIGH);
    delay(20);
    const uint32_t rawHigh = sampleVbatRaw(8);

    ctrlOn = (rawHigh > rawLow) ? HIGH : LOW;
    digitalWrite(PIN_ADC_CTRL, ctrlOn);
    delay(20);

    const uint32_t raw = sampleVbatRaw(8);
    const uint32_t mv = analogReadMilliVolts(PIN_VBAT);
    Serial.printf_P(
        PSTR("VBAT ADC_CTRL LOW=%u HIGH=%u using %s; raw=%u adc=%umV\r\n"),
        rawLow, rawHigh, ctrlOn == LOW ? "LOW" : "HIGH", raw, mv);
    inited = true;
  }

  digitalWrite(PIN_ADC_CTRL, ctrlOn);
  delay(8);

  const uint32_t raw = sampleVbatRaw(12);
  uint32_t mv = analogReadMilliVolts(PIN_VBAT);
  if (mv < 50) {
    mv = (raw * 1250UL) / 4095UL;
  }
  return (mv / 1000.0f) * VBAT_DIVIDER;
}

int readBatteryPercent() {
  const float volts = readBatteryVolts();
  static float filtered = 0;
  if (filtered < 0.5f) {
    filtered = volts;
  } else {
    filtered = filtered * 0.7f + volts * 0.3f;
  }

  if (filtered < 2.5f) {
    return -1;
  }
  return voltageToPercent(filtered);
}

void drawBattery(int percent) {
  const bool known = percent >= 0;
  if (percent < 0) {
    percent = 0;
  } else if (percent > 100) {
    percent = 100;
  }

  char pctText[6];
  if (known) {
    snprintf(pctText, sizeof(pctText), "%u%%", (unsigned)percent);
  } else {
    snprintf(pctText, sizeof(pctText), "--%%");
  }

  const int textW = (int)strlen(pctText) * 6;
  const int bodyW = 14;
  const int bodyH = 7;
  const int nubW = 2;
  const int gap = 2;
  const int totalW = textW + gap + bodyW + nubW;
  const int x = kWidth - totalW;
  const int y = 0;

  drawText(x, y, pctText, true);

  const int bx = x + textW + gap;
  drawRect(bx, y, bodyW, bodyH, true);
  fillRect(bx + bodyW, y + 2, nubW, 3, true);
  if (known) {
    const int innerW = bodyW - 2;
    const int fillW = (innerW * percent + 50) / 100;
    if (fillW > 0) {
      fillRect(bx + 1, y + 1, fillW, bodyH - 2, true);
    }
  }
}

void drawNodeIcon(int x, int y) {
  fillRect(x + 3, y, 3, 3, true);
  setPixel(x + 2, y + 3, true);
  setPixel(x + 6, y + 3, true);
  fillRect(x, y + 4, 3, 3, true);
  fillRect(x + 6, y + 4, 3, 3, true);
}

void drawPeerCount(uint8_t peers) {
  char text[4];
  snprintf(text, sizeof(text), "%u", peers);
  const int textW = (int)strlen(text) * 6;
  const int iconW = 9;
  const int gap = 2;
  const int iconX = kWidth - iconW;
  const int textX = iconX - gap - textW;
  const int y = 10;

  drawText(textX, y, text, true);
  drawNodeIcon(iconX, y);
}

void drawCueBox(int x, int w, uint8_t cueNumber, uint8_t state) {
  const bool green = state == CUE_STATE_GREEN;
  const int h = (w >= (int)kWidth) ? 42 : 36;
  const int y = 22;

  if (green) {
    fillRect(x, y, w, h, true);
  } else {
    fillRect(x, y, w, h, false);
    drawRect(x, y, w, h, true);
  }

  char label[8];
  snprintf(label, sizeof(label), "CUE %u", cueNumber);
  const char* color = green ? "GREEN" : "RED";
  const bool ink = !green;
  const int labelW = (int)strlen(label) * 6;
  const int colorW = (int)strlen(color) * 6;
  const int textY0 = y + (h >= 42 ? 10 : 6);
  const int textY1 = y + (h >= 42 ? 24 : 18);
  drawText(x + (w - labelW) / 2, textY0, label, ink);
  drawText(x + (w - colorW) / 2, textY1, color, ink);
}

}  // namespace

void cueDisplayBegin() {
  g_ready = initOled();
  if (!g_ready) {
    Serial.print(F("OLED not found; continuing without display."));
    Serial.print(LINE_END);
    return;
  }
  cueDisplaySplash();
}

void cueDisplaySplash() {
  if (!g_ready) {
    return;
  }
  clear();
  drawText(0, 0, "CUE LIGHT", true);
  drawBattery(readBatteryPercent());
  drawText(0, 12, FIRMWARE_VERSION, true);
  drawPeerCount(0);
  drawText(0, 28, "HOLD PRG 3S", true);
  drawText(0, 40, "WHILE BLINKING", true);
  flush();
}

void cueDisplayShowOff() {
  if (!g_ready) {
    return;
  }
  clear();
  fillRect(0, 18, kWidth, 40, true);
  const char* msg = "-OFF-";
  const int textW = 5 * 6;
  drawText((kWidth - textW) / 2, 32, msg, false);
  flush();
}

void cueDisplayShowWifiWiped() {
  if (!g_ready) {
    return;
  }
  clear();
  fillRect(0, 10, kWidth, 44, true);
  drawText((kWidth - 10 * 6) / 2, 20, "WIFI WIPED", false);
  drawText((kWidth - 8 * 6) / 2, 36, "AP SETUP", false);
  flush();
}

void cueDisplayPowerDown() {
  if (g_ready) {
    sendCmd(0xAE);
    g_ready = false;
  }
  pinMode(PIN_VEXT, OUTPUT);
  digitalWrite(PIN_VEXT, HIGH);
}

void cueDisplayRefresh() {
  if (!g_ready) {
    return;
  }

  clear();
  drawText(0, 0, "CUE LIGHT", true);
  if (cueLora.isReady()) {
    drawText(58, 0, "LORA", true);
  }
  drawBattery(readBatteryPercent());

  char ipLine[22];
  if (WiFi.status() == WL_CONNECTED) {
    const IPAddress ip = WiFi.localIP();
    snprintf(ipLine, sizeof(ipLine), "%u.%u.%u.%u", ip[0], ip[1], ip[2],
             ip[3]);
  } else if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
    const IPAddress ip = WiFi.softAPIP();
    snprintf(ipLine, sizeof(ipLine), "AP %u.%u.%u.%u", ip[0], ip[1], ip[2],
             ip[3]);
  } else {
    snprintf(ipLine, sizeof(ipLine), "NO WIFI");
  }
  drawText(0, 10, ipLine, true);
  drawPeerCount(peerSync.countPeers());

#if CUE_LOCAL == CUE_LOCAL_ALL
  drawCueBox(0, 62, CUE_NUMBER_1, cueIO.getCueState(CUE_NUMBER_1));
  drawCueBox(66, 62, CUE_NUMBER_2, cueIO.getCueState(CUE_NUMBER_2));
#elif CUE_LOCAL == CUE_LOCAL_TWO
  drawCueBox(0, kWidth, CUE_NUMBER_2, cueIO.getCueState(CUE_NUMBER_2));
#else
  drawCueBox(0, kWidth, CUE_NUMBER_1, cueIO.getCueState(CUE_NUMBER_1));
#endif
  flush();
}

#endif
