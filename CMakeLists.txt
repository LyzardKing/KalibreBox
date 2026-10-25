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
    src/cpp/core/model/Library.cpp
    src/cpp/core/text/TextNormalization.cpp
)

qt_add_library(kalibreboxcore STATIC ${KALIBREBOX_CORE_SOURCES})
target_include_directories(kalibreboxcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/cpp)
target_link_libraries(kalibreboxcore PUBLIC Qt6::Core Qt6::Sql)

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
