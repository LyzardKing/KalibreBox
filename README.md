# KalibreBox

Calibre e-book library manager for Dropbox, built with [Kirigami](https://develop.kde.org/docs/use/kirigami/) (Qt 6 / KDE Frameworks).

A Qt/KDE rewrite of [CalibreBoxNew](https://github.com/galileo/CalibreBoxNew) — browse, search, download, and manage a Calibre library hosted on Dropbox.

## Status

Early development. Target: Linux desktop first, Android later.

- [ ] Data layer (SQLite, Calibre `metadata.db` schema)
- [x] Dropbox client (OAuth 2 + PKCE, token refresh, list/search/download/upload)
- [ ] Book details / send-to-Kobo
- [x] Kirigami UI (library, file browser, library management)
- [x] Flatpak packaging
- [ ] Android port

## Requirements

- Qt 6.8+ (Core, Gui, Qml, Quick, QuickControls2, Sql, Network)
- Kirigami 6 QML module (`qml6-module-org-kde-kirigami`)
- CMake 3.20+

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/kalibrebox
```

## License

MIT — see [LICENSE](LICENSE).
