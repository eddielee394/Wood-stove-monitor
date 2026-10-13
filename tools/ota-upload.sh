#!/bin/sh
# Build and upload firmware over Wi-Fi. The OTA password is read from a local
# file and passed via the environment so it never appears in git or logs.
set -e
PW_FILE="$HOME/.config/stove-monitor/ota_password"
[ -f "$PW_FILE" ] || { echo "missing $PW_FILE"; exit 1; }
STOVE_OTA_PASSWORD="$(cat "$PW_FILE")"
export STOVE_OTA_PASSWORD
cd "$(dirname "$0")/.."
. "$HOME/esptool-env/bin/activate"
exec pio run -e ota -t upload "$@"
