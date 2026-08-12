#pragma once

#include <Arduino.h>
#include "config.h"

class CueIO {
public:
  void begin();
  void loop();

  uint8_t getCueState(uint8_t cueNumber) const;
  void setCueState(uint8_t cueNumber, uint8_t state, bool broadcast);

private:
  struct CueChannel {
    uint8_t cueNumber;
    uint8_t buttonPin;
    uint8_t redPin;
    uint8_t greenPin;
    uint8_t state;
    bool lastStableLevel;
    bool lastReadingLevel;
    unsigned long lastDebounceMs;
  };

  CueChannel _cues[CUE_COUNT];

  CueChannel* cueByNumber(uint8_t cueNumber);
  const CueChannel* cueByNumber(uint8_t cueNumber) const;
  void applyOutputs(CueChannel& cue);
  void pollButton(CueChannel& cue);
};

extern CueIO cueIO;
