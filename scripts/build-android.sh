#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"

ANDROID_IMAGE="${ANDROID_IMAGE:-invent-registry.kde.org/sysadmin/ci-images/android-qt611}"
CRAFT_ROOT="${CRAFT_ROOT:-$HOME/craft-kde-android}"
OUTPUT_APK="${OUTPUT_APK:-$REPO_ROOT/dist/kalibrebox-unsigned.apk}"

if ! command -v docker >/dev/null 2>&1; then
    echo "Error: Docker is required to build the Android APK." >&2
    exit 1
fi

if [[ ! -d "$CRAFT_ROOT" ]]; then
    echo "Error: Craft root does not exist: $CRAFT_ROOT" >&2
    echo "Set CRAFT_ROOT to a configured Craft root and try again." >&2
    exit 1
fi

"$SCRIPT_DIR/bootstrap-android-craft.sh"

CRAFT_ROOT="$(cd -- "$CRAFT_ROOT" && pwd)"
mkdir -p "$(dirname -- "$OUTPUT_APK")"
OUTPUT_APK="$(cd -- "$(dirname -- "$OUTPUT_APK")" && pwd)/$(basename -- "$OUTPUT_APK")"

docker run --rm \
    --volume "$CRAFT_ROOT:/home/user/CraftRoot" \
    --volume "$REPO_ROOT:/home/user/KalibreBox" \
    --workdir /home/user/KalibreBox \
    --entrypoint bash \
    "$ANDROID_IMAGE" \
    -eo pipefail -c '
        source /home/user/CraftRoot/craft/craftenv.sh
        craft --install-deps apps/kalibrebox
        craft --options apps/kalibrebox.srcDir=/home/user/KalibreBox \
            --configure --make apps/kalibrebox
        cd /home/user/CraftRoot/build/apps/kalibrebox/work/build
        cmake --install . --prefix "$PWD/android-icons"
        cmake --build . --target apk
    '

BUILT_APK="$CRAFT_ROOT/build/apps/kalibrebox/work/build/android-build/build/outputs/apk/release/android-build-release-unsigned.apk"
if [[ ! -s "$BUILT_APK" ]]; then
    echo "Error: the build completed but the expected APK was not found: $BUILT_APK" >&2
    exit 1
fi

cp "$BUILT_APK" "$OUTPUT_APK"
printf 'Unsigned APK: %s\n' "$OUTPUT_APK"
