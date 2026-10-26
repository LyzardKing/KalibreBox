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
        preferredWidth: Kirigami.Units.gridUnit * 24

        contentItem: ColumnLayout {
            Controls.Label {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
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

    // Cover-tile grid in the spirit of the Android app (Compose adaptive grid
    // of 120x180dp cards), sized from Kirigami units so it follows the
    // platform's scaling.
    GridView {
        id: grid
        anchors.fill: parent
        clip: true

        readonly property real coverWidth: Kirigami.Units.gridUnit * 6.5
        readonly property real coverHeight: coverWidth * 1.5
        readonly property real gap: Kirigami.Units.largeSpacing
        readonly property real sidePadding: Kirigami.Units.largeSpacing
        readonly property int columns: Math.max(1, Math.floor(
            (width - 2 * sidePadding + gap) / (coverWidth + gap)))

        model: library.books

        leftMargin: sidePadding
        rightMargin: sidePadding
        topMargin: Kirigami.Units.smallSpacing
        bottomMargin: Kirigami.Units.smallSpacing

        cellWidth: (width - 2 * sidePadding) / columns
        cellHeight: coverHeight + Kirigami.Units.gridUnit * 3 + gap

        delegate: Item {
            id: cell
            required property int index
            required property var model

            width: grid.cellWidth
            height: grid.cellHeight

            // AbstractCard supplies the card background and press feedback.
            Kirigami.AbstractCard {
                id: tileDelegate
                readonly property var model: cell.model
                anchors.horizontalCenter: parent.horizontalCenter
                width: grid.coverWidth + 2 * padding
                height: parent.height - grid.gap
                padding: Kirigami.Units.smallSpacing
                showClickFeedback: true
                onClicked: {
                    // Snapshot: the delegate's model object dies with the delegate.
                    const m = cell.model
                    detailsDialog.book = {
                        bookId: m.bookId, title: m.title, authors: m.authors,
                        comment: m.comment, path: m.path, formats: m.formats,
                        cover: m.cover
                    }
                    detailsDialog.open()
                }

                contentItem: Column {
                    topPadding: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.ShadowedRectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: grid.coverWidth
                        height: grid.coverHeight
                        radius: Kirigami.Units.cornerRadius
                        color: Kirigami.Theme.alternateBackgroundColor
                        shadow.size: Kirigami.Units.smallSpacing
                        shadow.color: Qt.rgba(0, 0, 0, 0.3)

                        Image {
                            anchors.fill: parent
                            asynchronous: true
                            fillMode: Image.PreserveAspectCrop
                            sourceSize.width: width
                            visible: tileDelegate.model.cover !== ""
                            source: tileDelegate.model.cover
                        }
                        Kirigami.Icon {
                            anchors.centerIn: parent
                            width: Kirigami.Units.iconSizes.large
                            height: width
                            source: "application-epub+zip"
                            visible: tileDelegate.model.cover === ""
                        }
                    }
                    Controls.Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: grid.coverWidth
                        text: tileDelegate.model.title
                        maximumLineCount: 2
                        wrapMode: Text.WordWrap
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter
                        font: Kirigami.Theme.smallFont
                    }
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
