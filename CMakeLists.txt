cmake_minimum_required(VERSION 3.24)           # 3.24  full FetchContent GIT_SPARSE support
project(RoboticsSoftware LANGUAGES CXX)

#   Global compiler & Qt meta-object settings

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

#  Third-party packages (via find_package)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
find_package(OpenGL REQUIRED)
find_package(glm     REQUIRED)
find_package(assimp  CONFIG REQUIRED)
find_package(urdfdom CONFIG REQUIRED)

#  Qt Advanced Docking System  FetchContent

include(FetchContent)

# The newest *stable* tag as of 2025-07-08 is 4.4.0.
# If you ever want to follow HEAD, just change GIT_TAG to "master".
FetchContent_Declare(
  qtads
  GIT_REPOSITORY https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git
  GIT_TAG        4.4.0
  GIT_SHALLOW    TRUE                 # faster checkout
)

#   Disable the internal git describe version logic that breaks when
#    the source is fetched without .git metadata (CI tarballs, shallow clones).
set(ADS_SKIP_GIT_VERSION ON CACHE BOOL "Skip git-based versioning" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Disable ADS examples" FORCE)

FetchContent_MakeAvailable(qtads)      # creates the target: QtADS

#  Project sources / headers

file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/include/*Headers/*.hpp")

file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/pugixml/*.cpp"
)

# Find .qrc files ONLY in the project's root directory (NOT recursive)
file(GLOB ROOT_QRC_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.qrc"
)

# Add the root .qrc files to the list of sources
list(APPEND SOURCE_FILES ${ROOT_QRC_FILES})

qt_add_executable(RoboticsSoftware
    MANUAL_FINALIZATION
    ${SOURCE_FILES}
    ${HEADER_FILES})

#  Include paths

target_include_directories(RoboticsSoftware PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ObjectHeaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/RenderingHeaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/UIHeaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/UtilityHeaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/external"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/pugixml"
    "${qtads_SOURCE_DIR}/src"          # ADS public headers
    ${urdfdom_INCLUDE_DIRS})

#  Compile definitions / options

target_compile_definitions(RoboticsSoftware PRIVATE
    GLM_ENABLE_EXPERIMENTAL
    RS_DEBUG_DUMP
    NOMINMAX)

if(MSVC)
    target_compile_options(RoboticsSoftware PRIVATE
        /GS
        $<$<CONFIG:Debug>:/RTC1>
    )
endif()

#  Link libraries

target_link_libraries(RoboticsSoftware PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    OpenGL::GL
    glm::glm
    assimp::assimp
    ${urdfdom_LIBRARIES}
    ads::qtadvanceddocking-qt6)                          # provided by FetchContent

#  Post-build steps

# 1) Copy ADS runtime (Windows only)

if(WIN32)
    add_custom_command(TARGET RoboticsSoftware POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:ads::qtadvanceddocking-qt6> #  CORRECTED: Use the namespaced alias
                $<TARGET_FILE_DIR:RoboticsSoftware>
        COMMENT "Copying QtADS runtime")
endif()

# 2) Copy shaders directory
add_custom_command(TARGET RoboticsSoftware POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
            "$<TARGET_FILE_DIR:RoboticsSoftware>/shaders"
    COMMENT "Copying shaders to output directory")

# 3) Copy example URDF
add_custom_command(TARGET RoboticsSoftware POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/simple_arm.urdf"
            "$<TARGET_FILE_DIR:RoboticsSoftware>/simple_arm.urdf"
    COMMENT "Copying simple_arm.urdf to output directory…")

# 4) Run windeployqt (Windows) after build
if(WIN32)
    add_custom_command(TARGET RoboticsSoftware POST_BUILD
        COMMAND Qt6::windeployqt
                $<$<CONFIG:Debug>:--debug>
                $<$<CONFIG:Release>:--release>
                --dir "$<TARGET_FILE_DIR:RoboticsSoftware>"
                "$<TARGET_FILE:RoboticsSoftware>"
        COMMENT "Running windeployqt")
endif()

#  Platform-specific application icons

if(MSVC)  # Windows resource file with .ico
    target_sources(RoboticsSoftware PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/icons/app.rc)
endif()

if(APPLE) # macOS .icns inside bundle
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/icons/app.icns
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    set_target_properties(RoboticsSoftware PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_ICON_FILE app.icns)
endif()

#  Finalise Qt executable

qt_finalize_executable(RoboticsSoftware)

#  Install rules

install(TARGETS RoboticsSoftware
        RUNTIME DESTINATION .
        BUNDLE  DESTINATION .)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/shaders" DESTINATION shaders)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/icons"   DESTINATION icons)
install(FILES "${CMAKE_SOURCE_DIR}/simple_arm.urdf" DESTINATION .)
