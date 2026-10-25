cmake_minimum_required(VERSION 3.20)

project(KalibreBox
    VERSION 0.1.0
    DESCRIPTION "Calibre e-book library manager for Dropbox, built with Kirigami"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Sql Network)
find_package(KF6I18n REQUIRED)
qt_standard_project_setup(REQUIRES 6.8)

enable_testing()

set(KALIBREBOX_CORE_SOURCES
    src/cpp/core/data/LibraryRepository.cpp
    src/cpp/core/db/DatabaseHelper.cpp
    src/cpp/core/dropbox/DropboxAuthProvider.cpp
    src/cpp/core/dropbox/DropboxClient.cpp
    src/cpp/core/dropbox/DropboxCredential.cpp
    src/cpp/core/dropbox/DropboxUrls.cpp
    src/cpp/core/dropbox/Pkce.cpp
    src/cpp/core/model/Library.cpp
    src/cpp/core/text/TextNormalization.cpp
    src/cpp/ui/BookListModel.cpp
    src/cpp/ui/LibraryController.cpp
)

# Dropbox app key (PKCE flow needs no secret). Read from local.properties, a
# gitignored file next to this CMakeLists.
set(DROPBOX_APP_KEY "")
set(DROPBOX_OAUTH_PORT "61196")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/local.properties")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/local.properties" _dropbox_props)
    foreach(_prop_line IN LISTS _dropbox_props)
        if(_prop_line MATCHES "^dropbox.key=(.*)$")
            set(DROPBOX_APP_KEY "${CMAKE_MATCH_1}")
        elseif(_prop_line MATCHES "^dropbox.oauthPort=(.*)$")
            set(DROPBOX_OAUTH_PORT "${CMAKE_MATCH_1}")
        endif()
    endforeach()
endif()
string(LENGTH "${DROPBOX_APP_KEY}" DROPBOX_APP_KEY_LENGTH)
message(STATUS "Dropbox app key: ${DROPBOX_APP_KEY_LENGTH} characters")
message(STATUS "Dropbox OAuth port: ${DROPBOX_OAUTH_PORT}")

qt_add_library(kalibreboxcore STATIC ${KALIBREBOX_CORE_SOURCES})
target_include_directories(kalibreboxcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/cpp)
target_link_libraries(kalibreboxcore PUBLIC Qt6::Core Qt6::Gui Qt6::Sql Qt6::Network)
if(DROPBOX_APP_KEY)
    target_compile_definitions(kalibreboxcore PRIVATE
        KALIBREBOX_DROPBOX_APP_KEY="${DROPBOX_APP_KEY}")
endif()
if(DROPBOX_OAUTH_PORT)
    target_compile_definitions(kalibreboxcore PRIVATE
        KALIBREBOX_DROPBOX_OAUTH_PORT="${DROPBOX_OAUTH_PORT}")
endif()

qt_add_executable(kalibrebox
    src/cpp/main.cpp
)

target_compile_definitions(kalibrebox PRIVATE
    KALIBREBOX_VERSION="${PROJECT_VERSION}"
)

qt_add_qml_module(kalibrebox
    URI KalibreBox
    VERSION 1.0
    QML_FILES
        src/qml/Main.qml
        src/qml/LibraryPage.qml
)

target_link_libraries(kalibrebox PRIVATE
    kalibreboxcore
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    KF6::I18n
)

add_subdirectory(tests)

install(TARGETS kalibrebox ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

include(FeatureSummary)
feature_summary(WHAT ALL)
