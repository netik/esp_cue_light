#include "CueIO.h"

#include "CueDisplay.h"
#include "PeerSync.h"

#ifdef CUE_BOARD_HELTEC_V3
#include <WiFi.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#endif

CueIO cueIO;

namespace {
volatile uint8_t g_btnPendingMask = 0;

void IRAM_ATTR btn1Isr() { g_btnPendingMask |= 0x01; }

void IRAM_ATTR btn2Isr() { g_btnPendingMask |= 0x02; }

bool localButtonEnabled(uint8_t cueNumber) {
#if CUE_LOCAL == CUE_LOCAL_ONE
  return cueNumber == CUE_NUMBER_1;
#elif CUE_LOCAL == CUE_LOCAL_TWO
  return cueNumber == CUE_NUMBER_2;
#else
  (void)cueNumber;
  return true;
#endif
}
}  // namespace

CueIO::CueChannel* CueIO::cueByNumber(uint8_t cueNumber) {
  if (cueNumber < 1 || cueNumber > CUE_COUNT) {
    return nullptr;
  }
  return &_cues[cueNumber - 1];
}

const CueIO::CueChannel* CueIO::cueByNumber(uint8_t cueNumber) const {
  if (cueNumber < 1 || cueNumber > CUE_COUNT) {
    return nullptr;
  }
  return &_cues[cueNumber - 1];
}

void CueIO::applyOutputs(CueChannel& cue) {
  digitalWrite(cue.redPin, cue.state == CUE_STATE_RED ? LAMP_ON : LAMP_OFF);
  digitalWrite(cue.greenPin, cue.state == CUE_STATE_GREEN ? LAMP_ON : LAMP_OFF);
}

void CueIO::setDfmLamps(uint8_t state) {
  const uint8_t redLevel = state == CUE_STATE_RED ? LAMP_ON : LAMP_OFF;
  const uint8_t greenLevel = state == CUE_STATE_GREEN ? LAMP_ON : LAMP_OFF;

#if CUE_LOCAL != CUE_LOCAL_TWO
  pinMode(PIN_CUE1_RED, OUTPUT);
  pinMode(PIN_CUE1_GREEN, OUTPUT);
  digitalWrite(PIN_CUE1_RED, redLevel);
  digitalWrite(PIN_CUE1_GREEN, greenLevel);
#endif
#if CUE_LOCAL != CUE_LOCAL_ONE
  pinMode(PIN_CUE2_RED, OUTPUT);
  pinMode(PIN_CUE2_GREEN, OUTPUT);
  digitalWrite(PIN_CUE2_RED, redLevel);
  digitalWrite(PIN_CUE2_GREEN, greenLevel);
#endif
}

void CueIO::updateStatusLed() {
  digitalWrite(PIN_STATUS_LED, getCueState(CUE_STATUS_NUMBER) == CUE_STATE_GREEN
                                     ? STATUS_LED_ON
                                     : STATUS_LED_OFF);
  cueDisplayRefresh();
}

void CueIO::begin() {
  _cues[0] = {CUE_NUMBER_1, PIN_BTN_CUE1, PIN_CUE1_RED, PIN_CUE1_GREEN,
              CUE_STATE_RED, 0, false, true, 0, 0};
  _cues[1] = {CUE_NUMBER_2, PIN_BTN_CUE2, PIN_CUE2_RED, PIN_CUE2_GREEN,
              CUE_STATE_RED, 0, false, true, 0, 0};

  for (auto& cue : _cues) {
    pinMode(cue.redPin, OUTPUT);
    pinMode(cue.greenPin, OUTPUT);
    applyOutputs(cue);
    if (!localButtonEnabled(cue.cueNumber)) {
      continue;
    }
    pinMode(cue.buttonPin, INPUT_PULLUP);
    cue.lastReadingLevel = digitalRead(cue.buttonPin) == LOW;
  }

  pinMode(PIN_STATUS_LED, OUTPUT);
  updateStatusLed();

#ifdef CUE_BOARD_HELTEC_V3
  _ignorePrimaryUntilRelease = digitalRead(PIN_BTN_PRIMARY) == LOW;
  _primaryPressStartMs = 0;
  _primaryLongPressHandled = false;
#if CUE_LOCAL != CUE_LOCAL_TWO
  if (PIN_BTN_CUE1 != PIN_BTN_PRIMARY) {
    attachInterrupt(digitalPinToInterrupt(PIN_BTN_CUE1), btn1Isr, FALLING);
  }
#endif
#if CUE_LOCAL != CUE_LOCAL_ONE
  if (PIN_BTN_CUE2 != PIN_BTN_PRIMARY) {
    attachInterrupt(digitalPinToInterrupt(PIN_BTN_CUE2), btn2Isr, FALLING);
  }
#endif
#else
#if CUE_LOCAL != CUE_LOCAL_TWO
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_CUE1), btn1Isr, FALLING);
#endif
#if CUE_LOCAL != CUE_LOCAL_ONE
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_CUE2), btn2Isr, FALLING);
#endif
#endif
}

uint8_t CueIO::getCueState(uint8_t cueNumber) const {
  const CueChannel* cue = cueByNumber(cueNumber);
  return cue ? cue->state : CUE_STATE_RED;
}

uint32_t CueIO::getCueSeq(uint8_t cueNumber) const {
  const CueChannel* cue = cueByNumber(cueNumber);
  return cue ? cue->seq : 0;
}

void CueIO::applyRemoteCueState(uint8_t cueNumber, uint8_t state, uint32_t seq) {
  CueChannel* cue = cueByNumber(cueNumber);
  if (cue == nullptr || state > CUE_STATE_GREEN) {
    return;
  }

  cue->state = state;
  cue->seq = seq;
  applyOutputs(*cue);
  updateStatusLed();

  Serial.printf_P(PSTR("Cue %u -> %s (remote seq %u)\r\n"), cueNumber,
                  state ? PSTR("GREEN") : PSTR("RED"), seq);
}

void CueIO::setCueState(uint8_t cueNumber, uint8_t state, bool sync) {
  CueChannel* cue = cueByNumber(cueNumber);
  if (cue == nullptr || state > CUE_STATE_GREEN || cue->state == state) {
    return;
  }

  cue->state = state;
  if (sync) {
    ++cue->seq;
  }
  applyOutputs(*cue);
  updateStatusLed();

  Serial.printf_P(PSTR("Cue %u -> %s\r\n"), cueNumber,
                  state ? PSTR("GREEN") : PSTR("RED"));

  if (sync) {
    peerSync.notifyLocalChange();
  }
}

bool CueIO::acceptButtonPress(CueChannel& cue) {
  if (!cue.armed) {
    return false;
  }

  if (digitalRead(cue.buttonPin) != LOW) {
    return false;
  }

  const unsigned long now = millis();
  if ((now - cue.lastAcceptedMs) < BTN_LOCKOUT_MS) {
    return false;
  }

  cue.armed = false;
  cue.lastAcceptedMs = now;

  const uint8_t nextState =
      cue.state == CUE_STATE_RED ? CUE_STATE_GREEN : CUE_STATE_RED;
  setCueState(cue.cueNumber, nextState, true);
  return true;
}

void CueIO::processPendingButtons() {
  uint8_t pending;
  noInterrupts();
  pending = g_btnPendingMask;
  g_btnPendingMask = 0;
  interrupts();

  for (uint8_t i = 0; i < CUE_COUNT; ++i) {
    const uint8_t bit = 1u << i;
    if ((pending & bit) == 0 || !localButtonEnabled(_cues[i].cueNumber)) {
      continue;
    }
    acceptButtonPress(_cues[i]);
  }
}

void CueIO::pollButton(CueChannel& cue) {
  const bool pressed = digitalRead(cue.buttonPin) == LOW;
  const unsigned long now = millis();

  if (pressed) {
    cue.releaseMs = 0;
  } else if (cue.lastReadingLevel) {
    cue.releaseMs = now;
  } else if (cue.releaseMs != 0 &&
             (now - cue.releaseMs) >= BTN_RELEASE_ARM_MS) {
    cue.armed = true;
    cue.releaseMs = 0;
  }

  cue.lastReadingLevel = pressed;
}

#ifdef CUE_BOARD_HELTEC_V3

void CueIO::releaseSleepHolds() {
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)PIN_VEXT);
  gpio_hold_dis((gpio_num_t)PIN_STATUS_LED);
  gpio_hold_dis((gpio_num_t)PIN_CUE1_RED);
  gpio_hold_dis((gpio_num_t)PIN_CUE1_GREEN);
  gpio_hold_dis((gpio_num_t)PIN_CUE2_RED);
  gpio_hold_dis((gpio_num_t)PIN_CUE2_GREEN);
  gpio_hold_dis((gpio_num_t)PIN_LORA_RST);
  gpio_hold_dis((gpio_num_t)PIN_LORA_NSS);
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    rtc_gpio_hold_dis(GPIO_NUM_0);
    rtc_gpio_deinit(GPIO_NUM_0);
  }
  pinMode(PIN_BTN_PRIMARY, INPUT_PULLUP);
}

CueIO::CueChannel* CueIO::primaryCue() {
  for (auto& cue : _cues) {
    if (cue.buttonPin == PIN_BTN_PRIMARY &&
        localButtonEnabled(cue.cueNumber)) {
      return &cue;
    }
  }
  return nullptr;
}

void CueIO::pollPrimaryButton() {
  const bool pressed = digitalRead(PIN_BTN_PRIMARY) == LOW;
  const unsigned long now = millis();

  if (_ignorePrimaryUntilRelease) {
    if (!pressed) {
      _ignorePrimaryUntilRelease = false;
      _primaryPressStartMs = 0;
      _primaryLongPressHandled = false;
    }
    return;
  }

  if (pressed) {
    if (_primaryPressStartMs == 0) {
      _primaryPressStartMs = now;
    } else if (!_primaryLongPressHandled &&
               (now - _primaryPressStartMs) >= POWER_OFF_HOLD_MS) {
      _primaryLongPressHandled = true;
      enterPowerOff();
    }
    return;
  }

  if (_primaryPressStartMs != 0 && !_primaryLongPressHandled) {
    const unsigned long heldMs = now - _primaryPressStartMs;
    CueChannel* cue = primaryCue();
    if (cue != nullptr && heldMs >= BTN_RELEASE_ARM_MS &&
        (now - cue->lastAcceptedMs) >= BTN_LOCKOUT_MS) {
      const uint8_t nextState =
          cue->state == CUE_STATE_RED ? CUE_STATE_GREEN : CUE_STATE_RED;
      cue->lastAcceptedMs = now;
      setCueState(cue->cueNumber, nextState, true);
    }
  }

  _primaryPressStartMs = 0;
  _primaryLongPressHandled = false;
}

void CueIO::enterPowerOff() {
  Serial.print(F("Power off. Tap PRG to wake."));
  Serial.print(LINE_END);

  cueDisplayShowOff();
  delay(1000);

  for (auto& cue : _cues) {
    digitalWrite(cue.redPin, LAMP_OFF);
    digitalWrite(cue.greenPin, LAMP_OFF);
  }
  digitalWrite(PIN_STATUS_LED, STATUS_LED_OFF);
  cueDisplayPowerDown();

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  esp_wifi_stop();

  pinMode(PIN_LORA_RST, OUTPUT);
  digitalWrite(PIN_LORA_RST, LOW);
  pinMode(PIN_LORA_NSS, OUTPUT);
  digitalWrite(PIN_LORA_NSS, HIGH);

  gpio_hold_en((gpio_num_t)PIN_VEXT);
  gpio_hold_en((gpio_num_t)PIN_STATUS_LED);
  gpio_hold_en((gpio_num_t)PIN_CUE1_RED);
  gpio_hold_en((gpio_num_t)PIN_CUE1_GREEN);
  gpio_hold_en((gpio_num_t)PIN_CUE2_RED);
  gpio_hold_en((gpio_num_t)PIN_CUE2_GREEN);
  gpio_hold_en((gpio_num_t)PIN_LORA_RST);
  gpio_hold_en((gpio_num_t)PIN_LORA_NSS);
  gpio_deep_sleep_hold_en();

  while (digitalRead(PIN_BTN_PRIMARY) == LOW) {
    delay(10);
  }
  delay(50);

  rtc_gpio_pulldown_dis(GPIO_NUM_0);
  rtc_gpio_pullup_en(GPIO_NUM_0);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);

  Serial.flush();
  esp_deep_sleep_start();
}

#endif

void CueIO::loop() {
#ifdef CUE_BOARD_HELTEC_V3
  pollPrimaryButton();
#endif
  for (auto& cue : _cues) {
#ifdef CUE_BOARD_HELTEC_V3
    if (cue.buttonPin == PIN_BTN_PRIMARY) {
      continue;
    }
#endif
    if (localButtonEnabled(cue.cueNumber)) {
      pollButton(cue);
    }
  }
  processPendingButtons();

#if CUE_HAS_OLED
  static unsigned long lastOledMs = 0;
  const unsigned long now = millis();
  if (lastOledMs == 0 || (now - lastOledMs) >= 1000) {
    lastOledMs = now;
    cueDisplayRefresh();
  }
#endif
}
