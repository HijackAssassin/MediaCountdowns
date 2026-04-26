cmake_minimum_required(VERSION 3.21)
project(MediaCountdowns VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt6 is mandatory — do NOT fall back to Qt5
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
qt_standard_project_setup()

add_subdirectory(MainApp)
add_subdirectory(TrayApp)
