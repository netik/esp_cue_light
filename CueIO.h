#pragma once

#include <Arduino.h>
#include "config.h"

class CueIO {
public:
  void begin();
  void loop();

  // Boot DFM: drive local cue lamp(s) red or green before begin().
  void setDfmLamps(uint8_t state);

  uint8_t getCueState(uint8_t cueNumber) const;
  uint32_t getCueSeq(uint8_t cueNumber) const;
  void setCueState(uint8_t cueNumber, uint8_t state, bool sync);
  void applyRemoteCueState(uint8_t cueNumber, uint8_t state, uint32_t seq);

#ifdef CUE_BOARD_HELTEC_V3
  static void releaseSleepHolds();
#endif

private:
  struct CueChannel {
    uint8_t cueNumber;
    uint8_t buttonPin;
    uint8_t redPin;
    uint8_t greenPin;
    uint8_t state;
    uint32_t seq;
    bool lastReadingLevel;
    bool armed;
    unsigned long releaseMs;
    unsigned long lastAcceptedMs;
  };

  CueChannel _cues[CUE_COUNT];

  CueChannel* cueByNumber(uint8_t cueNumber);
  const CueChannel* cueByNumber(uint8_t cueNumber) const;
  void applyOutputs(CueChannel& cue);
  void updateStatusLed();
  bool acceptButtonPress(CueChannel& cue);
  void processPendingButtons();
  void pollButton(CueChannel& cue);
#ifdef CUE_BOARD_HELTEC_V3
  CueChannel* primaryCue();
  void pollPrimaryButton();
  void enterPowerOff();
  bool _ignorePrimaryUntilRelease = false;
  unsigned long _primaryPressStartMs = 0;
  bool _primaryLongPressHandled = false;
#endif
};

extern CueIO cueIO;
