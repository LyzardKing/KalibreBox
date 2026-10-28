cmake_minimum_required(VERSION 3.20)

project(KalibreBox
    VERSION 0.1.0
    DESCRIPTION "Calibre e-book library manager for Dropbox, built with Kirigami"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Sql Network NetworkAuth)
find_package(KF6I18n REQUIRED)
qt_standard_project_setup(REQUIRES 6.8)

set(KALIBREBOX_CORE_SOURCES
    src/cpp/core/data/LibraryRepository.cpp
    src/cpp/core/db/DatabaseHelper.cpp
    src/cpp/core/dropbox/DropboxAuthProvider.cpp
    src/cpp/core/dropbox/DropboxClient.cpp
    src/cpp/core/dropbox/DropboxCredential.cpp
    src/cpp/core/dropbox/DropboxUrls.cpp
    src/cpp/core/model/Library.cpp
    src/cpp/core/text/TextNormalization.cpp
    src/cpp/ui/AndroidDownloads.cpp
    src/cpp/ui/AndroidFiles.cpp
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
target_link_libraries(kalibreboxcore PUBLIC Qt6::Core Qt6::Gui Qt6::Sql Qt6::Network Qt6::NetworkAuth)
if(DROPBOX_APP_KEY)
    target_compile_definitions(kalibreboxcore PRIVATE
        KALIBREBOX_DROPBOX_APP_KEY="${DROPBOX_APP_KEY}")
endif()
if(DROPBOX_OAUTH_PORT)
    target_compile_definitions(kalibreboxcore PRIVATE
        KALIBREBOX_DROPBOX_OAUTH_PORT="${DROPBOX_OAUTH_PORT}")
endif()

qt_extract_metatypes(kalibreboxcore)

qt_add_executable(kalibrebox
    src/cpp/main.cpp
)

target_compile_definitions(kalibrebox PRIVATE
    KALIBREBOX_VERSION="${PROJECT_VERSION}"
)

# The generated qmltyperegistrations.cpp includes the QML_FOREIGN header by bare name.
target_include_directories(kalibrebox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/cpp/ui)

qt_add_qml_module(kalibrebox
    URI KalibreBox
    VERSION 1.0
    QML_FILES
        src/qml/Main.qml
        src/qml/LibraryPage.qml
        src/qml/BookDetailsDialog.qml
    SOURCES
        src/cpp/ui/LibraryControllerQml.h
)

target_link_libraries(kalibrebox PRIVATE
    kalibreboxcore
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    KF6::I18n
    KF6::I18nQml
)

if(ANDROID)
    # KDE Frameworks install their QML modules to <prefix>/lib/qml, not Qt's
    # <prefix>/qml, so androiddeployqt only bundles Kirigami and the Breeze
    # style if that directory is an explicit QML import path (same as
    # ECMAddAndroidApk). Without it the app exits right after start-up.
    set(_kde_prefixes ${CMAKE_FIND_ROOT_PATH} ${CMAKE_PREFIX_PATH})
    list(REMOVE_DUPLICATES _kde_prefixes)
    foreach(_prefix IN LISTS _kde_prefixes)
        # qmlimportscanner chokes on symlinks.
        file(REAL_PATH "${_prefix}/lib/qml" _kde_qml_dir)
        if(IS_DIRECTORY "${_kde_qml_dir}")
            set_property(TARGET kalibrebox APPEND PROPERTY QT_QML_IMPORT_PATH "${_kde_qml_dir}")
        endif()
    endforeach()
    set_target_properties(kalibrebox PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
        QT_ANDROID_PACKAGE_NAME "io.github.kalibrebox"
        QT_ANDROID_VERSION_CODE 1
        QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
    )

    # Bundle the Breeze icons the app uses (the macro adds Kirigami's own
    # icons itself). Defined by KF6Kirigami's config, which only needs to be
    # found on Android. The macro installs to ${KDE_INSTALL_QMLDIR}, which is
    # empty (icons land in /org/kde/...) unless KDEInstallDirs is included,
    # as Okular's mobile app does via ECM.
    find_package(ECM REQUIRED NO_MODULE)
    list(APPEND CMAKE_MODULE_PATH ${ECM_KDE_MODULE_DIR})
    include(KDEInstallDirs6)
    find_package(KF6Kirigami REQUIRED)
    kirigami_package_breeze_icons(ICONS
        about
        application-x-storybook
        bookmarks-organize
        document-open
        document-save
        document-share
        list-add
        network-connect
        network-disconnect
        view-refresh
    )

    # Kirigami loads the icons from assets:/qml/org/kde/kirigami/breeze-internal,
    # which androiddeployqt's QML import scan never copies. As ECM does, hand
    # it the install dir as an extra plugin dir (copied verbatim to
    # assets/<dirname>). `cmake --install --prefix <build>/android-icons`
    # fills it before the APK is built; it must exist at configure time.
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/android-icons/lib/qml")
    set_property(TARGET kalibrebox APPEND PROPERTY
        QT_ANDROID_EXTRA_PLUGINS "${CMAKE_BINARY_DIR}/android-icons/lib/qml")
endif()

# KDE's clang-format style via ECM (generates .clang-format and the
# `clang-format` target). Optional so builds without ECM still work.
find_package(ECM NO_MODULE QUIET)
if(ECM_FOUND AND NOT ANDROID)
    list(APPEND CMAKE_MODULE_PATH ${ECM_KDE_MODULE_DIR})
    include(KDEClangFormat)
    file(GLOB_RECURSE KALIBREBOX_FORMAT_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h)
    kde_clang_format(${KALIBREBOX_FORMAT_FILES})
endif()

# QML formatting with Qt's own qmlformat (exported as Qt6::qmlformat), so it
# doesn't depend on PATH. The `qmlformat` target rewrites files in place.
if(TARGET Qt6::qmlformat AND NOT ANDROID)
    get_target_property(KALIBREBOX_QML_SOURCES kalibrebox QT_QML_MODULE_QML_FILES)
    add_custom_target(qmlformat
        COMMAND Qt6::qmlformat -i ${KALIBREBOX_QML_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Formatting QML sources with qmlformat")
endif()

if(NOT ANDROID)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS kalibrebox
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

include(FeatureSummary)
feature_summary(WHAT ALL)
