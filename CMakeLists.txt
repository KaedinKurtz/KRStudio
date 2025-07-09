cmake_minimum_required(VERSION 3.20)
project(RoboticsSoftware LANGUAGES CXX)

# ─────────────────────────────────────────────────────────────────────────────
#  Global compiler & Qt meta-object settings
# ─────────────────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS   OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ─────────────────────────────────────────────────────────────────────────────
#  Third-party packages
# ─────────────────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
find_package(OpenGL  REQUIRED)
find_package(glm     REQUIRED)
find_package(assimp  CONFIG REQUIRED)
find_package(urdfdom CONFIG REQUIRED)

# ─────────────────────────────────────────────────────────────────────────────
#  Qt Advanced Docking System (ADS) — build from source
# ─────────────────────────────────────────────────────────────────────────────
set(ADS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/ads")

if(NOT EXISTS "${ADS_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "ADS sources not found at '${ADS_ROOT}'.\n"
        "Did you forget --recursive when cloning?")
endif()

add_subdirectory(${ADS_ROOT})           # creates target: qtadvanceddocking

# ─────────────────────────────────────────────────────────────────────────────
#  Project sources / headers
# ─────────────────────────────────────────────────────────────────────────────
file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/include/*Headers/*.hpp")

file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
     "${CMAKE_CURRENT_SOURCE_DIR}/external/pugixml/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/*.qrc")

qt_add_executable(RoboticsSoftware
    MANUAL_FINALIZATION
    ${SOURCE_FILES}
    ${HEADER_FILES})

# ─────────────────────────────────────────────────────────────────────────────
#  Include paths
# ─────────────────────────────────────────────────────────────────────────────
target_include_directories(RoboticsSoftware PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ObjectHeaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/RenderingHeaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/UIHeaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/UtilityHeaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/external"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/pugixml"
    "${ADS_ROOT}/src"                   # ADS public headers
    ${urdfdom_INCLUDE_DIRS})

# ─────────────────────────────────────────────────────────────────────────────
#  Compile definitions / options
# ─────────────────────────────────────────────────────────────────────────────
target_compile_definitions(RoboticsSoftware PRIVATE
    GLM_ENABLE_EXPERIMENTAL
    RS_DEBUG_DUMP
    NOMINMAX)

if(MSVC)
    target_compile_options(RoboticsSoftware PRIVATE /GS /RTC1)
endif()

# ─────────────────────────────────────────────────────────────────────────────
#  Link libraries
# ─────────────────────────────────────────────────────────────────────────────
target_link_libraries(RoboticsSoftware PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    OpenGL::GL
    glm::glm
    assimp::assimp
    ${urdfdom_LIBRARIES}
    qtadvanceddocking)                  # Debug/Release handled automatically

# ─────────────────────────────────────────────────────────────────────────────
#  Post-build steps
# ─────────────────────────────────────────────────────────────────────────────
# 1) Copy ADS runtime (Windows only)
if(WIN32)
    add_custom_command(TARGET RoboticsSoftware POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:qtadvanceddocking>
                $<TARGET_FILE_DIR:RoboticsSoftware>
        COMMENT "Copying Qt Advanced Docking System runtime…")
endif()

# 2) Copy shaders directory
add_custom_command(TARGET RoboticsSoftware POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
            "$<TARGET_FILE_DIR:RoboticsSoftware>/shaders"
    COMMENT "Copying shaders → output directory…")

# 3) Copy example URDF
add_custom_command(TARGET RoboticsSoftware POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/simple_arm.urdf"
            "$<TARGET_FILE_DIR:RoboticsSoftware>/simple_arm.urdf"
    COMMENT "Copying simple_arm.urdf → output directory…")

# 4) Run windeployqt (Windows) after build
if(WIN32)
    add_custom_command(TARGET RoboticsSoftware POST_BUILD
        COMMAND Qt6::windeployqt
                $<$<CONFIG:Debug>:--debug>
                $<$<CONFIG:Release>:--release>
                --dir "$<TARGET_FILE_DIR:RoboticsSoftware>"
                "$<TARGET_FILE:RoboticsSoftware>"
        COMMENT "Running windeployqt…")
endif()

# ─────────────────────────────────────────────────────────────────────────────
#  Platform-specific application icons
# ─────────────────────────────────────────────────────────────────────────────
if(MSVC)  # Windows resource file with .ico
    target_sources(RoboticsSoftware PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/icons/app.rc)
endif()

if(APPLE) # macOS .icns inside bundle
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/icons/app.icns
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    set_target_properties(RoboticsSoftware PROPERTIES
        MACOSX_BUNDLE            TRUE
        MACOSX_BUNDLE_ICON_FILE  app.icns)
endif()

# ─────────────────────────────────────────────────────────────────────────────
#  Finalise Qt executable (automatically adds Qt plugins on macOS/Linux)
# ─────────────────────────────────────────────────────────────────────────────
qt_finalize_executable(RoboticsSoftware)

# ─────────────────────────────────────────────────────────────────────────────
#  Install rules  (<prefix>/app …)
# ─────────────────────────────────────────────────────────────────────────────
install(TARGETS RoboticsSoftware
        RUNTIME DESTINATION .   # exe goes to prefix root
        BUNDLE  DESTINATION .)  # macOS .app bundle

install(DIRECTORY "${CMAKE_SOURCE_DIR}/shaders"
        DESTINATION shaders)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/icons"
        DESTINATION icons)

install(FILES "${CMAKE_SOURCE_DIR}/simple_arm.urdf"
        DESTINATION .)

#  windeployqt / macdeployqt can be added as install(CODE) blocks if desired.
