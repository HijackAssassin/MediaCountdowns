cmake_minimum_required(VERSION 3.21)

# Enable Qt's resource compiler (rcc) to auto-process .qrc files.
# rcc does NOT use windres, so no path-with-spaces issues.
set(CMAKE_AUTORCC ON)

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
    ${CMAKE_SOURCE_DIR}/shared
)

target_link_libraries(MediaCountdowns PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

install(TARGETS MediaCountdowns RUNTIME DESTINATION bin)
