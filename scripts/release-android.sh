#!/usr/bin/env bash
set -euo pipefail

# Usage: scripts/release-android.sh [MAJOR.MINOR.PATCH]
#
# With a version: bumps it first (commit, push, tag; see scripts/bump).
# Without: releases the version currently in CMakeLists.txt, whose v<version>
# tag must already exist and be pushed.
# Then builds and signs the APK locally and publishes it as a GitHub Release.
# KEYSTORE and KEYSTORE_PASS are read as in scripts/sign-android.sh.

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

command -v gh >/dev/null 2>&1 || { echo "Error: the GitHub CLI (gh) is required." >&2; exit 1; }

if [[ $# -gt 1 ]]; then
    echo "Usage: $(basename "$0") [MAJOR.MINOR.PATCH]" >&2
    exit 2
fi

if [[ $# -eq 1 ]]; then
    "$SCRIPT_DIR/bump" "$1"
fi

VERSION="$(sed -nE '/^project\(/,/\)/ s/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt | head -n1)"
TAG="v${VERSION}"

if ! git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
    echo "Error: tag $TAG does not exist; pass a version to bump first." >&2
    exit 1
fi
if [[ "$(git rev-parse "$TAG^{commit}")" != "$(git rev-parse HEAD)" ]]; then
    echo "Error: HEAD is not at $TAG; check out the tagged commit or bump." >&2
    exit 1
fi
if gh release view "$TAG" >/dev/null 2>&1; then
    echo "Error: release $TAG already exists." >&2
    exit 1
fi

"$SCRIPT_DIR/build-android.sh"
"$SCRIPT_DIR/sign-android.sh"

gh release create "$TAG" dist/kalibrebox.apk --title "$TAG" --generate-notes --verify-tag
echo "Published $TAG."
