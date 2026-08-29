#!/usr/bin/env bash
set -euo pipefail

LIB_DIR="${HOME}/Documents/Arduino/libraries"
ESP_ASYNC_DIR="${LIB_DIR}/ESPAsyncWebServer"
ESP_ASYNC_REPO="https://github.com/ESP32Async/ESPAsyncWebServer.git"

echo "Ensuring ESP8266 core is installed..."
arduino-cli core install esp8266:esp8266

echo "Ensuring project libraries are installed..."
arduino-cli lib install "AsyncEspFsWebserver" "ESPAsyncTCP"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
bash "${SCRIPT_DIR}/patch-asyncespfswebserver.sh"

if [[ -d "${ESP_ASYNC_DIR}/.git" ]]; then
  origin="$(git -C "${ESP_ASYNC_DIR}" remote get-url origin 2>/dev/null || true)"
  if [[ "${origin}" == *"ESP32Async/ESPAsyncWebServer"* ]]; then
    echo "ESP32Async/ESPAsyncWebServer already installed."
  else
    echo "Replacing incompatible ESPAsyncWebServer with ESP32Async fork..."
    rm -rf "${ESP_ASYNC_DIR}"
    git clone --depth 1 "${ESP_ASYNC_REPO}" "${ESP_ASYNC_DIR}"
  fi
elif [[ -d "${ESP_ASYNC_DIR}" ]]; then
  echo "Replacing incompatible ESPAsyncWebServer with ESP32Async fork..."
  rm -rf "${ESP_ASYNC_DIR}"
  git clone --depth 1 "${ESP_ASYNC_REPO}" "${ESP_ASYNC_DIR}"
else
  echo "Installing ESP32Async/ESPAsyncWebServer..."
  git clone --depth 1 "${ESP_ASYNC_REPO}" "${ESP_ASYNC_DIR}"
fi

echo "Library setup complete."
