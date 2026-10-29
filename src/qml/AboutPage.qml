pragma ComponentBehavior: Bound

import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.ki18n

Kirigami.AboutPage {
    aboutData: ({
            displayName: "KalibreBox",
            componentName: "kalibrebox",
            shortDescription: KI18n.i18n("Calibre e-book library manager for Dropbox"),
            homepage: "https://github.com/LyzardKing/KalibreBox",
            bugReport: "https://github.com/LyzardKing/KalibreBox/issues",
            version: Qt.application.version,
            copyrightStatement: "© 2026 Galileo Sartor",
            authors: [
                {
                    name: "Galileo Sartor",
                    task: "",
                    emailAddress: "",
                    webAddress: "",
                    ocsUsername: ""
                }
            ],
            credits: [],
            translators: [],
            licenses: [
                {
                    name: "MIT",
                    text: "© 2026 Galileo Sartor. Released under the MIT license; see the LICENSE file for the full text.",
                    spdx: "MIT"
                }
            ]
        })
}
