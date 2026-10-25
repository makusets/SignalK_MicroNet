#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
MICRONET_ENV_DIR="${MICRONET_ENV_DIR:-/workspace/.venvs/micronet}"
python3 -m venv "$MICRONET_ENV_DIR"
"$MICRONET_ENV_DIR/bin/python" -m pip install --disable-pip-version-check -r requirements-dev.txt
# Only create an ignored placeholder file when no user secrets exist.
if [ ! -e examples/secrets.yaml ]; then
  cp examples/secrets.yaml.example examples/secrets.yaml
fi
"$MICRONET_ENV_DIR/bin/python" -m pytest -q
"$MICRONET_ENV_DIR/bin/esphome" config examples/lolin-d32.yaml >/dev/null
"$MICRONET_ENV_DIR/bin/esphome" config examples/all-sensors.yaml >/dev/null
"$MICRONET_ENV_DIR/bin/esphome" config examples/passive.yaml >/dev/null
