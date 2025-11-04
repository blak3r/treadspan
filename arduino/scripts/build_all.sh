#!/usr/bin/env bash
set -euo pipefail

# -------- CONFIG --------
ENV_NAME="lilygo-t-display"
BUILD_DIR="../.pio/build/${ENV_NAME}"
RELEASES_BASE="${HOME}/Documents/git/treadspan-web-installer/releases"

# PlatformIO Core (auto-detect)
PIO="${HOME}/.platformio/penv/bin/pio"
if [[ ! -x "$PIO" ]]; then
  if command -v pio >/dev/null 2>&1; then
    PIO="$(command -v pio)"
  elif python3 -c "import platformio" >/dev/null 2>&1; then
    PIO="python3 -m platformio"
  else
    echo "Couldn't find PlatformIO Core."
    echo "Try:  pipx install platformio   (or open VS Code once so it installs ~/.platformio/penv)"
    exit 1
  fi
fi

# Use PlatformIO’s bundled Python + esptool (avoids Homebrew issues)
PY="${HOME}/.platformio/penv/bin/python"
ESP="${HOME}/.platformio/packages/tool-esptoolpy/esptool.py"
BOOT_APP0="${HOME}/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

# Flash params for this board
FLASH_MODE="dio"
FLASH_FREQ="40m"
FLASH_SIZE="4MB"

# Default build order
DEFAULT_VARIANTS="lifespan-omni lifespan-retro ftms urevo"

macro_for_variant() {
  case "$1" in
    lifespan-omni)  echo "OMNI_CONSOLE_MODE" ;;
    lifespan-retro) echo "RETRO_MODE" ;;
    ftms)           echo "FTMS_MODE" ;;
    urevo)          echo "UREVO_MODE" ;;
    *) echo "Unknown variant: $1" >&2; exit 1 ;;
  esac
}

merge_factory() {
  local out="$1"
  "$PY" "$ESP" --chip esp32 merge_bin -o "$out" \
    --flash_mode "$FLASH_MODE" --flash_freq "$FLASH_FREQ" --flash_size "$FLASH_SIZE" \
    0x1000  "$BUILD_DIR/bootloader.bin" \
    0x8000  "$BUILD_DIR/partitions.bin" \
    0xe000  "$BOOT_APP0" \
    0x10000 "$BUILD_DIR/firmware.bin"
}

build_variant() {
  local variant="$1"
  local macro; macro="$(macro_for_variant "$variant")"

  echo "=== Building ${variant} (-D ${macro}) ==="

  # Clean to ensure flags take effect
  $PIO run -e "$ENV_NAME" -t clean >/dev/null

  # Inject flags via ${sysenv.EXTRA_FLAGS} in platformio.ini
  EXTRA_FLAGS="-D RELEASE_BUILD -D ${macro}" \
  $PIO run -e "$ENV_NAME"

  # Sanity
  for f in bootloader.bin partitions.bin firmware.bin; do
    [[ -f "$BUILD_DIR/$f" ]] || { echo "Missing $BUILD_DIR/$f" >&2; exit 1; }
  done
  [[ -f "$BOOT_APP0" ]] || { echo "Missing $BOOT_APP0" >&2; exit 1; }

  # Merge
  local factory="$BUILD_DIR/factory.bin"
  merge_factory "$factory"
  [[ -f "$factory" ]] || { echo "Failed to create $factory" >&2; exit 1; }

  # Copy to releases/<variant>/firmware.bin
  local outdir="$RELEASES_BASE/$variant"
  mkdir -p "$outdir"
  cp -f "$factory" "$outdir/firmware.bin"
  echo "✓ ${variant} -> $outdir/firmware.bin"
}

# -------- PRECHECKS --------
[[ -x "$PY" ]]  || { echo "PlatformIO Python not found at $PY" >&2; exit 1; }
[[ -f "$ESP" ]] || { echo "esptool.py not found at $ESP" >&2; exit 1; }

# -------- MAIN --------
if [[ $# -gt 0 ]]; then
  for v in "$@"; do build_variant "$v"; done
else
  for v in $DEFAULT_VARIANTS; do build_variant "$v"; done
fi

echo "All done. Blake, reminder you need to manually edit the manifest version number!"
