pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.kde.ki18n

Kirigami.Dialog {
    id: root
    // Filled in by LibraryPage just before open().
    property var book: ({
            title: "",
            authors: "",
            comment: "",
            formats: [],
            bookId: -1,
            path: ""
        })

    title: KI18n.i18n("Book details")
    // 80% of the window; maximumWidth must follow or it would cap the dialog.
    preferredWidth: (parent ? parent.width : Screen.width) * 0.8
    maximumWidth: preferredWidth
    padding: Kirigami.Units.largeSpacing

    ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Controls.Label {
            text: root.book.title
            font.bold: true
            font.pointSize: 14
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.preferredWidth: 1
        }
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
            // The height should be set by the parent layout
            // height: 1
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing
        }

        Controls.Label {
            visible: root.book.comment.length > 0
            text: KI18n.i18n("Description")
            font.capitalization: Font.AllUppercase
            opacity: 0.7
            Layout.fillWidth: true
        }
        Controls.ScrollView {
            id: descriptionScroll
            visible: root.book.comment.length > 0
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.maximumHeight: Kirigami.Units.gridUnit * 12
            Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
            // Content is exactly as wide as the view, so there is nothing to drag sideways.
            contentWidth: availableWidth

            // Belt and braces for touch: lock the inner Flickable to vertical.
            Component.onCompleted: {
                contentItem.flickableDirection = Flickable.VerticalFlick;
                contentItem.boundsBehavior = Flickable.StopAtBounds;
            }

            // A read-only TextArea inside a ScrollView wraps to the view width
            // and scrolls vertically only; the text is selectable too.
            Controls.TextArea {
                // Calibre comments are HTML; render as plain text in phase 4.
                text: root.book.comment.replace(/<[^>]*>/g, " ").replace(/\s+/g, " ").trim() ? root.book.comment.replace(/<[^>]*>/g, " ").replace(/\s+/g, " ").trim() : KI18n.i18n("No description available.")
                readOnly: true
                wrapMode: TextEdit.Wrap
                opacity: 0.85
                padding: 0
                background: null
            }
        }

        Controls.Label {
            text: KI18n.i18n("Available formats")
            font.capitalization: Font.AllUppercase
            opacity: 0.7
            Layout.topMargin: Kirigami.Units.mediumSpacing
            Layout.fillWidth: true
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
                    onClicked: LibraryController.saveFormat(root.book.bookId, root.book.path, formatRow.modelData.fileName, formatRow.modelData.format, "file://" + LibraryController.downloadsFolder())
                }
            }
        }
    }

    customFooterActions: [
        Kirigami.Action {
            text: KI18n.i18n("Close")
            onTriggered: root.close()
        }
    ]
}
