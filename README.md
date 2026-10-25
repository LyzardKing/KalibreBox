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

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/kalibrebox
```

## License

MIT — see [LICENSE](LICENSE).
