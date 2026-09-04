# Cue state timer — design spec

**Date:** 2026-09-03  
**Status:** Approved

## Goal

Show how long the current cue has been in its present state (red or green) inside the Heltec OLED cue box, lower-right corner.

## Format

- Under 1 hour: `m:ss` (e.g. `0:07`, `12:34`)
- 1 hour and up: `h:mm:ss` (e.g. `1:02:03`)

## Architecture

Track `stateChangedMs` per cue in `CueIO`. Update when state actually changes (local button or remote sync). Render in `drawCueBox()` using existing 5×7 font at 1 Hz refresh.

## Layout

Cue label and RED/GREEN text remain centered. Timer sits lower-right with 2 px inset. Inverted ink on green fill; white on red outline.

## Scope

Heltec OLED only. No API or web dashboard changes.

## Edge cases

- Boot: timer starts at 0 for initial red state.
- Remote sync with unchanged state: timer not reset.
- `millis()` wrap: unsigned subtraction is safe.
