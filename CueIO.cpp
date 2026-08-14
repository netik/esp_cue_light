#include "CueIO.h"

#include "PeerSync.h"

CueIO cueIO;

namespace {
volatile uint8_t g_btnPendingMask = 0;

void IRAM_ATTR btn1Isr() { g_btnPendingMask |= 0x01; }

void IRAM_ATTR btn2Isr() { g_btnPendingMask |= 0x02; }
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
  digitalWrite(cue.redPin, cue.state == CUE_STATE_RED ? HIGH : LOW);
  digitalWrite(cue.greenPin, cue.state == CUE_STATE_GREEN ? HIGH : LOW);
}

void CueIO::updateStatusLed() {
  // NodeMCU onboard LED is active LOW.
  digitalWrite(PIN_STATUS_LED,
                getCueState(CUE_NUMBER_1) == CUE_STATE_GREEN ? LOW : HIGH);
}

void CueIO::begin() {
  _cues[0] = {CUE_NUMBER_1, PIN_BTN_CUE1, PIN_CUE1_RED, PIN_CUE1_GREEN,
              CUE_STATE_RED, 0, false, true, 0, 0};
  _cues[1] = {CUE_NUMBER_2, PIN_BTN_CUE2, PIN_CUE2_RED, PIN_CUE2_GREEN,
              CUE_STATE_RED, 0, false, true, 0, 0};

  for (auto& cue : _cues) {
    pinMode(cue.buttonPin, INPUT_PULLUP);
    pinMode(cue.redPin, OUTPUT);
    pinMode(cue.greenPin, OUTPUT);
    cue.lastReadingLevel = digitalRead(cue.buttonPin) == LOW;
    applyOutputs(cue);
  }

  pinMode(PIN_STATUS_LED, OUTPUT);
  updateStatusLed();

  attachInterrupt(digitalPinToInterrupt(PIN_BTN_CUE1), btn1Isr, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_CUE2), btn2Isr, FALLING);
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
    if ((pending & bit) == 0) {
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

void CueIO::loop() {
  for (auto& cue : _cues) {
    pollButton(cue);
  }
  processPendingButtons();
}
