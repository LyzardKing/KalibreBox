pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.ki18n
import QtQuick.Dialogs

Kirigami.ApplicationWindow {
    id: root

    width: 800
    height: 600
    minimumWidth: 400
    minimumHeight: 400
    title: KI18n.i18n("KalibreBox")

    // Picking a library in the drawer ends in librariesChanged; the delegates
    // live in a different scope than `root`, so the drawer is closed from here.
    Connections {
        target: LibraryController
        function onLibrariesChanged() {
            root.globalDrawer.close();
        }
    }

    globalDrawer: Kirigami.GlobalDrawer {
        title: "KalibreBox"
        titleIcon: "application-x-storybook"

        topContent: [
            Kirigami.ListSectionHeader {
                Layout.fillWidth: true
                text: KI18n.i18n("Libraries")
            },
            Controls.Label {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                Layout.bottomMargin: Kirigami.Units.smallSpacing
                visible: LibraryController.libraries.length === 0
                text: KI18n.i18n("No libraries yet")
                opacity: 0.7
                wrapMode: Text.WordWrap
            },
            Repeater {
                model: LibraryController.libraries

                delegate: Controls.ItemDelegate {
                    id: libraryDelegate
                    required property var modelData

                    Layout.fillWidth: true
                    text: libraryDelegate.modelData.name
                    icon.name: libraryDelegate.modelData.isOpds ? "network-server" : "folder-cloud"
                    highlighted: libraryDelegate.modelData.current
                    onClicked: LibraryController.selectLibrary(libraryDelegate.modelData.id)
                }
            },
            Kirigami.Separator {
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.smallSpacing
            }
        ]

        actions: [
            Kirigami.Action {
                text: KI18n.i18n("Add library")
                icon.name: "list-add"
                onTriggered: addLibraryDialog.open()
            },
            Kirigami.Action {
                text: KI18n.i18n("Manage libraries")
                icon.name: "configure"
                onTriggered: {
                    root.globalDrawer.close();
                    root.pageStack.layers.push(manageLibrariesPage);
                }
            },
            Kirigami.Action {
                text: KI18n.i18n("Filter by author")
                icon.name: "user"
                enabled: LibraryController.hasLibrary && !LibraryController.isOpds
                onTriggered: {
                    authorDialog.authors = LibraryController.authors();
                    authorDialog.text = "";
                    authorDialog.open();
                }
            },
            Kirigami.Action {
                text: LibraryController.signedIn ? KI18n.i18n("Sign out of Dropbox") : KI18n.i18n("Sign in to Dropbox")
                icon.name: LibraryController.signedIn ? "network-disconnect" : "network-connect"
                onTriggered: {
                    if (LibraryController.signedIn)
                        LibraryController.signOut();
                    else
                        LibraryController.signIn();
                }
            },
            Kirigami.Action {
                text: KI18n.i18n("Sync library")
                icon.name: "view-refresh"
                enabled: LibraryController.hasLibrary && !LibraryController.syncing
                onTriggered: LibraryController.syncLibrary()
            },
            Kirigami.Action {
                text: KI18n.i18n("Settings")
                icon.name: "preferences-system"
                onTriggered: {
                    root.globalDrawer.close();
                    root.pageStack.layers.push(settingsPage);
                }
            }
        ]

        // The footer stays pinned to the bottom of the drawer.
        footer: ColumnLayout {
            spacing: 0

            Kirigami.Separator {
                Layout.fillWidth: true
            }
            Controls.ItemDelegate {
                text: KI18n.i18n("About")
                icon.name: "about"
                Layout.fillWidth: true
                onClicked: {
                    root.globalDrawer.close();
                    root.pageStack.layers.push(aboutPage);
                }
            }
        }
    }

    Component {
        id: aboutPage

        Kirigami.AboutPage {
            aboutData: ({
                    displayName: "KalibreBox",
                    componentName: "kalibrebox",
                    shortDescription: KI18n.i18n("Calibre e-book library manager for Dropbox"),
                    homepage: "https://github.com/LyzardKing/KalibreBox",
                    bugReport: "https://github.com/LyzardKing/KalibreBox/issues",
                    version: Qt.application.version,
                    copyrightStatement: "© 2026 Galileo Sartor",
                    authors: [
                        {
                            name: "Galileo Sartor",
                            task: "",
                            emailAddress: "",
                            webAddress: "",
                            ocsUsername: ""
                        }
                    ],
                    credits: [],
                    translators: [],
                    licenses: [
                        {
                            name: "MIT",
                            text: "© 2026 Galileo Sartor. Released under the MIT license; see the LICENSE file for the full text.",
                            spdx: "MIT"
                        }
                    ]
                })
        }
    }

    Component {
        id: manageLibrariesPage

        Kirigami.ScrollablePage {
            id: managePage
            title: KI18n.i18n("Manage libraries")

            // The library being renamed or deleted by the open dialog.
            property string targetId: ""
            property string targetName: ""

            actions: [
                Kirigami.Action {
                    text: KI18n.i18n("Add library")
                    icon.name: "list-add"
                    onTriggered: addLibraryDialog.open()
                }
            ]

            ListView {
                id: libraryList
                model: LibraryController.libraries

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.gridUnit * 4
                    visible: libraryList.count === 0
                    text: KI18n.i18n("No libraries yet")
                    helpfulAction: Kirigami.Action {
                        text: KI18n.i18n("Add library")
                        icon.name: "list-add"
                        onTriggered: addLibraryDialog.open()
                    }
                }

                delegate: Kirigami.SwipeListItem {
                    id: libraryItem
                    required property var modelData

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.largeSpacing

                        Kirigami.Icon {
                            source: libraryItem.modelData.isOpds ? "network-server" : "folder-cloud"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                            Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Controls.Label {
                                Layout.fillWidth: true
                                text: libraryItem.modelData.name
                                font.bold: libraryItem.modelData.current
                                elide: Text.ElideRight
                            }
                            Controls.Label {
                                Layout.fillWidth: true
                                text: libraryItem.modelData.location
                                opacity: 0.7
                                font: Kirigami.Theme.smallFont
                                elide: Text.ElideMiddle
                            }
                        }
                    }

                    actions: [
                        Kirigami.Action {
                            text: KI18n.i18n("Rename")
                            icon.name: "edit-rename"
                            onTriggered: {
                                managePage.targetId = libraryItem.modelData.id;
                                managePage.targetName = libraryItem.modelData.name;
                                renameField.text = libraryItem.modelData.name;
                                renameDialog.open();
                                renameField.forceActiveFocus();
                                renameField.selectAll();
                            }
                        },
                        Kirigami.Action {
                            text: KI18n.i18n("Delete")
                            icon.name: "edit-delete"
                            onTriggered: {
                                managePage.targetId = libraryItem.modelData.id;
                                managePage.targetName = libraryItem.modelData.name;
                                deleteDialog.open();
                            }
                        }
                    ]
                }
            }

            Kirigami.Dialog {
                id: renameDialog
                title: KI18n.i18n("Rename library")
                padding: Kirigami.Units.largeSpacing
                preferredWidth: Kirigami.Units.gridUnit * 20

                function apply() {
                    if (renameField.text.trim().length === 0)
                        return;
                    LibraryController.renameLibrary(managePage.targetId, renameField.text);
                    renameDialog.close();
                }

                contentItem: Controls.TextField {
                    id: renameField
                    placeholderText: KI18n.i18n("Library name")
                    onAccepted: renameDialog.apply()
                }

                customFooterActions: [
                    Kirigami.Action {
                        text: KI18n.i18n("Rename")
                        icon.name: "edit-rename"
                        enabled: renameField.text.trim().length > 0
                        onTriggered: renameDialog.apply()
                    }
                ]
            }

            Kirigami.PromptDialog {
                id: deleteDialog
                title: KI18n.i18n("Delete library")
                subtitle: KI18n.i18n("Remove \"%1\" from KalibreBox? Its downloaded catalogue is deleted from this device; your files on the server are not touched.", managePage.targetName)
                standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
                onAccepted: LibraryController.removeLibrary(managePage.targetId)
            }
        }
    }

    Component {
        id: settingsPage

        Kirigami.ScrollablePage {
            title: KI18n.i18n("Settings")

            Kirigami.FormLayout {
                RowLayout {
                    // Select download folder for books (default is the Downloads folder)
                    Kirigami.FormData.label: KI18n.i18n("Download folder")
                    Controls.CheckBox {
                        id: publicDownloadsCheck
                        visible: Qt.platform.os === "android"
                        text: KI18n.i18n("Downloads")
                        checked: LibraryController.usePublicDownloads
                        onClicked: {
                            if (checked) {
                                LibraryController.usePublicDownloads = true;
                            } else {
                                checked = true;
                                folderDialog.open();
                            }
                        }
                    }
                    Controls.Button {
                        visible: Qt.platform.os !== "android" || !LibraryController.usePublicDownloads
                        icon.name: "folder"
                        text: LibraryController.downloadFolderPath
                        onClicked: folderDialog.open()
                    }
                }
                Connections {
                    target: LibraryController
                    function onUsePublicDownloadsChanged() {
                        publicDownloadsCheck.checked = LibraryController.usePublicDownloads;
                    }
                }
                Controls.SpinBox {
                    Kirigami.FormData.label: KI18n.i18n("Catalogue batch size")
                    from: 1
                    to: 500
                    editable: true
                    value: LibraryController.opdsBatchSize
                    onValueModified: LibraryController.opdsBatchSize = value
                    Controls.ToolTip.visible: hovered
                    Controls.ToolTip.text: KI18n.i18n("Books loaded at a time from OPDS catalogues; more load as you scroll.")
                }
                Controls.ComboBox {
                    visible: Qt.platform.os == "android"
                    Kirigami.FormData.label: KI18n.i18n("Theme")
                    model: [KI18n.i18n("System"), KI18n.i18n("Light"), KI18n.i18n("Dark")]
                    currentIndex: LibraryController.theme
                    onActivated: index => LibraryController.theme = index
                }
            }
        }
    }

    Kirigami.SearchDialog {
        id: authorDialog

        property var authors: []

        title: KI18n.i18n("Filter by author")
        searchFieldPlaceholderText: KI18n.i18n("Search authors…")
        emptyText: KI18n.i18n("No authors found")

        // Case-insensitive substring match on what is typed in the dialog.
        model: authors.filter(name => name.toLowerCase().includes(text.trim().toLowerCase()))
        delegate: Controls.ItemDelegate {
            id: authorDelegate
            required property string modelData
            width: ListView.view.width
            text: authorDelegate.modelData
            onClicked: {
                LibraryController.searchQuery = authorDelegate.modelData;
                authorDialog.close();
            }
        }
    }

    Kirigami.Dialog {
        id: addLibraryDialog
        title: KI18n.i18n("Add library")
        padding: Kirigami.Units.largeSpacing
        preferredWidth: Kirigami.Units.gridUnit * 24

        // New source types only need an entry here and a block of fields below.
        readonly property var sourceTypes: [
            {
                text: KI18n.i18n("Dropbox"),
                value: "dropbox"
            },
            {
                text: KI18n.i18n("OPDS catalogue"),
                value: "opds"
            }
        ]
        readonly property string sourceType: typeBox.currentValue
        readonly property bool isOpds: sourceType === "opds"
        readonly property bool canAdd: !LibraryController.syncing && (isOpds ? opdsUrlField.text.trim().length > 0 : libraryPathField.text.length > 0)

        function clearFields() {
            libraryNameField.clear();
            libraryPathField.clear();
            opdsUrlField.clear();
            opdsUserField.clear();
            opdsPasswordField.clear();
        }

        contentItem: ColumnLayout {
            Controls.ComboBox {
                id: typeBox
                Layout.fillWidth: true
                model: addLibraryDialog.sourceTypes
                textRole: "text"
                valueRole: "value"
            }
            Controls.Label {
                text: addLibraryDialog.isOpds ? KI18n.i18n("URL of an OPDS catalogue, e.g. a Calibre content server (http://host:8080/opds) or https://www.gutenberg.org/ebooks.opds/.") : KI18n.i18n("Dropbox folder or shared link for a Calibre library.")
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                wrapMode: Text.WordWrap
                font.italic: true
            }
            Controls.TextField {
                id: libraryNameField
                Layout.fillWidth: true
                placeholderText: KI18n.i18n("Display name (optional)")
            }
            Controls.TextField {
                id: libraryPathField
                visible: !addLibraryDialog.isOpds
                Layout.fillWidth: true
                placeholderText: KI18n.i18n("e.g. /CalibreLibrary")
            }
            Controls.TextField {
                id: opdsUrlField
                visible: addLibraryDialog.isOpds
                Layout.fillWidth: true
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoPredictiveText
                placeholderText: KI18n.i18n("https://example.org/opds")
            }
            Controls.TextField {
                id: opdsUserField
                visible: addLibraryDialog.isOpds
                Layout.fillWidth: true
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                placeholderText: KI18n.i18n("Username (optional)")
            }
            Controls.TextField {
                id: opdsPasswordField
                visible: addLibraryDialog.isOpds
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: KI18n.i18n("Password (optional)")
            }
        }

        customFooterActions: [
            Kirigami.Action {
                text: KI18n.i18n("Add library")
                enabled: addLibraryDialog.canAdd
                onTriggered: {
                    if (addLibraryDialog.isOpds)
                        LibraryController.addOpdsLibrary(opdsUrlField.text, libraryNameField.text, opdsUserField.text, opdsPasswordField.text);
                    else
                        LibraryController.addLibrary(libraryPathField.text, libraryNameField.text);
                    addLibraryDialog.clearFields();
                    addLibraryDialog.close();
                }
            }
        ]
    }

    pageStack.initialPage: LibraryPage {
        onAddLibraryRequested: addLibraryDialog.open()
    }

    FolderDialog {
        id: folderDialog
        currentFolder: Qt.platform.os === "android" ? "" : LibraryController.downloadFolder
        onAccepted: LibraryController.downloadFolder = selectedFolder
    }
}
