#!/bin/sh
# Cut a firmware release: tag, build, publish a GitHub Release, optionally
# install it over Wi-Fi.
#
#   tools/release.sh 0.7.0                 tag v0.7.0 and publish
#   tools/release.sh 0.7.0 --install HOST  ...then install it on the monitor
#                                          (HOST e.g. 192.168.1.98)
#
# Requires: on main, everything committed and pushed, and a "## v0.7.0"
# section in CHANGELOG.md (used as the release notes).
#
# Release assets:
#   stove-monitor-vX.Y.Z.bin       app image for OTA (keeps settings)
#   stove-monitor-vX.Y.Z-full.bin  USB recovery image, flash at 0x0; ERASES
#                                  settings, Wi-Fi and the OTA password
set -e

VER="$1"
[ -n "$VER" ] || { echo "usage: $0 X.Y.Z [--install HOST]"; exit 1; }
echo "$VER" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' || { echo "version must look like 0.7.0"; exit 1; }
TAG="v$VER"
INSTALL_HOST=""
if [ "$2" = "--install" ]; then
  INSTALL_HOST="$3"
  [ -n "$INSTALL_HOST" ] || { echo "--install needs a host, e.g. 192.168.1.98"; exit 1; }
fi

cd "$(dirname "$0")/.."

# ---- preflight
[ "$(git rev-parse --abbrev-ref HEAD)" = "main" ] || { echo "not on main"; exit 1; }
[ -z "$(git status --porcelain)" ] || { echo "uncommitted changes:"; git status --short; exit 1; }
git fetch -q origin
[ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] || { echo "main is not in sync with origin/main (push or pull first)"; exit 1; }
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then echo "$TAG already exists"; exit 1; fi
NOTES="$(awk -v h="## $TAG" '$0 ~ "^## " { if (on) exit; on = (index($0, h) == 1); next } on' CHANGELOG.md)"
[ -n "$(echo "$NOTES" | tr -d '[:space:]')" ] || { echo "add a '## $TAG' section to CHANGELOG.md, commit and push it first"; exit 1; }

# ---- tag, then build (the build stamps itself from the tag)
git tag -a "$TAG" -m "Stove monitor $TAG"
trap 'echo "release failed; removing local tag $TAG"; git tag -d "$TAG" >/dev/null' EXIT

. "$HOME/esptool-env/bin/activate"
pio run -e wt32-sc01-plus
grep -q "\"$TAG\"" include/version_gen.h || { echo "build is not stamped $TAG:"; cat include/version_gen.h; exit 1; }

B=.pio/build/wt32-sc01-plus
APP="dist/stove-monitor-$TAG.bin"
FULL="dist/stove-monitor-$TAG-full.bin"
mkdir -p dist
cp "$B/firmware.bin" "$APP"
python -m esptool --chip esp32s3 merge-bin -o "$FULL" \
  0x0 "$B/bootloader.bin" 0x8000 "$B/partitions.bin" \
  0xe000 "$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" \
  0x10000 "$B/firmware.bin"

# ---- publish
git push -q origin "$TAG"
trap - EXIT
gh release create "$TAG" "$APP" "$FULL" --title "$TAG" --notes "$NOTES

---
**Install over Wi-Fi:** \`stove-monitor-$TAG.bin\` (settings are kept).
**USB recovery:** \`stove-monitor-$TAG-full.bin\` at offset 0x0 - this erases settings, Wi-Fi and the OTA password."
echo "published $TAG"

# ---- optional install over Wi-Fi (password passed on stdin, never in argv)
if [ -n "$INSTALL_HOST" ]; then
  PW_FILE="$HOME/.config/stove-monitor/ota_password"
  [ -f "$PW_FILE" ] || { echo "missing $PW_FILE"; exit 1; }
  echo "installing $TAG on $INSTALL_HOST..."
  printf 'user = "ota:%s"\n' "$(sed 's/[\\"]/\\&/g' "$PW_FILE")" |
    curl --fail --show-error -s -K - -F "firmware=@$APP" "http://$INSTALL_HOST/update"
fi
