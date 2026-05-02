cmake_minimum_required(VERSION 3.21)
project(MediaCountdowns VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
qt_standard_project_setup()

set(MAIN_SOURCES
    src/main.cpp
    src/mainwindow.cpp        src/mainwindow.h
    src/tilewidget.cpp        src/tilewidget.h
    src/addtiledialog.cpp     src/addtiledialog.h
    src/outlinedlabel.h
    src/countdownwidget.h
    src/applogger.h
    src/customtiledialog.cpp  src/customtiledialog.h
    src/edittiledialog.cpp    src/edittiledialog.h
    src/tmdbscraper.cpp       src/tmdbscraper.h
    src/jsonmanager.cpp       src/jsonmanager.h
    resources/resources.qrc
)

qt_add_executable(MediaCountdowns WIN32
    ${MAIN_SOURCES}
    mainwindow.ui
)

target_include_directories(MediaCountdowns PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/shared
)

target_link_libraries(MediaCountdowns PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

install(TARGETS MediaCountdowns RUNTIME DESTINATION bin)
