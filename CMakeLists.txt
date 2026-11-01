cmake_minimum_required(VERSION 3.25)
# バージョン番号はここだけで管理する(exe のバージョン情報と配布 zip の名前に使われる)
project(ImeIndicator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(UNICODE _UNICODE NOMINMAX)
add_compile_options(/utf-8 /W4 /permissive-)

add_subdirectory(probe)

add_executable(ime-indicator WIN32
    src/main.cpp
    src/ImeMonitor.cpp
    src/CaretLocator.cpp
    src/IndicatorWindow.cpp
    src/TrayIcon.cpp
    src/UiaWorker.cpp
    src/Theme.cpp
    src/Renderer.cpp
    src/Settings.cpp
    resources/app.manifest
    resources/app.rc
)
target_link_libraries(ime-indicator PRIVATE imm32 oleacc ole32 oleaut32 shcore user32 gdi32 shell32 advapi32 d2d1 dwrite)
configure_file(resources/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h @ONLY)
target_include_directories(ime-indicator PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
# 使わないコードを取り除く(exe の大半は静的リンクした C ランタイム)
target_compile_options(ime-indicator PRIVATE $<$<CONFIG:Release>:/Gy /GL>)
target_link_options(ime-indicator PRIVATE $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>)

# 見た目の確認用: ライト/ダーク × あ/a × DPI を 1 枚の PNG に並べる
add_executable(badge-preview tools/badge-preview.cpp src/Renderer.cpp src/Theme.cpp)
target_link_libraries(badge-preview PRIVATE ole32 windowscodecs gdi32 msimg32 user32 advapi32 shell32 d2d1 dwrite)
