cmake_minimum_required(VERSION 3.21)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
qt_standard_project_setup()

set(TRAY_SOURCES
    src/main.cpp
    src/trayapp.cpp   src/trayapp.h
    # Reuse JsonManager from MainApp
    ${CMAKE_SOURCE_DIR}/MainApp/src/jsonmanager.cpp
    ${CMAKE_SOURCE_DIR}/MainApp/src/jsonmanager.h
)

qt_add_executable(MediaCountdownsNotifier WIN32
    ${TRAY_SOURCES}
)

target_include_directories(MediaCountdownsNotifier PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/MainApp/src
    ${CMAKE_SOURCE_DIR}/shared
)

target_link_libraries(MediaCountdownsNotifier PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

# Windows Shell/COM APIs needed for startup shortcut creation (IShellLink)
if(WIN32)
    target_link_libraries(MediaCountdownsNotifier PRIVATE Ole32 Shell32)
endif()

install(TARGETS MediaCountdownsNotifier RUNTIME DESTINATION bin)
