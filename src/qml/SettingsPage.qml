pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.ki18n

Kirigami.ScrollablePage {
    id: root

    signal folderSelectionRequested

    title: KI18n.i18n("Settings")

    Kirigami.FormLayout {
        ColumnLayout {
            Kirigami.FormData.label: KI18n.i18n("Download folder")
            Layout.fillWidth: true

            Controls.CheckBox {
                id: publicDownloadsCheck
                visible: Qt.platform.os === "android"
                text: KI18n.i18n("Downloads")
                checked: LibraryController.usePublicDownloads
                onClicked: {
                    if (checked) {
                        LibraryController.usePublicDownloads = true;
                    } else {
                        checked = true;
                        root.folderSelectionRequested();
                    }
                }
            }
            Controls.Button {
                visible: Qt.platform.os !== "android" || !LibraryController.usePublicDownloads
                icon.name: "folder"
                text: LibraryController.downloadFolderPath
                Layout.fillWidth: true
                onClicked: root.folderSelectionRequested()
            }
        }
        Connections {
            target: LibraryController
            function onUsePublicDownloadsChanged() {
                publicDownloadsCheck.checked = LibraryController.usePublicDownloads;
            }
        }
        Controls.SpinBox {
            Kirigami.FormData.label: KI18n.i18n("Catalogue batch size")
            from: 1
            to: 500
            editable: true
            value: LibraryController.opdsBatchSize
            onValueModified: LibraryController.opdsBatchSize = value
            Controls.ToolTip.visible: hovered
            Controls.ToolTip.text: KI18n.i18n("Books loaded at a time from OPDS catalogues; more load as you scroll.")
        }
        Controls.ComboBox {
            visible: Qt.platform.os == "android"
            Kirigami.FormData.label: KI18n.i18n("Theme")
            model: [KI18n.i18n("System"), KI18n.i18n("Light"), KI18n.i18n("Dark")]
            currentIndex: LibraryController.theme
            onActivated: index => LibraryController.theme = index
        }
    }
}
