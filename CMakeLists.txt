cmake_minimum_required(VERSION 3.20)
project(RoboticsSoftware CXX)

# --- Global Project Settings ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)



# --- Find Required Packages ---
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
find_package(OpenGL REQUIRED)
find_package(glm REQUIRED)
find_package(urdfdom CONFIG REQUIRED)

# --- Define Paths to ADS Library ---
set(ADS_ROOT "D:/RoboticsSoftware/ads")
set(ADS_INCLUDE_DIR "${ADS_ROOT}/src")
# --- CORRECTED PATH to Qt 6.9.0 build ---
set(ADS_BUILD_DIR "${ADS_ROOT}/build/Desktop_Qt_6_9_0_MSVC2022_64bit-Debug") 
set(ADS_LIB_DIR "${ADS_BUILD_DIR}/x64/lib")
set(ADS_BIN_DIR "${ADS_BUILD_DIR}/x64/bin")


# --- Find ADS Library using find_library() ---
# This is a more robust way to find the library. It will fail during configuration if not found.

# Find the Debug version of the library
find_library(
    ADS_LIBRARY_DEBUG # Variable to store the full path to the library
    NAMES qtadvanceddocking-qt6d qtadvanceddockingd # Common names for the debug library
    HINTS "${ADS_LIB_DIR}" # Directory to search in
    REQUIRED # This will cause CMake to fail if the library isn't found
)

# Find the Release version (add this for when you build in Release mode)
find_library(
    ADS_LIBRARY_RELEASE
    NAMES qtadvanceddocking-qt6 qtadvanceddocking
    # You will likely need to point this to a Release build directory for ADS
    HINTS "D:/RoboticsSoftware/ads/build/Your-Release-Build-Folder/x64/lib"
)


# --- Define the Executable Target ---
set(SOURCE_FILES
    src/main.cpp
    src/Camera.cpp
    src/Shader.cpp
    src/Mesh.cpp
    src/Robot.cpp
    src/MainWindow.cpp
    src/ViewportWidget.cpp
    src/DiagnosticsPanel.cpp
    src/StaticToolbar.cpp
    src/Grid.cpp
    src/RenderingSystem.cpp
    src/Scene.cpp
    src/PropertiesPanel.cpp
    src/gridPropertiesWidget.cpp
    src/LinkPropertiesWidget.cpp
    src/SceneBuilder.cpp
    src/JointPropertiesWidget.cpp
    src/PreviewViewport.cpp
    src/RobotEnrichmentDialog.cpp
    src/IntersectionSystem.cpp
    src/URDFParser.cpp
    src/URDFImporterDialog.cpp
    src/KRobotWriter.cpp
    src/KRobotParser.cpp
    src/SDFParser.cpp
    src/static_toolbar.ui
    resources.qrc
    external/pugixml/pugixml.cpp
)

set(HEADER_FILES
    include/MainWindow.hpp
    include/ViewportWidget.hpp
    include/DiagnosticsPanel.hpp
    include/StaticToolbar.hpp
    include/PropertiesPanel.hpp
    include/gridPropertiesWidget.hpp
    include/IntersectionSystem.hpp
    include/RenderingSystem.hpp
    include/URDFParser.hpp
    include/URDFImporterDialog.hpp
    include/KRobotParser.hpp
    include/KRobotWriter.hpp
    include/SDFParser.hpp
    include/SceneBuilder.hpp
    include/PreviewViewport.hpp
    include/RobotEnrichmentDialog.hpp
    include/LinkPropertiesWidget.hpp
    include/JointPropertiesWidget.hpp
)

qt_add_executable(RoboticsSoftware
    MANUAL_FINALIZATION
    ${SOURCE_FILES}
    ${HEADER_FILES}
)

# --- Target-Specific Properties ---
target_compile_definitions(RoboticsSoftware PRIVATE GLM_ENABLE_EXPERIMENTAL)

target_include_directories(RoboticsSoftware PRIVATE
    "include"
    "external"
    "${ADS_INCLUDE_DIR}"
    ${urdfdom_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/external/pugixml 
)

# Note: We are not using target_link_directories() for ADS anymore.
# We will link against the full path found by find_library().

target_link_libraries(RoboticsSoftware PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    OpenGL::GL
    glm::glm
    ${urdfdom_LIBRARIES}
    # Link the correct library using a generator expression
    $<$<CONFIG:Debug>:${ADS_LIBRARY_DEBUG}>
    $<$<CONFIG:Release>:${ADS_LIBRARY_RELEASE}>
)

# --- Post-Build Steps ---
add_custom_command(TARGET RoboticsSoftware POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ADS_BIN_DIR}/$<IF:$<CONFIG:Debug>,qtadvanceddocking-qt6d.dll,qtadvanceddocking-qt6.dll>"
        "$<TARGET_FILE_DIR:RoboticsSoftware>"
    COMMENT "Copying Qt Advanced Docking System DLL..."
)

add_custom_command(TARGET RoboticsSoftware POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:RoboticsSoftware>/shaders"
    COMMENT "Copying shaders to output directory..."
)

add_custom_command(TARGET RoboticsSoftware POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/simple_arm.urdf"
        "$<TARGET_FILE_DIR:RoboticsSoftware>/simple_arm.urdf"
    COMMENT "Copying simple_arm.urdf to output directory..."
)

# --- Deploy Qt Dependencies ---
# Use windeployqt to automatically copy all necessary Qt DLLs, plugins, etc.
# This is the most robust way to handle runtime dependencies on Windows.
add_custom_command(TARGET RoboticsSoftware POST_BUILD
    # Use the Qt6::windeployqt imported executable target. This is the modern, reliable
    # way to call tools provided by Qt in CMake, as it guarantees the correct path.
    COMMAND Qt6::windeployqt
        # Select the correct mode based on the build type
        $<$<CONFIG:Debug>:--debug>
        $<$<CONFIG:Release>:--release>
        # Specify the directory where the executable is
        --dir "$<TARGET_FILE_DIR:RoboticsSoftware>"
        # Specify the executable to scan
        "$<TARGET_FILE:RoboticsSoftware>"
    COMMENT "Running windeployqt to deploy Qt dependencies..."
)

if (MSVC)
    # stack-cookie (/GS) is already on by default for MSVC, but keep it explicit
    target_compile_options(RoboticsSoftware PRIVATE /GS /RTC1)
endif()

# Finalize the Qt setup
qt_finalize_executable(RoboticsSoftware)
