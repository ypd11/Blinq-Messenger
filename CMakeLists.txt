cmake_minimum_required(VERSION 3.16)

project(BlinqMessenger VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Multimedia Network Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Multimedia Network Widgets)

if(MINGW)
    set(CMAKE_RC_COMPILE_OBJECT "<CMAKE_RC_COMPILER> -O coff <DEFINES> <FLAGS> <SOURCE> <OBJECT>")
endif()

set(PROJECT_SOURCES
        main.cpp
        chatwindow.cpp
        chatwindow.h
        internetrelayservice.cpp
        internetrelayservice.h
        lanchatservice.cpp
        lanchatservice.h
        mainwindow.cpp
        mainwindow.h
        publicchatwindow.cpp
        publicchatwindow.h
        settingsdialog.cpp
        settingsdialog.h
        windowsmediawatcher.cpp
        windowsmediawatcher.h
        app.rc
        resources.qrc
        mainwindow.ui
)

configure_file(help.html help.html COPYONLY)
configure_file(assets/appicon.ico assets/appicon.ico COPYONLY)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(BlinqMessenger
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET BlinqMessenger APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(BlinqMessenger SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(BlinqMessenger
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(BlinqMessenger PRIVATE Qt${QT_VERSION_MAJOR}::Multimedia Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Widgets)
target_compile_definitions(BlinqMessenger PRIVATE BLINQ_APP_VERSION="${PROJECT_VERSION}")
if(WIN32)
    file(GLOB WINSDK_CPPWINRT_DIRS "C:/Program Files (x86)/Windows Kits/10/Include/*/cppwinrt")
    if(WINSDK_CPPWINRT_DIRS)
        list(SORT WINSDK_CPPWINRT_DIRS)
        list(GET WINSDK_CPPWINRT_DIRS -1 WINSDK_CPPWINRT_DIR)
        target_include_directories(BlinqMessenger PRIVATE "${WINSDK_CPPWINRT_DIR}")
    endif()
    target_link_libraries(BlinqMessenger PRIVATE winmm bcrypt windowsapp shell32 user32)
endif()


# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.exeinnovate.BlinqMessenger)
endif()
set_target_properties(BlinqMessenger PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME "messenger"
)

include(GNUInstallDirs)
install(TARGETS BlinqMessenger
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(BlinqMessenger)
endif()
