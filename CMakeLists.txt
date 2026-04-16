cmake_minimum_required(VERSION 3.21)
project(AkeraGlitchStudio VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets)
find_package(PkgConfig QUIET)

set(APP_SOURCES
    src/app/main.cpp
    src/app/MainWindow.h
    src/app/MainWindow.cpp
    src/app/AppState.h
    src/app/AppState.cpp
    src/ui/PreviewGLWidget.h
    src/ui/PreviewGLWidget.cpp
    src/ui/ControlPanel.h
    src/ui/ControlPanel.cpp
    src/ui/TransportBar.h
    src/ui/TransportBar.cpp
    src/media/MediaEngine.h
    src/media/MediaEngine.cpp
    src/effects/PreviewEffects.h
    src/effects/PreviewEffects.cpp
    src/export/ExportWorker.h
    src/export/ExportWorker.cpp
    src/preset/PresetManager.h
    src/preset/PresetManager.cpp
)

add_executable(AkeraGlitchStudio WIN32 ${APP_SOURCES})

target_include_directories(AkeraGlitchStudio PRIVATE src)
target_link_libraries(AkeraGlitchStudio PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets)

if (PkgConfig_FOUND)
    pkg_check_modules(GSTREAMER QUIET IMPORTED_TARGET
        gstreamer-1.0>=1.20
        gstreamer-app-1.0
        gstreamer-video-1.0)
endif()

if (TARGET PkgConfig::GSTREAMER)
    message(STATUS "GStreamer found: enabling appsink playback backend")
    target_compile_definitions(AkeraGlitchStudio PRIVATE AKERA_HAS_GSTREAMER=1)
    target_link_libraries(AkeraGlitchStudio PRIVATE PkgConfig::GSTREAMER)
else()
    message(STATUS "GStreamer not found. Building with stub playback backend.")
endif()

if (MSVC)
    target_compile_options(AkeraGlitchStudio PRIVATE /W4 /permissive-)
else()
    target_compile_options(AkeraGlitchStudio PRIVATE -Wall -Wextra -Wpedantic)
endif()
