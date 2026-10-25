import QtQuick
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

    pageStack.initialPage: LibraryPage {
    }
}
