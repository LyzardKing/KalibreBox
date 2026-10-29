pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.ki18n

Kirigami.Page {
    id: root
    property var book: ({
            title: "",
            authors: "",
            comment: "",
            formats: [],
            bookId: -1,
            path: "",
            cover: ""
        })

    readonly property string plainDescription: book.comment.replace(/<[^>]*>/g, " ").replace(/[ \t\r\f]+/g, " ").replace(/ ?\n ?/g, "\n").replace(/\n{2,}/g, "\n").trim()

    title: root.book.title || KI18n.i18n("Book details")

    Controls.Drawer {
        id: coverDrawer
        edge: Qt.RightEdge
        width: root.width * 0.8
        height: root.height

        contentItem: Image {
            source: root.book.cover
            fillMode: Image.PreserveAspectFit
        }
    }

    Connections {
        target: LibraryController
        function onBookDetailsLoaded(bookId) {
            if (root.book.bookId === bookId)
                root.book = LibraryController.bookDetails(bookId);
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Kirigami.Units.smallSpacing

        Controls.Label {
            visible: root.book.authors.length > 0
            text: root.book.authors
            font.italic: true
            opacity: 0.8
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.preferredWidth: 1
        }

        Rectangle {
            color: Kirigami.Theme.alternateBackgroundColor
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            Layout.topMargin: Kirigami.Units.smallSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing
        }

        GridLayout {
            id: detailsGrid

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0

            columns: root.width < root.height || width < Kirigami.Units.gridUnit * 36 ? 1 : 2

            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.largeSpacing

            Image {
                id: coverImage
                asynchronous: true
                source: root.book.cover
                fillMode: Image.PreserveAspectFit
                visible: root.book.cover !== ""
                Layout.preferredWidth: parent.width / 3
                Layout.preferredHeight: coverImage.implicitWidth > 0 ? parent.width / 3 * coverImage.implicitHeight / coverImage.implicitWidth : 0
                Layout.maximumHeight: 200
                Layout.alignment: Qt.AlignHCenter

                TapHandler {
                    onTapped: coverDrawer.open()
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 0
    
                Controls.Label {
                    visible: root.book.comment.length > 0
                    text: KI18n.i18n("Description")
                    font.capitalization: Font.AllUppercase
                    opacity: 0.7
                    Layout.fillWidth: true
                }
                Controls.ScrollView {
                    visible: root.book.comment.length > 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 0
                    Layout.preferredWidth: 1

                    Controls.TextArea {
                        width: parent.width
                        text: root.plainDescription || KI18n.i18n("No description available.")
                        readOnly: true
                        wrapMode: TextEdit.Wrap
                        opacity: 0.85
                        padding: 0
                        background: null
                    }
                }
            }
        }

        Controls.Label {
            text: KI18n.i18n("Available formats")
            font.capitalization: Font.AllUppercase
            opacity: 0.7
            Layout.topMargin: Kirigami.Units.mediumSpacing
            Layout.fillWidth: true
        }

        RowLayout {
            visible: LibraryController.detailsLoading && root.book.formats.length === 0
            spacing: Kirigami.Units.smallSpacing

            Controls.BusyIndicator {
                running: parent.visible
            }
            Controls.Label {
                text: KI18n.i18n("Loading formats…")
                opacity: 0.7
            }
        }
        Controls.Label {
            visible: !LibraryController.detailsLoading && root.book.formats.length === 0
            text: KI18n.i18n("No downloadable formats.")
            opacity: 0.7
        }

        Repeater {
            model: root.book.formats
            delegate: RowLayout {
                id: formatRow
                required property var modelData

                Controls.Label {
                    text: formatRow.modelData.format
                    Layout.fillWidth: true
                }
                // Open with the system default application (downloads on demand).
                Controls.ToolButton {
                    icon.name: "document-open"
                    text: KI18n.i18n("Open")
                    display: Controls.AbstractButton.TextBesideIcon
                    enabled: !LibraryController.busy
                    onClicked: LibraryController.openFormat(root.book.bookId, root.book.path, formatRow.modelData.fileName, formatRow.modelData.format)
                }
                // Share sheet; Android only.
                Controls.ToolButton {
                    visible: Qt.platform.os === "android"
                    icon.name: "document-share"
                    text: KI18n.i18n("Share")
                    display: Controls.AbstractButton.TextBesideIcon
                    enabled: !LibraryController.busy
                    onClicked: LibraryController.shareFormat(root.book.bookId, root.book.path, formatRow.modelData.fileName, formatRow.modelData.format)
                }
                // Save into the user's Downloads folder (as on Android).
                Controls.ToolButton {
                    icon.name: "document-save"
                    text: KI18n.i18n("Download")
                    display: Controls.AbstractButton.TextBesideIcon
                    enabled: !LibraryController.busy
                    onClicked: LibraryController.saveFormat(root.book.bookId, root.book.path, formatRow.modelData.fileName, formatRow.modelData.format, LibraryController.downloadFolder)
                }
            }
        }
    }
}
