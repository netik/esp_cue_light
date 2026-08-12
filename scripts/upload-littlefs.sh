#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Upload the project data/ tree to the ESP8266 LittleFS partition.

By default, device state created at runtime is preserved across uploads:
  1. Read the current filesystem from the device (when reachable)
  2. Merge /setup/ and credentials.bin into the new image
  3. Otherwise restore /setup/ from .littlefs-backup/
  4. After a successful read, refresh the local backup

Options:
  --fresh       Do not preserve runtime files; upload data/ only
  --port PORT   Serial port (default: /dev/cu.usbserial-0001)
  -h, --help    Show this help
EOF
}

PORT="${CUE_LIGHT_PORT:-/dev/cu.usbserial-0001}"
PRESERVE_RUNTIME=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    --fresh)
      PRESERVE_RUNTIME=0
      shift
      ;;
    --port)
      PORT="${2:?missing value for --port}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    -*)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
    *)
      PORT="$1"
      shift
      ;;
  esac
done

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATA_DIR="${PROJECT_DIR}/data"
BUILD_DIR="${PROJECT_DIR}/build"
BACKUP_DIR="${PROJECT_DIR}/.littlefs-backup"
STAGING_DIR="${BUILD_DIR}/littlefs-staging"
DEVICE_IMAGE="${BUILD_DIR}/littlefs-device.bin"
DEVICE_UNPACK="${BUILD_DIR}/littlefs-device-unpack"

ARDUINO15="${HOME}/Library/Arduino15"

find_arduino_tool() {
  local pattern="$1"
  find "${ARDUINO15}/packages/esp8266" -path "${pattern}" 2>/dev/null | sort -V | tail -1
}

MKLITTLEFS="$(find_arduino_tool '*/tools/mklittlefs/*/mklittlefs')"
ESPTOOL="$(find_arduino_tool '*/hardware/esp8266/*/tools/esptool/esptool.py')"

# Default NodeMCU 4M2M layout: 2 MB filesystem at 0x200000
FS_START="0x200000"
FS_SIZE="2097152"
FS_PAGE="256"
FS_BLOCK="8192"
SETUP_CONFIG="setup/config.json"
CREDENTIALS_FILE="credentials.bin"

if [[ ! -d "${DATA_DIR}" ]]; then
  echo "Missing data directory: ${DATA_DIR}" >&2
  exit 1
fi

if [[ ! -x "${MKLITTLEFS}" ]]; then
  echo "mklittlefs not found. Install esp8266 core: arduino-cli core install esp8266:esp8266" >&2
  exit 1
fi

if [[ ! -f "${ESPTOOL}" ]]; then
  echo "esptool.py not found under ${ARDUINO15}/packages/esp8266" >&2
  exit 1
fi

mklittlefs_unpack() {
  local image="$1"
  local dest="$2"
  rm -rf "${dest}"
  mkdir -p "${dest}"
  "${MKLITTLEFS}" -u "${dest}" -s "${FS_SIZE}" -p "${FS_PAGE}" -b "${FS_BLOCK}" "${image}"
}

copy_setup_tree() {
  local source_root="$1"
  local label="$2"

  if [[ ! -f "${source_root}/${SETUP_CONFIG}" ]]; then
    return 1
  fi

  rm -rf "${STAGING_DIR}/setup"
  mkdir -p "${STAGING_DIR}/setup"
  cp -a "${source_root}/setup/." "${STAGING_DIR}/setup/"
  echo "Preserved /setup from ${label}."
  return 0
}

copy_credentials_file() {
  local source_root="$1"
  local label="$2"

  if [[ ! -f "${source_root}/${CREDENTIALS_FILE}" ]]; then
    return 1
  fi

  cp -a "${source_root}/${CREDENTIALS_FILE}" "${STAGING_DIR}/${CREDENTIALS_FILE}"
  echo "Preserved /${CREDENTIALS_FILE} from ${label}."
  return 0
}

merge_runtime_files() {
  local source_root="$1"
  local label="$2"
  local merged=0

  if copy_setup_tree "${source_root}" "${label}"; then
    merged=1
  fi

  if copy_credentials_file "${source_root}" "${label}"; then
    merged=1
  fi

  return "${merged}"
}

refresh_local_backup() {
  rm -rf "${BACKUP_DIR}/setup"
  mkdir -p "${BACKUP_DIR}/setup"
  cp -a "${STAGING_DIR}/setup/." "${BACKUP_DIR}/setup/"
  echo "Updated local backup at ${BACKUP_DIR}/setup"

  if [[ -f "${STAGING_DIR}/${CREDENTIALS_FILE}" ]]; then
    cp -a "${STAGING_DIR}/${CREDENTIALS_FILE}" "${BACKUP_DIR}/${CREDENTIALS_FILE}"
    echo "Updated local backup at ${BACKUP_DIR}/${CREDENTIALS_FILE}"
  fi
}

read_device_filesystem() {
  echo "Reading current LittleFS from ${PORT}..."
  rm -f "${DEVICE_IMAGE}"
  python3 "${ESPTOOL}" \
    --chip esp8266 \
    --port "${PORT}" \
    --baud 115200 \
    read_flash \
    "${FS_START}" "${FS_SIZE}" "${DEVICE_IMAGE}"
}

prepare_staging() {
  rm -rf "${STAGING_DIR}"
  mkdir -p "${STAGING_DIR}"
  cp -a "${DATA_DIR}/." "${STAGING_DIR}/"

  if [[ "${PRESERVE_RUNTIME}" -eq 0 ]]; then
    echo "Fresh upload: runtime files will not be preserved."
    return
  fi

  local preserved=0

  if read_device_filesystem && mklittlefs_unpack "${DEVICE_IMAGE}" "${DEVICE_UNPACK}"; then
    if merge_runtime_files "${DEVICE_UNPACK}" "device flash"; then
      preserved=1
      refresh_local_backup
    else
      echo "No runtime files found on device to preserve."
    fi
  else
    echo "Could not read filesystem from device (port busy or not connected)."
  fi

  if [[ "${preserved}" -eq 0 ]]; then
    if [[ -f "${BACKUP_DIR}/${SETUP_CONFIG}" ]]; then
      copy_setup_tree "${BACKUP_DIR}" "local backup (.littlefs-backup)"
      preserved=1
    fi
    if [[ -f "${BACKUP_DIR}/${CREDENTIALS_FILE}" ]]; then
      copy_credentials_file "${BACKUP_DIR}" "local backup (.littlefs-backup)"
      preserved=1
    fi
  fi

  if [[ "${preserved}" -eq 0 ]]; then
    echo "No saved runtime files found. Upload will reset WiFi/options until you configure /setup again."
  fi
}

mkdir -p "${BUILD_DIR}"
prepare_staging

IMAGE="${BUILD_DIR}/littlefs.bin"

echo "Building LittleFS image from ${STAGING_DIR}..."
"${MKLITTLEFS}" -c "${STAGING_DIR}" -s "${FS_SIZE}" -p "${FS_PAGE}" -b "${FS_BLOCK}" "${IMAGE}"

echo "Uploading ${IMAGE} to ${PORT} at ${FS_START}..."
python3 "${ESPTOOL}" \
  --chip esp8266 \
  --port "${PORT}" \
  --baud 115200 \
  write_flash \
  --flash_mode dio \
  --flash_freq 40m \
  --flash_size 4MB \
  "${FS_START}" "${IMAGE}"

echo "LittleFS upload complete."
