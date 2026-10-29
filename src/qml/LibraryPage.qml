pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.ki18n

Kirigami.Page {
    id: root
    title: LibraryController.hasLibrary ? LibraryController.libraryName : KI18n.i18n("Library")
    leftPadding: Qt.platform.os === "android" ? 0 : Kirigami.Units.largeSpacing
    rightPadding: 0

    function openIfAwaiting() {
        if (LibraryController.awaitingAuthCode)
            signInDialog.open();
    }

    Connections {
        target: LibraryController
        function onAwaitingAuthCodeChanged() {
            if (LibraryController.awaitingAuthCode)
                signInDialog.open();
        }
    }

    // The Add Library dialog now lives in Main.qml (sidebar action).
    signal addLibraryRequested
    signal bookDetailsRequested(var book)

    readonly property Kirigami.Action addLibraryAction: Kirigami.Action {
        text: KI18n.i18n("Add library")
        onTriggered: root.addLibraryRequested()
    }
    readonly property Kirigami.Action signInAction: Kirigami.Action {
        text: KI18n.i18n("Sign in to Dropbox")
        onTriggered: signInDialog.open()
    }

    header: ColumnLayout {
        spacing: 0

        RowLayout {
            Kirigami.SearchField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: LibraryController.isOpds ? KI18n.i18n("Search the catalogue…") : KI18n.i18n("Filter by title or author…")
                text: LibraryController.searchQuery
                onTextChanged: LibraryController.searchQuery = text
            }
            Controls.BusyIndicator {
                visible: LibraryController.syncing
                running: LibraryController.syncing
            }
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: LibraryController.status.length > 0
            type: LibraryController.syncing ? Kirigami.MessageType.Information : Kirigami.MessageType.Warning
            text: LibraryController.status
        }
    }

    Kirigami.Dialog {
        id: signInDialog
        title: KI18n.i18n("Dropbox sign-in")
        padding: Kirigami.Units.largeSpacing
        preferredWidth: Kirigami.Units.gridUnit * 24

        contentItem: ColumnLayout {
            Controls.Label {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                wrapMode: Text.WordWrap
                text: LibraryController.awaitingAuthCode ? KI18n.i18n("Paste the code shown by Dropbox in the browser:") : KI18n.i18n("A browser tab will open for the Dropbox sign-in.")
            }
            Controls.TextField {
                id: authCodeField
                visible: LibraryController.awaitingAuthCode
                Layout.fillWidth: true
                placeholderText: KI18n.i18n("Authorization code")
            }
        }

        customFooterActions: [
            Kirigami.Action {
                text: LibraryController.awaitingAuthCode ? KI18n.i18n("Continue") : KI18n.i18n("Open browser")
                onTriggered: {
                    if (LibraryController.awaitingAuthCode) {
                        LibraryController.submitAuthCode(authCodeField.text);
                        authCodeField.clear();
                        signInDialog.close();
                    } else {
                        // Keep the dialog open: it switches to the
                        // code-entry form once Dropbox asks for a code.
                        LibraryController.signIn();
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
        reuseItems: true
        cacheBuffer: cellHeight * 2

        readonly property real coverWidth: Kirigami.Units.gridUnit * 6.5
        readonly property real coverHeight: coverWidth * 1.5
        readonly property real gap: Kirigami.Units.largeSpacing
        readonly property real sidePadding: gap
        readonly property int columns: Math.max(1, Math.floor((width - leftMargin - rightMargin + gap) / (coverWidth + gap)))

        model: LibraryController.books

        // Desktop reserves scrollbar width; Android keeps the outer margin equal to the tile gap.
        Controls.ScrollBar.vertical: Controls.ScrollBar {
            id: vbar
        }

        // Download covers for what is on screen first (debounced while flicking).
        function prioritizeVisible() {
            const rowHeight = cellHeight;
            const first = Math.floor(Math.max(0, contentY) / rowHeight) * columns;
            const last = (Math.ceil((contentY + height) / rowHeight) + 1) * columns - 1;
            LibraryController.prioritizeCovers(first, last);
        }
        // Catalogues load a batch at a time: ask for the next one at the end of
        // the list, or when the loaded books do not fill the view yet.
        function loadMoreIfNeeded() {
            if (count > 0 && LibraryController.canLoadMore && (atYEnd || contentHeight <= height))
                LibraryController.loadMore();
        }
        onMovementEnded: prioritizeTimer.restart()
        onCountChanged: {
            prioritizeTimer.restart();
            loadMoreIfNeeded();
        }
        onAtYEndChanged: loadMoreIfNeeded()
        Connections {
            target: LibraryController
            function onCoversQueued() {
                grid.prioritizeVisible();
            }
            function onCanLoadMoreChanged() {
                grid.loadMoreIfNeeded();
            }
        }
        Timer {
            id: prioritizeTimer
            interval: 150
            onTriggered: grid.prioritizeVisible()
        }

        leftMargin: sidePadding
        rightMargin: sidePadding + (Qt.platform.os === "android" ? 0 : vbar.implicitWidth)
        topMargin: Kirigami.Units.smallSpacing
        bottomMargin: Kirigami.Units.smallSpacing

        cellWidth: (width - leftMargin - rightMargin) / columns
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
                    const m = cell.model;
                    root.bookDetailsRequested({
                        bookId: m.bookId,
                        title: m.title,
                        authors: m.authors,
                        comment: m.comment,
                        path: m.path,
                        formats: m.formats,
                        cover: m.cover
                    });
                    // Catalogue stubs only learn their formats when opened.
                    if (m.needsDetails)
                        LibraryController.loadBookDetails(m.bookId);
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
            visible: grid.count === 0 && !(LibraryController.isOpds && LibraryController.syncing)
            icon.name: "bookmarks-organize"
            text: !LibraryController.hasLibrary ? KI18n.i18n("No library configured") : KI18n.i18n("No books found")
            explanation: !LibraryController.hasLibrary ? (LibraryController.signedIn ? KI18n.i18n("Add a Dropbox Calibre library to get started.") : KI18n.i18n("Sign in to Dropbox first, then add a Calibre library.")) : (LibraryController.searchQuery.length ? KI18n.i18n("Nothing matches the current filter.") : (LibraryController.isOpds ? KI18n.i18n("The catalogue returned no books. Check its address and your connection.") : KI18n.i18n("Syncing may still be running, or the library is empty."))
            )
            // Catalogues have nothing to sign in to or add.
            helpfulAction: LibraryController.hasLibrary && LibraryController.isOpds ? null : (LibraryController.signedIn ? root.addLibraryAction : root.signInAction)
        }
    }
}
