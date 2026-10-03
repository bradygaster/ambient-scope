#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLI="${ARDUINO_CLI:-arduino-cli}"
if [[ "$(uname -s)" == "Darwin" ]]; then
  DEFAULT_CONFIG="$HOME/Library/Arduino15/arduino-cli.yaml"
else
  DEFAULT_CONFIG="$HOME/.arduino15/arduino-cli.yaml"
fi
CONFIG="${ARDUINO_CONFIG:-$DEFAULT_CONFIG}"
WAVESHARE_REVISION="e4344e70c2fa78a13e8a06566507f1ba8af6672a"
WAVESHARE="${WAVESHARE_DIR:-$ROOT/.deps/waveshare}"
FQBN="esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,CPUFreq=240,FlashMode=qio,FlashSize=16M,PartitionScheme=custom,PSRAM=opi,LoopCore=1,EventsCore=1,DebugLevel=error"
ACTION="${1:-build}"

if [[ ! -x "$(command -v "$CLI" 2>/dev/null || true)" && ! -x "$CLI" ]]; then
  echo "Arduino CLI not found. Set ARDUINO_CLI or add arduino-cli to PATH." >&2
  exit 1
fi
if [[ ! -f "$CONFIG" ]]; then
  echo "Arduino CLI configuration not found: $CONFIG" >&2
  exit 1
fi
if [[ ! -d "$WAVESHARE/.git" ]]; then
  mkdir -p "$(dirname "$WAVESHARE")"
  git clone --filter=blob:none --no-checkout \
    https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75.git "$WAVESHARE"
  git -C "$WAVESHARE" sparse-checkout set \
    examples/arduino/libraries/GFX_Library_for_Arduino \
    examples/arduino/libraries/SensorLib
  git -C "$WAVESHARE" checkout --detach "$WAVESHARE_REVISION"
fi

GFX="$WAVESHARE/examples/arduino/libraries/GFX_Library_for_Arduino"
SENSORS="$WAVESHARE/examples/arduino/libraries/SensorLib"
BUILD="$ROOT/build/firmware"

case "$ACTION" in
  build)
    mkdir -p "$BUILD"
    "$CLI" --config-file "$CONFIG" compile --fqbn "$FQBN" \
      --build-property "upload.maximum_size=2097152" \
      --library "$GFX" --library "$SENSORS" --warnings default \
      --build-path "$BUILD" "$ROOT/firmware/AmbientScope"
    ;;
  upload)
    PORT="${2:?Usage: bash tools/arduino.sh upload /dev/cu.usbmodemXXXX}"
    [[ -f "$BUILD/AmbientScope.ino.bin" ]] || {
      echo "Build first: bash tools/arduino.sh build" >&2
      exit 1
    }
    "$CLI" --config-file "$CONFIG" upload --fqbn "$FQBN" \
      --port "$PORT" --input-dir "$BUILD" "$ROOT/firmware/AmbientScope"
    ;;
  *)
    echo "Usage: bash tools/arduino.sh {build|upload PORT}" >&2
    exit 2
    ;;
esac
