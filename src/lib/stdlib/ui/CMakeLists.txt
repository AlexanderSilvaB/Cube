include_directories(../..)

set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
add_definitions(-DWIN_EXPORT)

message("Looking for Qt...")
# Qt modules (https://doc.qt.io/qt-5/qtmodules.html) you're using in your application
find_package(Qt5 COMPONENTS Widgets Core UiTools QuickWidgets Quick)
if (${Qt5_FOUND})
    message("Found Qt " ${Qt5_VERSION})

    set(SRC ui.cpp WM.cpp Shapes.cpp)

    add_library(ui SHARED ${SRC})
    target_link_libraries(ui Qt5::Widgets Qt5::Core Qt5::UiTools Qt5::QuickWidgets Qt5::Quick)
    set_target_properties(ui PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/stdlib/libs/)
    set_target_properties(ui PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/stdlib/libs/)

else()
    message("Couldn't find Qt. Not building UI library")
endif()