cmake_minimum_required(VERSION 3.21)

# Without it the binaries require the macOS version they were built on.
# 13 is the oldest macOS that Qt 6.10 supports.
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")

project(Kolorit
    VERSION 1.1.0
    DESCRIPTION "Repaint images into any palette, plus retro effects"
    HOMEPAGE_URL "https://github.com/Kalikoff/Kolorit"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# All executables go to bin/ inside the build folder
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${cfg} CFG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/bin)
endforeach()

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Widgets Concurrent Svg Network LinguistTools)
qt_standard_project_setup()
# qt_standard_project_setup only enables AUTOMOC/AUTOUIC; .qrc resources go through AUTORCC
set(CMAKE_AUTORCC ON)

if(MSVC)
    # UTF-8 sources, strict warnings, parallel compilation
    add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus /MP)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    # Aggregates with default member initializers are often filled only partially on purpose
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()

# ---------------------------------------------------------------------------
# Processing core: a separate library without QtWidgets, shared by the
# application, the command-line tool and the tests.
# ---------------------------------------------------------------------------
add_library(kolorit_core STATIC
    src/core/ColorSpace.h
    src/core/ColorSpace.cpp
    src/core/Palette.h
    src/core/Palette.cpp
    src/core/BuiltinPalettes.h
    src/core/BuiltinPalettes.cpp
    src/core/ParallelFor.h
    src/core/PixelOps.h
    src/core/Random.h
    src/core/ImageTask.h
    src/core/ImageTaskRunner.h
    src/core/ImageTaskRunner.cpp
    src/processing/PaletteMapper.h
    src/processing/PaletteMapper.cpp
    src/processing/PaletteBuilder.h
    src/processing/PaletteBuilder.cpp
    src/processing/Effects.h
    src/processing/Effects.cpp
    src/processing/VhsEffect.h
    src/processing/VhsEffect.cpp
    src/processing/ConsoleEffect.h
    src/processing/ConsoleEffect.cpp
    src/processing/CrtEffect.h
    src/processing/CrtEffect.cpp
    src/app/BatchRunner.h
    src/app/BatchRunner.cpp
    src/app/Language.h
    src/app/Language.cpp
)
target_include_directories(kolorit_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(kolorit_core PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent)
target_compile_definitions(kolorit_core PUBLIC KOLORIT_VERSION="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# Interface: also a library, so the offscreen test can drive the main window
# and the dialogs
# ---------------------------------------------------------------------------
add_library(kolorit_ui STATIC
    src/app/AppPaths.h
    src/app/AppPaths.cpp
    src/app/Updates.h
    src/app/Updates.cpp
    src/ui/Theme.h
    src/ui/Theme.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/RepaintPanel.h
    src/ui/RepaintPanel.cpp
    src/ui/ImageView.h
    src/ui/ImageView.cpp
    src/ui/PalettePanel.h
    src/ui/PalettePanel.cpp
    src/ui/HistoryPanel.h
    src/ui/HistoryPanel.cpp
    src/ui/PaletteStrip.h
    src/ui/PaletteStrip.cpp
    src/ui/PreviewDialog.h
    src/ui/PreviewDialog.cpp
    src/ui/EffectParamsDialog.h
    src/ui/EffectParamsDialog.cpp
    src/ui/ChunkPaletteDialog.h
    src/ui/ChunkPaletteDialog.cpp
    src/ui/PaletteGeneratorDialog.h
    src/ui/PaletteGeneratorDialog.cpp
    src/ui/KMeansPaletteDialog.h
    src/ui/KMeansPaletteDialog.cpp
    src/ui/VhsDialog.h
    src/ui/VhsDialog.cpp
)
target_link_libraries(kolorit_ui PUBLIC kolorit_core Qt6::Widgets Qt6::Svg Qt6::Network)

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
qt_add_executable(Kolorit WIN32 MACOSX_BUNDLE
    src/main.cpp
    resources/kolorit.qrc
)
target_link_libraries(Kolorit PRIVATE kolorit_ui)

if(APPLE)
    # Bundle metadata for Info.plist and the Finder/Dock icon
    set(KOLORIT_ICNS resources/macos/Kolorit.icns)
    target_sources(Kolorit PRIVATE ${KOLORIT_ICNS})
    set_source_files_properties(${KOLORIT_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(Kolorit PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Kolorit"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.kalikoff.kolorit"
        MACOSX_BUNDLE_ICON_FILE "Kolorit.icns"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Copyright (c) 2026 Kalikoff")
endif()

# Batch mode is a separate console executable. Kolorit.exe is a GUI (WIN32)
# build: it has no console, its output goes nowhere and the shell does not wait
# for it. A console build behaves like an ordinary command-line tool.
qt_add_executable(kolorit_cli
    src/app/BatchMain.cpp
)
set_target_properties(kolorit_cli PROPERTIES OUTPUT_NAME "kolorit-cli")
target_link_libraries(kolorit_cli PRIVATE kolorit_core Qt6::Gui)

# Working folders next to the executable:
# Images/ for source pictures, Palette/ for user palettes, Results/ for output.
# On macOS they live in ~/Documents/Kolorit instead (see AppPaths): folders inside
# the .app bundle would break its signature.
if(NOT APPLE)
    add_custom_command(TARGET Kolorit POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Kolorit>/Images"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Kolorit>/Palette"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Kolorit>/Results"
        COMMENT "Creating the working folders next to Kolorit")
endif()

# ---------------------------------------------------------------------------
# Translations. English is the source language; every other language is a
# Qt .ts file compiled into the core library under :/i18n.
# The update_translations target refreshes the .ts files from the sources.
# ---------------------------------------------------------------------------
set(KOLORIT_TS_FILES translations/kolorit_ru.ts)
qt_add_translations(TARGETS kolorit_core
    SOURCE_TARGETS kolorit_core kolorit_ui Kolorit
    TS_FILES ${KOLORIT_TS_FILES}
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -no-obsolete -locations relative)

# ---------------------------------------------------------------------------
# Installation and deployment: cmake --install <build> --prefix <dist>
# The deploy script pulls in the Qt libraries, plugins and Qt's own translations.
# On macOS the result is a self-contained Kolorit.app with kolorit-cli inside it.
# ---------------------------------------------------------------------------
if(APPLE)
    set(KOLORIT_BUNDLE_CONTENTS Kolorit.app/Contents)
    # kolorit-cli shares the Qt frameworks and plugins that macdeployqt puts in the bundle
    set_target_properties(kolorit_cli PROPERTIES INSTALL_RPATH "@executable_path/../Frameworks")
    install(TARGETS Kolorit BUNDLE DESTINATION .)
    install(TARGETS kolorit_cli RUNTIME DESTINATION ${KOLORIT_BUNDLE_CONTENTS}/MacOS)
    # ...but runs on the offscreen platform, which macdeployqt leaves out
    install(FILES "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>"
        DESTINATION ${KOLORIT_BUNDLE_CONTENTS}/PlugIns/platforms)
    install(FILES LICENSE resources/fonts/VT323-OFL.txt DESTINATION ${KOLORIT_BUNDLE_CONTENTS}/Resources)
    # macdeployqt does not copy Qt's own translations (standard buttons, file dialog)
    foreach(ts ${KOLORIT_TS_FILES})
        string(REGEX REPLACE "^.*kolorit_(.+)\\.ts$" "\\1" lang "${ts}")
        install(FILES "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}/qtbase_${lang}.qm"
            DESTINATION ${KOLORIT_BUNDLE_CONTENTS}/Resources/translations)
    endforeach()
else()
    install(TARGETS Kolorit kolorit_cli RUNTIME DESTINATION bin)
    install(FILES LICENSE resources/fonts/VT323-OFL.txt DESTINATION share/doc/kolorit)
endif()
qt_generate_deploy_app_script(
    TARGET Kolorit
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${deploy_script})
if(APPLE)
    # macdeployqt rewrites library paths, which invalidates the linker's signatures, and
    # Apple Silicon refuses to run unsigned code: sign the whole bundle ad hoc
    install(CODE [[
        execute_process(
            COMMAND codesign --force --deep --sign - "${CMAKE_INSTALL_PREFIX}/Kolorit.app"
            COMMAND_ERROR_IS_FATAL ANY)
    ]])
endif()

# ---------------------------------------------------------------------------
# Tools: renders the README images from docs/images/photos
#   cmake --build <build> --target readme_images
# ---------------------------------------------------------------------------
option(KOLORIT_BUILD_TOOLS "Build the documentation tools" OFF)
if(KOLORIT_BUILD_TOOLS)
    qt_add_executable(kolorit_readme_images tools/readme_images.cpp resources/kolorit.qrc)
    target_link_libraries(kolorit_readme_images PRIVATE kolorit_ui)
    add_custom_target(readme_images
        COMMAND kolorit_readme_images "${CMAKE_CURRENT_SOURCE_DIR}/docs/images/photos"
                                      "${CMAKE_CURRENT_SOURCE_DIR}/docs/images"
        DEPENDS kolorit_readme_images
        COMMENT "Rendering the README images"
        VERBATIM)
endif()

# ---------------------------------------------------------------------------
# Tests: the processing pipeline and the interface, both offscreen
# ---------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
