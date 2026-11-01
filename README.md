# KalibreBox

Calibre e-book library manager for Dropbox, built with [Kirigami](https://develop.kde.org/docs/use/kirigami/) (Qt 6 / KDE Frameworks).

A Qt/KDE rewrite of [CalibreBoxNew](https://github.com/galileo/CalibreBoxNew) — browse, search, download, and manage a Calibre library hosted on Dropbox.

## Status

Early development. Target: Linux desktop first, Android later.

- [x] Data layer (SQLite, Calibre `metadata.db` schema)
- [x] Dropbox client (OAuth 2 + PKCE with token refresh, list folder, get metadata,
      file download incl. shared links, copy-to-folder for send-to-Kobo, delete)
- [ ] UI (Kirigami app shell running, feature pages pending)
- [ ] Book details / send-to-Kobo integration
- [ ] Flatpak packaging
- [ ] Android port

## Requirements

- Qt 6.8+ (Core, Gui, Qml, Quick, QuickControls2, Sql, Network)
- Kirigami 6 QML module (`qml6-module-org-kde-kirigami`)
- CMake 3.20+

## Configuration

Create a gitignored `local.properties` file next to `CMakeLists.txt`:

```properties
dropbox.key=your-app-key
```

On the Dropbox side (App Console → your app → Settings → OAuth 2), register the
redirect URI **exactly** as:

```
http://127.0.0.1:61196/callback
```

The port must be stable because Dropbox matches redirect URIs exactly. Change
it with `dropbox.oauthPort=...` (env: `KALIBREBOX_DROPBOX_OAUTH_PORT`) — and
register the matching URI in the App Console. No app secret is required; the
PKCE authorization-code flow is used.

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/kalibrebox
```

## Build for Android

Build with the same KDE Android image used for local Craft builds:

```sh
make build-android
```

This runs `scripts/build-android.sh` and writes the unsigned APK to
`dist/kalibrebox-unsigned.apk`. If `~/craft-kde-android` (override with
`CRAFT_ROOT=/path`) has no Craft root yet, `scripts/bootstrap-android-craft.sh`
creates one inside the Android image (arm64) and registers this repository's
`craft` directory as a blueprint location. `ANDROID_IMAGE` and `OUTPUT_APK` can
also be overridden.

Releases are built and published locally: `make release version=X.Y.Z` (or
`scripts/release-android.sh X.Y.Z`) bumps the version, tags and pushes, builds
and signs the APK, and uploads it with `gh release create`. Without `version`
it releases the current version, which must already be tagged. This needs
`gh` logged in, plus `KEYSTORE` and `KEYSTORE_PASS` (see below).

To sign the APK, run `make sign-android` (or `scripts/sign-android.sh`). It
zip-aligns, signs and verifies `dist/kalibrebox-unsigned.apk` inside the same
Docker image and writes `dist/kalibrebox.apk`. Configure it with `KEYSTORE`
(default `$CRAFT_ROOT/tmp/key.keystore`), `KEYSTORE_PASS` (prompted for when
unset and a terminal is available), and optionally `INPUT_APK`
and `OUTPUT_APK`.

Check the upstream Craft docs for any updates on Android builds.
https://develop.kde.org/docs/packaging/android/building_applications/

Notes:

- The OAuth flow used by default (paste-the-code, no redirect URI) works on
  Android too. Auth uses Qt Network Authorization (`QOAuth2AuthorizationCodeFlow`).
- The Dropbox app key comes from `local.properties` baked in at configure time;
  the same QSettings/env overrides as on desktop apply at runtime.

## License

MIT — see [LICENSE](LICENSE).
