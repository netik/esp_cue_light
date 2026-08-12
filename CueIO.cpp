#include "CueIO.h"
#include "UdpCue.h"

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

void CueIO::begin() {
  _cues[0] = {CUE_NUMBER_1, PIN_BTN_CUE1, PIN_CUE1_RED, PIN_CUE1_GREEN,
              CUE_STATE_RED, HIGH, HIGH, 0};
  _cues[1] = {CUE_NUMBER_2, PIN_BTN_CUE2, PIN_CUE2_RED, PIN_CUE2_GREEN,
              CUE_STATE_RED, HIGH, HIGH, 0};

  for (auto& cue : _cues) {
    pinMode(cue.buttonPin, INPUT_PULLUP);
    pinMode(cue.redPin, OUTPUT);
    pinMode(cue.greenPin, OUTPUT);
    applyOutputs(cue);
  }
}

uint8_t CueIO::getCueState(uint8_t cueNumber) const {
  const CueChannel* cue = cueByNumber(cueNumber);
  return cue ? cue->state : CUE_STATE_RED;
}

void CueIO::setCueState(uint8_t cueNumber, uint8_t state, bool broadcast) {
  CueChannel* cue = cueByNumber(cueNumber);
  if (cue == nullptr || state > CUE_STATE_GREEN || cue->state == state) {
    return;
  }

  cue->state = state;
  applyOutputs(*cue);

  Serial.printf_P(PSTR("Cue %u -> %s\r\n"), cueNumber,
                  state ? PSTR("GREEN") : PSTR("RED"));

  if (broadcast) {
    udpCue.broadcastCue(cueNumber, state);
  }
}

void CueIO::pollButton(CueChannel& cue) {
  const bool reading = digitalRead(cue.buttonPin) == LOW;

  if (reading != cue.lastReadingLevel) {
    cue.lastDebounceMs = millis();
    cue.lastReadingLevel = reading;
  }

  if ((millis() - cue.lastDebounceMs) < BTN_DEBOUNCE_MS) {
    return;
  }

  if (reading == cue.lastStableLevel) {
    return;
  }

  cue.lastStableLevel = reading;
  if (!reading) {
    return;
  }

  const uint8_t nextState =
      cue.state == CUE_STATE_RED ? CUE_STATE_GREEN : CUE_STATE_RED;
  setCueState(cue.cueNumber, nextState, true);
}

void CueIO::loop() {
  for (auto& cue : _cues) {
    pollButton(cue);
  }
}
