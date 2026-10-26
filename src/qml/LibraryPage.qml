import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: root
    title: library.hasLibrary ? library.libraryName : i18n("Library")

    function openIfAwaiting() {
        if (library.awaitingAuthCode)
            signInDialog.open()
    }

    Connections {
        target: library
        function onAwaitingAuthCodeChanged() {
            if (library.awaitingAuthCode)
                signInDialog.open()
        }
    }

    BookDetailsDialog {
        id: detailsDialog
    }
    // The Add Library dialog now lives in Main.qml (sidebar action).
    signal addLibraryRequested()

    readonly property Kirigami.Action addLibraryAction: Kirigami.Action {
        text: i18n("Add library")
        onTriggered: root.addLibraryRequested()
    }
    readonly property Kirigami.Action signInAction: Kirigami.Action {
        text: i18n("Sign in to Dropbox")
        onTriggered: signInDialog.open()
    }

    header: ColumnLayout {
        spacing: 0

        RowLayout {
            Controls.TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: i18n("Filter by title or author…")
                text: library.searchQuery
                onTextChanged: library.searchQuery = text
            }
            Controls.BusyIndicator {
                visible: library.syncing
                running: library.syncing
            }
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: library.status.length > 0
            type: library.syncing ? Kirigami.MessageType.Information
                                  : Kirigami.MessageType.Warning
            text: library.status
        }
    }

    Kirigami.Dialog {
        id: signInDialog
        title: i18n("Dropbox sign-in")
        padding: Kirigami.Units.largeSpacing

        contentItem: ColumnLayout {
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: library.awaitingAuthCode
                      ? i18n("Paste the code shown by Dropbox in the browser:")
                      : i18n("A browser tab will open for the Dropbox sign-in.")
            }
            Controls.TextField {
                id: authCodeField
                visible: library.awaitingAuthCode
                Layout.fillWidth: true
                placeholderText: i18n("Authorization code")
            }
        }

        customFooterActions: [
            Kirigami.Action {
                text: library.awaitingAuthCode ? i18n("Continue") : i18n("Open browser")
                onTriggered: {
                    if (library.awaitingAuthCode) {
                        library.submitAuthCode(authCodeField.text)
                        authCodeField.clear()
                        signInDialog.close()
                    } else {
                        // Keep the dialog open: it switches to the
                        // code-entry form once Dropbox asks for a code.
                        library.signIn()
                    }
                }
            }
        ]
    }

    GridView {
        id: grid
        anchors.fill: parent
        clip: true

        model: library.books

        cellWidth: {
            const target = Kirigami.Units.gridUnit * 12;
            const perRow = Math.max(1, Math.floor(width / target));
            return Math.floor(width / perRow);
        }
        cellHeight: cellWidth * 1.75 + Kirigami.Units.gridUnit * 3

        delegate: Item {
            id: tileDelegate
            required property int index
            required property var model

            width: grid.cellWidth
            height: grid.cellHeight

            TapHandler {
                onSingleTapped: {
                    detailsDialog.book = tileDelegate.model
                    detailsDialog.open()
                }
            }

            Column {
                x: Kirigami.Units.smallSpacing
                y: Kirigami.Units.smallSpacing
                width: parent.width - 2 * Kirigami.Units.smallSpacing
                spacing: Kirigami.Units.smallSpacing

                Rectangle {
                    width: parent.width
                    height: grid.cellWidth * 1.4
                    color: Kirigami.Theme.alternateBackgroundColor

                    Image {
                        anchors.fill: parent
                        anchors.margins: 1
                        asynchronous: true
                        fillMode: Image.PreserveAspectCrop
                        sourceSize.width: width
                        visible: model.cover !== ""
                        source: model.cover
                    }
                    Kirigami.Icon {
                        anchors.centerIn: parent
                        width: parent.width / 3
                        height: width
                        source: "application-epub+zip"
                        visible: model.cover === ""
                    }
                }
                Controls.Label {
                    width: parent.width
                    text: model.title
                    maximumLineCount: 2
                    wrapMode: Text.WordWrap
                    elide: Text.ElideRight
                    font.bold: true
                }
                Controls.Label {
                    width: parent.width
                    text: model.authors
                    elide: Text.ElideRight
                    opacity: 0.7
                }
            }
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - 2 * Kirigami.Units.largeSpacing
            visible: grid.count === 0
            icon.name: "bookmarks-organize"
            text: !library.hasLibrary
                  ? i18n("No library configured")
                  : i18n("No books found")
            explanation: !library.hasLibrary
                  ? (library.signedIn
                     ? i18n("Add a Dropbox Calibre library to get started.")
                     : i18n("Sign in to Dropbox first, then add a Calibre library."))
                  : (library.searchQuery.length
                     ? i18n("Nothing matches the current filter.")
                     : i18n("Syncing may still be running, or the library is empty."))
            helpfulAction: library.signedIn
                ? root.addLibraryAction
                : root.signInAction
        }
    }
}
