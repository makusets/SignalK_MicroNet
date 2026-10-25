#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
MICRONET_ENV_DIR="${MICRONET_ENV_DIR:-/workspace/.venvs/micronet}"
export XDG_CACHE_HOME="${XDG_CACHE_HOME:-/workspace/.cache}"
export PLATFORMIO_CORE_DIR="${PLATFORMIO_CORE_DIR:-/workspace/.platformio}"
"$MICRONET_ENV_DIR/bin/python" -m pytest -q
"$MICRONET_ENV_DIR/bin/esphome" config examples/lolin-d32.yaml >/dev/null
"$MICRONET_ENV_DIR/bin/esphome" config examples/all-sensors.yaml >/dev/null
if [ "${1:-}" = "--compile" ]; then
  "$MICRONET_ENV_DIR/bin/esphome" compile examples/lolin-d32.yaml
  "$MICRONET_ENV_DIR/bin/esphome" compile examples/all-sensors.yaml
fi
