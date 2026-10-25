import QtQuick
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: root
    title: i18n("Developer testing")
    clip: true

    // Revision counters force the summary bindings below to re-evaluate
    // when the controller emits its change signals.
    property int librariesRev: 0
    property int booksRev: 0

    Connections {
        target: dev
        function onLibrariesChanged() { root.librariesRev++ }
        function onBooksChanged() { root.booksRev++ }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: column.height + Kirigami.Units.largeSpacing

        Column {
            id: column
            x: Kirigami.Units.largeSpacing
            width: parent.width - 2 * Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            // ---- Status ----
            Kirigami.Heading {
                level: 3
                text: i18n("Status")
            }
            Controls.Label {
                text: dev.status.length ? dev.status : i18n("Ready.")
                wrapMode: Text.WordWrap
            }

            // ---- Libraries ----
            Kirigami.Heading {
                level: 3
                text: i18n("Libraries")
            }
            Controls.TextField {
                id: libraryPathInput
                width: parent.width
                placeholderText: i18n("Path or shared URL, e.g. /Calibre Library")
            }
            Controls.TextField {
                id: libraryNameInput
                width: parent.width
                placeholderText: i18n("Display name (optional)")
            }
            Row {
                spacing: Kirigami.Units.smallSpacing
                Controls.Button {
                    text: i18n("Add library")
                    onClicked: dev.addLibrary(libraryPathInput.text, libraryNameInput.text)
                }
                Controls.Button {
                    text: i18n("Remove current")
                    onClicked: dev.removeCurrentLibrary()
                }
            }
            Controls.Label {
                property string libraryList: {
                    root.librariesRev;
                    const summaries = dev.librarySummaries();
                    return summaries.length ? summaries.join("\n") : i18n("(no libraries)");
                }
                text: libraryList
                wrapMode: Text.WordWrap
            }

            // ---- Local import ----
            Kirigami.Heading {
                level: 3
                text: i18n("Local database import")
            }
            Controls.TextField {
                id: importDbPathInput
                width: parent.width
                placeholderText: i18n("Path to an existing Calibre metadata.db")
            }
            Controls.Button {
                text: i18n("Import into current library")
                onClicked: dev.importLocalMetadataDb(importDbPathInput.text)
            }

            // ---- Dropbox ----
            Kirigami.Heading {
                level: 3
                text: i18n("Dropbox")
            }
            Controls.Label {
                text: dev.signedIn ? i18n("Signed in") : i18n("Not signed in")
            }
            Row {
                spacing: Kirigami.Units.smallSpacing
                Controls.Button {
                    text: i18n("Sign in")
                    onClicked: dev.startSignIn()
                }
                Controls.Button {
                    text: i18n("Sign out")
                    onClicked: dev.signOut()
                }
            }
            Column {
                width: parent.width
                spacing: Kirigami.Units.smallSpacing
                visible: dev.awaitingAuthCode
                Controls.Label {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: i18n(
                        "A web page just opened in your browser and shows an "
                            + "authorization code at the top. Copy it and paste it "
                            + "below, then press 'Submit code'.")
                }
                Row {
                    spacing: Kirigami.Units.smallSpacing
                    Controls.TextField {
                        id: authCodeInput
                        width: 2 * Kirigami.Units.gridUnit * 10
                        placeholderText: i18n("Paste the authorization code here")
                    }
                    Controls.Button {
                        text: i18n("Submit code")
                        onClicked: {
                            dev.submitAuthCode(authCodeInput.text)
                            authCodeInput.clear()
                        }
                    }
                }
            }
            Flow {
                width: parent.width
                spacing: Kirigami.Units.smallSpacing
                Controls.Button {
                    text: i18n("Browse folder")
                    onClicked: dev.browseLibraryFolder()
                }
                Controls.Button {
                    text: i18n("Check metadata.db")
                    onClicked: dev.checkRemoteMetadata()
                }
                Controls.Button {
                    text: i18n("Download metadata.db")
                    onClicked: dev.downloadMetadataDb()
                }
                Controls.Button {
                    text: i18n("Send first book to Kobo")
                    onClicked: dev.sendCurrentTestToKobo()
                }
            }

            // ---- Books ----
            Kirigami.Heading {
                level: 3
                text: i18n("Books of current library")
            }
            Controls.Label {
                property string bookList: {
                    root.booksRev;
                    root.librariesRev;
                    const titles = dev.bookTitles();
                    return titles.length ? titles.join("\n")
                        : i18n("(none — import or download a metadata.db first)");
                }
                text: bookList
                wrapMode: Text.WordWrap
            }
        }
    }
}
