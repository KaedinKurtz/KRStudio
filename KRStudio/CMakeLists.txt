cmake_minimum_required(VERSION 3.16)

project(KRStudio VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
add_subdirectory(ads)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(KRStudio
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        propertiespanel.h
        propertiespanel.ui
        propertiespanel.cpp
        static_toolbar.ui
        resources.qrc
        dark_style.qss
    )
else()
    if(ANDROID)
        add_library(KRStudio SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(KRStudio
            ${PROJECT_SOURCES}
        )
    endif()
endif()

# --- MODIFIED/CORRECTED SECTION START ---


# This is the CONSOLIDATED linking command for better practice.
# I've combined your two separate calls into one.
target_include_directories(KRStudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ads/src
)

# 2. Tell CMake the EXACT FOLDER that contains the .lib file
#    This path is now corrected based on the file path you provided.
target_link_directories(KRStudio PRIVATE
    "D:/RoboticsSoftware/KRStudio/ads/build/Desktop_Qt_6_9_0_MSVC2022_64bit-Debug/x64/lib"
)

# 3. Tell CMake to link the libraries by their target names.
#    We are using the specific target name that corresponds to the debug library.
target_link_libraries(KRStudio PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    qtadvanceddocking-qt6
)

# --- MODIFIED/CORRECTED SECTION END ---

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.KRStudio)
endif()
set_target_properties(KRStudio PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS KRStudio
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(KRStudio)
endif()
