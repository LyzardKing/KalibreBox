import QtQuick
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: root
    title: library.hasLibrary ? library.libraryName : i18n("Library")

    // Sort order values mirror KalibreBox::SortOrder.
    actions: [
        Kirigami.Action {
            icon.name: "view-sort-descending"
            text: i18n("Sort")
            children: [
                Kirigami.Action {
                    text: i18n("Title")
                    checkable: true
                    checked: library.sortOrder === 0
                    onTriggered: library.sortOrder = 0
                },
                Kirigami.Action {
                    text: i18n("Author")
                    checkable: true
                    checked: library.sortOrder === 1
                    onTriggered: library.sortOrder = 1
                },
                Kirigami.Action {
                    text: i18n("Recently added")
                    checkable: true
                    checked: library.sortOrder === 2
                    onTriggered: library.sortOrder = 2
                }
            ]
        }
    ]

    header: Controls.Control {
        padding: Kirigami.Units.smallSpacing

        contentItem: Controls.TextField {
            id: searchField
            placeholderText: i18n("Filter by title or author…")
            text: library.searchQuery
            onTextChanged: library.searchQuery = text
        }
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
            required property int index
            required property var model

            width: grid.cellWidth
            height: grid.cellHeight

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
                  ? i18n("Add a Dropbox Calibre library first.")
                  : (library.searchQuery.length
                     ? i18n("Nothing matches the current filter.")
                     : i18n("Import or download a metadata.db for this library first."))
            helpfulAction: Kirigami.Action {
                text: i18n("Open developer tools")
                onTriggered: pageStack.push(Qt.resolvedUrl("DevPage.qml"))
            }
        }
    }
}
