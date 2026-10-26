import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root

    width: 800
    height: 600
    minimumWidth: 400
    minimumHeight: 400
    title: i18n("KalibreBox")

    globalDrawer: Kirigami.GlobalDrawer {
        title: "KalibreBox"
        titleIcon: "application-x-storybook"

        actions: [
            Kirigami.Action {
                text: i18n("Add library")
                icon.name: "list-add"
                onTriggered: addLibraryDialog.open()
            },
            Kirigami.Action {
                text: library.signedIn ? i18n("Sign out of Dropbox") : i18n("Sign in to Dropbox")
                icon.name: library.signedIn ? "network-disconnect" : "network-connect"
                onTriggered: {
                    if (library.signedIn)
                        library.signOut()
                    else
                        library.signIn()
                }
            },
            Kirigami.Action {
                text: i18n("Sync library")
                icon.name: "view-refresh"
                enabled: library.hasLibrary && !library.syncing
                onTriggered: library.syncLibrary()
            }
        ]
    }

    Kirigami.Dialog {
        id: addLibraryDialog
        title: i18n("Add Dropbox library")
        padding: Kirigami.Units.largeSpacing

        contentItem: ColumnLayout {
            Controls.Label {
                text: i18n("Dropbox folder or shared link for a Calibre library.")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.italic: true
            }
            Controls.TextField {
                id: libraryNameField
                Layout.fillWidth: true
                placeholderText: i18n("Display name (optional)")
            }
            Controls.TextField {
                id: libraryPathField
                Layout.fillWidth: true
                placeholderText: i18n("e.g. /CalibreLibrary")
            }
        }

        customFooterActions: [
            Kirigami.Action {
                text: i18n("Add library")
                enabled: !library.syncing && libraryPathField.text.length > 0
                onTriggered: {
                    library.addLibrary(libraryPathField.text, libraryNameField.text)
                    libraryNameField.clear()
                    libraryPathField.clear()
                    addLibraryDialog.close()
                }
            }
        ]
    }

    pageStack.initialPage: LibraryPage {
        onAddLibraryRequested: addLibraryDialog.open()
    }
}
