#include "CueIO.h"

#include "PeerSync.h"

CueIO cueIO;

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
              CUE_STATE_RED, 0, false, false, false, 0, 0};
  _cues[1] = {CUE_NUMBER_2, PIN_BTN_CUE2, PIN_CUE2_RED, PIN_CUE2_GREEN,
              CUE_STATE_RED, 0, false, false, false, 0, 0};

  for (auto& cue : _cues) {
    pinMode(cue.buttonPin, INPUT_PULLUP);
    pinMode(cue.redPin, OUTPUT);
    pinMode(cue.greenPin, OUTPUT);
    applyOutputs(cue);
  }

  pinMode(PIN_STATUS_LED, OUTPUT);
  updateStatusLed();
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

void CueIO::acceptButtonPress(CueChannel& cue) {
  const unsigned long now = millis();
  if ((now - cue.lastAcceptedMs) < BTN_LOCKOUT_MS) {
    cue.pendingPress = false;
    return;
  }

  cue.lastAcceptedMs = now;
  cue.pendingPress = false;

  const uint8_t nextState =
      cue.state == CUE_STATE_RED ? CUE_STATE_GREEN : CUE_STATE_RED;
  setCueState(cue.cueNumber, nextState, true);
}

void CueIO::pollButton(CueChannel& cue) {
  const bool pressed = digitalRead(cue.buttonPin) == LOW;
  const unsigned long now = millis();

  if (pressed != cue.lastReadingLevel) {
    cue.lastDebounceMs = now;
    if (pressed) {
      cue.pendingPress = true;
    }
    cue.lastReadingLevel = pressed;
  }

  if ((now - cue.lastDebounceMs) < BTN_DEBOUNCE_MS) {
    return;
  }

  if (pressed == cue.lastStableLevel) {
    return;
  }

  const bool wasStablePressed = cue.lastStableLevel;
  cue.lastStableLevel = pressed;

  if (pressed && !wasStablePressed) {
    // Debounced press — toggle immediately for responsive lamps.
    acceptButtonPress(cue);
  } else if (!pressed && !wasStablePressed && cue.pendingPress) {
    // Quick tap — released before debounce confirmed press.
    acceptButtonPress(cue);
  }
}

void CueIO::loop() {
  for (auto& cue : _cues) {
    pollButton(cue);
  }
}
