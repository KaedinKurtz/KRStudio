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
set(ADS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/ads")
if(NOT EXISTS "${ADS_ROOT}/src")
    message(FATAL_ERROR "ADS_ROOT ('${ADS_ROOT}') does not look like an ADS checkout")
endif()

set(ADS_INCLUDE_DIR  "${ADS_ROOT}/src")
# build output is typically ADS_ROOT/build/<preset>
file(GLOB _ads_libs RELATIVE "${ADS_ROOT}" "${ADS_ROOT}/build/*/x64/lib")
file(GLOB _ads_bins RELATIVE "${ADS_ROOT}" "${ADS_ROOT}/build/*/x64/bin")
list(GET _ads_libs 0 ADS_LIB_DIR)
list(GET _ads_bins 0 ADS_BIN_DIR)
set(ADS_LIB_DIR "${ADS_ROOT}/${ADS_LIB_DIR}")
set(ADS_BIN_DIR "${ADS_ROOT}/${ADS_BIN_DIR}")

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
    HINTS "${ADS_LIB_DIR}"
)

find_package(assimp CONFIG REQUIRED)


# --- Define the Executable Target ---

file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*Headers/*.hpp"
)

file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/pugixml/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.qrc"
)

set(PROJECT_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ObjectHeaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/RenderingHeaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/UIHeaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/UtilityHeaders"
)

qt_add_executable(RoboticsSoftware
    MANUAL_FINALIZATION
    ${SOURCE_FILES}
    ${HEADER_FILES}
)

add_compile_definitions(RS_DEBUG_DUMP)

# --- Target-Specific Properties ---
target_compile_definitions(RoboticsSoftware PRIVATE GLM_ENABLE_EXPERIMENTAL)

target_include_directories(RoboticsSoftware PRIVATE
    ${PROJECT_INCLUDE_DIRS}                 # new header structure
    "${CMAKE_CURRENT_SOURCE_DIR}/external"  # existing external dir
    "${ADS_INCLUDE_DIR}"
    ${urdfdom_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/external/pugixml"
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
    assimp::assimp   
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
