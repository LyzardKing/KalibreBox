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

    globalDrawer: Kirigami.GlobalDrawer {
        title: "KalibreBox"
        titleIcon: "application-x-storybook"

        actions: [
            Kirigami.Action {
                text: KI18n.i18n("Add library")
                icon.name: "list-add"
                onTriggered: addLibraryDialog.open()
            },
            Kirigami.Action {
                text: KI18n.i18n("Filter by author")
                icon.name: "user"
                enabled: LibraryController.hasLibrary
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
        id: settingsPage

        Kirigami.ScrollablePage {
            title: KI18n.i18n("Settings")

            Kirigami.FormLayout {
                RowLayout {
                    // Select download folder for books (default is the Downloads folder)
                    Kirigami.FormData.label: KI18n.i18n("Download folder")
                    Controls.TextField {
                        Layout.fillWidth: true
                        readOnly: true
                        text: LibraryController.downloadFolder
                    }
                    Controls.Button {
                        icon.name: "folder"
                        text: KI18n.i18n("Select folder")
                        onClicked: folderDialog.open()
                    }
                }
                Controls.ComboBox {
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
        title: KI18n.i18n("Add Dropbox library")
        padding: Kirigami.Units.largeSpacing
        preferredWidth: Kirigami.Units.gridUnit * 24

        contentItem: ColumnLayout {
            Controls.Label {
                text: KI18n.i18n("Dropbox folder or shared link for a Calibre library.")
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
                Layout.fillWidth: true
                placeholderText: KI18n.i18n("e.g. /CalibreLibrary")
            }
        }

        customFooterActions: [
            Kirigami.Action {
                text: KI18n.i18n("Add library")
                enabled: !LibraryController.syncing && libraryPathField.text.length > 0
                onTriggered: {
                    LibraryController.addLibrary(libraryPathField.text, libraryNameField.text);
                    libraryNameField.clear();
                    libraryPathField.clear();
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
        currentFolder: "file://" + LibraryController.downloadFolder
        onAccepted: LibraryController.downloadFolder = selectedFolder.toString().replace("file://", "")
    }
}
