#!/usr/bin/env bash

# Creates a Craft root for the Android build (arm64) inside the KDE Android
# image, with this repository's blueprints registered. Does nothing if the
# root is already set up.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"

ANDROID_IMAGE="${ANDROID_IMAGE:-invent-registry.kde.org/sysadmin/ci-images/android-qt611}"
CRAFT_ROOT="${CRAFT_ROOT:-$HOME/craft-kde-android}"

if ! command -v docker >/dev/null 2>&1; then
    echo "Error: Docker is required to set up Craft." >&2
    exit 1
fi

if [[ -f "$CRAFT_ROOT/craft/craftenv.sh" ]]; then
    echo "Craft root already set up: $CRAFT_ROOT"
    exit 0
fi

mkdir -p "$CRAFT_ROOT"
CRAFT_ROOT="$(cd -- "$CRAFT_ROOT" && pwd)"
# The image's user differs from the invoking user and must be able to write here.
chmod 777 "$CRAFT_ROOT"

docker run --rm \
    --volume "$CRAFT_ROOT:/home/user/CraftRoot" \
    --volume "$REPO_ROOT:/home/user/KalibreBox:ro" \
    --workdir /home/user \
    --entrypoint bash \
    "$ANDROID_IMAGE" \
    -eo pipefail -c '
        python3 -c "$(curl -fsSL https://invent.kde.org/packaging/craft/-/raw/master/setup/CraftBootstrap.py)" \
            --prefix /home/user/CraftRoot --use-defaults
        sed -i "/^\[Blueprints\]/a Locations = /home/user/KalibreBox/craft" \
            /home/user/CraftRoot/etc/CraftSettings.ini
    '

echo "Craft root ready: $CRAFT_ROOT"
