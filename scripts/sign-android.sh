#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"

ANDROID_IMAGE="${ANDROID_IMAGE:-invent-registry.kde.org/sysadmin/ci-images/android-qt611}"
CRAFT_ROOT="${CRAFT_ROOT:-$HOME/craft-kde-android}"
INPUT_APK="${INPUT_APK:-$REPO_ROOT/dist/kalibrebox-unsigned.apk}"
OUTPUT_APK="${OUTPUT_APK:-$REPO_ROOT/dist/kalibrebox.apk}"
KEYSTORE="${KEYSTORE:-$CRAFT_ROOT/tmp/key.keystore}"

if ! command -v docker >/dev/null 2>&1; then
    echo "Error: Docker is required to sign the Android APK." >&2
    exit 1
fi

if [[ ! -f "$INPUT_APK" ]]; then
    echo "Error: unsigned APK not found: $INPUT_APK (run scripts/build-android.sh first)" >&2
    exit 1
fi

if [[ ! -f "$KEYSTORE" ]]; then
    echo "Error: keystore not found: $KEYSTORE" >&2
    echo "Set KEYSTORE to the path of your signing keystore." >&2
    exit 1
fi

if [[ -z "${KEYSTORE_PASS:-}" ]]; then
    if [[ ! -t 0 ]]; then
        echo "Error: set KEYSTORE_PASS (no terminal available to prompt for it)." >&2
        exit 1
    fi
    read -r -s -p "Keystore password: " KEYSTORE_PASS
    echo
fi
export KEYSTORE_PASS

# The image's build tools are executable only by its own user, so the
# container cannot run as the invoking user; make the work dir writable by it.
work_dir="$(mktemp -d)"
trap 'rm -rf -- "$work_dir"' EXIT
chmod 777 "$work_dir"
cp -- "$INPUT_APK" "$work_dir/unsigned.apk"
chmod 644 "$work_dir/unsigned.apk"
# Copied instead of bind-mounted: a relative path would be taken by docker as a
# volume name, and a private (mode 600) keystore is unreadable to the container.
cp -- "$KEYSTORE" "$work_dir/keystore"
chmod 644 "$work_dir/keystore"

docker run --rm \
    --env KEYSTORE_PASS \
    --volume "$work_dir:/work" \
    --workdir /work \
    --entrypoint bash \
    "$ANDROID_IMAGE" \
    -euo pipefail -c '
        tools="$ANDROID_HOME/build-tools/$ANDROID_BUILD_TOOLS_REVISION"
        "$tools/zipalign" -p -f -v 4 unsigned.apk signed.apk
        "$tools/apksigner" sign --ks keystore --ks-pass env:KEYSTORE_PASS signed.apk
        "$tools/apksigner" verify signed.apk
    '

mkdir -p "$(dirname -- "$OUTPUT_APK")"
cp -- "$work_dir/signed.apk" "$OUTPUT_APK"
printf 'Signed APK: %s\n' "$OUTPUT_APK"
