import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Dialog {
    id: root
    // Filled in by LibraryPage just before open().
    property var book: ({
        title: "", authors: "", comment: "", formats: [], bookId: -1, path: ""
    })

    title: i18n("Book details")
    width: Kirigami.Units.gridUnit * 32
    padding: Kirigami.Units.largeSpacing

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Controls.Label {
            text: root.book.title
            font.bold: true
            font.pointSize: 14
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Controls.Label {
            visible: root.book.authors.length > 0
            text: root.book.authors
            font.italic: true
            opacity: 0.8
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Rectangle {
            color: Kirigami.Theme.alternateBackgroundColor
            height: 1
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing
        }

        Controls.Label {
            visible: root.book.comment.length > 0
            text: i18n("Description")
            font.capitalization: Font.AllUppercase
            opacity: 0.7
            Layout.fillWidth: true
        }
        Controls.Label {
            visible: root.book.comment.length > 0
            // Calibre comments are HTML; render as plain text in phase 4.
            text: root.book.comment.replace(/<[^>]*>/g, " ").replace(/\s+/g, " ").trim()
                  ? root.book.comment.replace(/<[^>]*>/g, " ").replace(/\s+/g, " ").trim()
                  : i18n("No description available.")
            wrapMode: Text.WordWrap
            maximumLineCount: 20
            elide: Text.ElideRight
            opacity: 0.85
            Layout.fillWidth: true
        }

        Controls.Label {
            text: i18n("Available formats")
            font.capitalization: Font.AllUppercase
            opacity: 0.7
            Layout.topMargin: Kirigami.Units.mediumSpacing
            Layout.fillWidth: true
        }

        Repeater {
            model: root.book.formats
            delegate: RowLayout {
                required property var modelData

                Controls.Label {
                    text: modelData.format
                    Layout.fillWidth: true
                }
                // Open with the system default application (downloads on demand).
                Controls.ToolButton {
                    icon.name: "document-open"
                    text: i18n("Open")
                    display: Controls.AbstractButton.TextBesideIcon
                    enabled: !library.busy
                    onClicked: library.openFormat(root.book.bookId, root.book.path, modelData.fileName, modelData.format)
                }
                // Save into the user's Downloads folder (as on Android).
                Controls.ToolButton {
                    icon.name: "document-save"
                    text: i18n("Download")
                    display: Controls.AbstractButton.TextBesideIcon
                    enabled: !library.busy
                    onClicked: library.saveFormat(
                        root.book.bookId, root.book.path, modelData.fileName,
                        modelData.format, "file://" + library.downloadsFolder())
                }
            }
        }
    }

    customFooterActions: [
        Kirigami.Action {
            text: i18n("Close")
            onTriggered: root.close()
        }
    ]
}
