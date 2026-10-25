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
                text: i18n("Libraries")
                icon.name: "view-list"
            },
            Kirigami.Action {
                text: i18n("Add library")
                icon.name: "list-add"
            },
            Kirigami.Action {
                text: i18n("Settings")
                icon.name: "settings-configure"
            },
            Kirigami.Action {
                text: i18n("Developer testing")
                icon.name: "applications-development"
                onTriggered: pageStack.push(Qt.resolvedUrl("DevPage.qml"))
            }
        ]
    }

    pageStack.initialPage: Kirigami.Page {
        title: i18n("Library")

        actions: [
            Kirigami.Action {
                icon.name: "edit-find"
                text: i18n("Search")
            },
            Kirigami.Action {
                icon.name: "view-sort-descending"
                text: i18n("Sort")
            }
        ]
    }
}
