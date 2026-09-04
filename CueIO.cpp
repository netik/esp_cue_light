/**
 * @file CueIO.cpp
 * @brief Local cue lamps, buttons, status LED, and Heltec power-off.
 *
 * Owns two cue channels (red/green common-anode RGB). A local button press
 * toggles state, bumps the per-cue sequence number, and asks @ref PeerSync to
 * push. Heltec PRG is polled (short press = cue, 3 s hold = deep sleep) so it
 * is not attached as a falling-edge ISR.
 */

#include "CueIO.h"

#include "CueDisplay.h"
#include "CueLora.h"
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
/** @brief Bit0 = cue 1, bit1 = cue 2. Set from GPIO ISRs, consumed in loop. */
volatile uint8_t g_btnPendingMask = 0;

/** @brief Falling-edge ISR for the Cue 1 header button. */
void IRAM_ATTR btn1Isr() { g_btnPendingMask |= 0x01; }

/** @brief Falling-edge ISR for the Cue 2 header button. */
void IRAM_ATTR btn2Isr() { g_btnPendingMask |= 0x02; }

/**
 * @brief Whether this firmware build drives a physical button for @p cueNumber.
 * @param cueNumber 1-based cue index.
 * @return false when @c CUE_LOCAL excludes that cue.
 */
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

/**
 * @brief Look up a mutable channel by 1-based cue number.
 * @param cueNumber Cue 1 or 2.
 * @return Pointer into `_cues`, or nullptr if out of range.
 */
CueIO::CueChannel* CueIO::cueByNumber(uint8_t cueNumber) {
  if (cueNumber < 1 || cueNumber > CUE_COUNT) {
    return nullptr;
  }
  return &_cues[cueNumber - 1];
}

/** @copydoc CueIO::cueByNumber(uint8_t) */
const CueIO::CueChannel* CueIO::cueByNumber(uint8_t cueNumber) const {
  if (cueNumber < 1 || cueNumber > CUE_COUNT) {
    return nullptr;
  }
  return &_cues[cueNumber - 1];
}

/**
 * @brief Drive this cue's red and green cathodes from @c cue.state.
 * @param cue Channel whose GPIO pins and state are applied.
 *
 * Common-anode lamps: GPIO LOW = color on. Red and green are never both on.
 */
void CueIO::applyOutputs(CueChannel& cue) {
  digitalWrite(cue.redPin, cue.state == CUE_STATE_RED ? LAMP_ON : LAMP_OFF);
  digitalWrite(cue.greenPin, cue.state == CUE_STATE_GREEN ? LAMP_ON : LAMP_OFF);
}

/**
 * @brief Blink-pattern helper used before @ref begin during the WiFi-wipe window.
 * @param state @c CUE_STATE_RED or @c CUE_STATE_GREEN for all local lamps.
 */
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

/**
 * @brief Mirror the local status cue onto the onboard LED and refresh the OLED.
 *
 * Status cue is Cue 1 unless @c CUE_LOCAL_TWO, which uses Cue 2.
 */
void CueIO::updateStatusLed() {
  digitalWrite(PIN_STATUS_LED, getCueState(CUE_STATUS_NUMBER) == CUE_STATE_GREEN
                                     ? STATUS_LED_ON
                                     : STATUS_LED_OFF);
  cueDisplayRefresh();
}

/**
 * @brief Configure lamp GPIOs, pull-ups, and (non-PRG) button interrupts.
 *
 * Cues start red, seq 0. Heltec PRG is not given an ISR; @ref pollPrimaryButton
 * distinguishes a tap from a power-off hold.
 */
void CueIO::begin() {
  const unsigned long now = millis();
  _cues[0] = {CUE_NUMBER_1, PIN_BTN_CUE1, PIN_CUE1_RED, PIN_CUE1_GREEN,
              CUE_STATE_RED, 0, false, true, 0, 0, now};
  _cues[1] = {CUE_NUMBER_2, PIN_BTN_CUE2, PIN_CUE2_RED, PIN_CUE2_GREEN,
              CUE_STATE_RED, 0, false, true, 0, 0, now};

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

/**
 * @brief Current red/green state for one cue.
 * @param cueNumber 1-based cue index.
 * @return @c CUE_STATE_RED or @c CUE_STATE_GREEN; red if @p cueNumber is invalid.
 */
uint8_t CueIO::getCueState(uint8_t cueNumber) const {
  const CueChannel* cue = cueByNumber(cueNumber);
  return cue ? cue->state : CUE_STATE_RED;
}

/**
 * @brief Monotonic sequence used by peer sync (newer seq wins).
 * @param cueNumber 1-based cue index.
 * @return Sequence, or 0 if @p cueNumber is invalid.
 */
uint32_t CueIO::getCueSeq(uint8_t cueNumber) const {
  const CueChannel* cue = cueByNumber(cueNumber);
  return cue ? cue->seq : 0;
}

/**
 * @brief Milliseconds since this cue last changed red/green.
 * @param cueNumber 1-based cue index.
 * @return Elapsed ms, or 0 if @p cueNumber is invalid.
 */
unsigned long CueIO::getCueStateAgeMs(uint8_t cueNumber) const {
  const CueChannel* cue = cueByNumber(cueNumber);
  return cue ? (millis() - cue->stateChangedMs) : 0;
}

/**
 * @brief Apply a peer/LoRa snapshot without incrementing seq or re-pushing.
 * @param cueNumber Cue to update.
 * @param state New red/green value.
 * @param seq Remote sequence to store (already known to be newer).
 */
void CueIO::applyRemoteCueState(uint8_t cueNumber, uint8_t state, uint32_t seq) {
  CueChannel* cue = cueByNumber(cueNumber);
  if (cue == nullptr || state > CUE_STATE_GREEN) {
    return;
  }

  if (cue->state != state) {
    cue->stateChangedMs = millis();
  }
  cue->state = state;
  cue->seq = seq;
  applyOutputs(*cue);
  updateStatusLed();

  Serial.printf_P(PSTR("Cue %u -> %s (remote seq %u)\r\n"), cueNumber,
                  state ? PSTR("GREEN") : PSTR("RED"), seq);
}

/**
 * @brief Local state change; optionally bump seq and notify peers.
 * @param cueNumber Cue to update.
 * @param state New red/green value.
 * @param sync If true, increment seq and call @ref PeerSync::notifyLocalChange.
 *
 * No-op if @p state already matches.
 */
void CueIO::setCueState(uint8_t cueNumber, uint8_t state, bool sync) {
  CueChannel* cue = cueByNumber(cueNumber);
  if (cue == nullptr || state > CUE_STATE_GREEN || cue->state == state) {
    return;
  }

  cue->stateChangedMs = millis();
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

/**
 * @brief Consume one armed falling-edge press: toggle and sync.
 * @param cue Channel whose button fired.
 * @retval true Toggle was applied.
 * @retval false Not armed, pin not LOW, or still in @c BTN_LOCKOUT_MS.
 */
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

/**
 * @brief Drain @c g_btnPendingMask (ISR bits) and accept each pending press.
 */
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

/**
 * @brief Re-arm a header button after it has been released for @c BTN_RELEASE_ARM_MS.
 * @param cue Channel to debounce.
 *
 * Prevents contact bounce from generating a second toggle on the same press.
 */
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

/**
 * @brief Drop GPIO holds left from deep sleep so Vext, lamps, and LoRa pins work.
 *
 * Must run before OLED/WiFi bring-up on Heltec. If wakeup was EXT0 (PRG),
 * deinit RTC on GPIO 0 so it can be a normal input again.
 */
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

/**
 * @brief Channel whose button pin is the onboard PRG (Cue 1 or Cue 2 per CUE_LOCAL).
 * @return Matching channel, or nullptr if PRG is not mapped to a local cue.
 */
CueIO::CueChannel* CueIO::primaryCue() {
  for (auto& cue : _cues) {
    if (cue.buttonPin == PIN_BTN_PRIMARY &&
        localButtonEnabled(cue.cueNumber)) {
      return &cue;
    }
  }
  return nullptr;
}

/**
 * @brief Heltec PRG: ignore until release after wake, 3 s hold = sleep, tap = toggle.
 *
 * Evaluated every @ref loop. Toggle happens on release so a power-off hold
 * does not also change the cue.
 */
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

/**
 * @brief Show -OFF-, cut WiFi/LoRa/OLED, hold GPIOs, deep-sleep until PRG goes LOW.
 *
 * Waits for PRG release first so the same hold that entered this function
 * does not immediately wake the chip. USB charging still works in sleep.
 */
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

  cueLora.end();

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

  /* sleep until primary button is released, releasing it puts us to sleep */
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

/**
 * @brief Main I/O tick: PRG poll, header debounce, ISR presses, 1 Hz OLED refresh.
 */
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
