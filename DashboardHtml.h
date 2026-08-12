#pragma once

#include <Arduino.h>

// Embedded copy of data/index.htm. Seeded onto LittleFS when missing.
static const char DASHBOARD_INDEX_HTM[] PROGMEM = R"=====(<!DOCTYPE html>
<html>
  <head>
    <meta charset="UTF-8">
    <title>Cue Light Status</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body { font-family: sans-serif; margin: 2rem; max-width: 480px; }
      h1 { font-size: 1.4rem; }
      .cue {
        display: flex;
        align-items: center;
        gap: 1rem;
        margin: 1rem 0;
        padding: 1rem;
        border: 1px solid #ccc;
        border-radius: 8px;
      }
      .lamp {
        width: 2rem;
        height: 2rem;
        border-radius: 50%;
        border: 2px solid #333;
      }
      .red { background: #c0392b; }
      .green { background: #27ae60; }
      .meta { color: #666; font-size: 0.9rem; }
    </style>
  </head>
  <body>
    <h1>Cue Light Status</h1>
    <p class="meta" id="network"></p>

    <div class="cue">
      <div class="lamp red" id="lamp1"></div>
      <div>
        <strong>Cue 1</strong>
        <div id="label1">—</div>
      </div>
    </div>

    <div class="cue">
      <div class="lamp red" id="lamp2"></div>
      <div>
        <strong>Cue 2</strong>
        <div id="label2">—</div>
      </div>
    </div>

    <p class="meta"><a href="/setup">Configure system / WiFi</a></p>

    <script>
      function renderCue(num, state) {
        const lamp = document.getElementById('lamp' + num);
        const label = document.getElementById('label' + num);
        const isGreen = Number(state) === 1;
        lamp.className = 'lamp ' + (isGreen ? 'green' : 'red');
        label.textContent = isGreen ? 'GREEN' : 'RED';
      }

      function refresh() {
        fetch('/api/cues')
          .then(r => r.json())
          .then(data => {
            document.getElementById('network').textContent =
              'System ' + data.system_id + ' · Group ' + data.cue_group;
            renderCue(1, data.cue1);
            renderCue(2, data.cue2);
          })
          .catch(err => console.error(err));
      }

      refresh();
      setInterval(refresh, 1000);
    </script>
  </body>
</html>
)=====";
