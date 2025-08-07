# Get the directory of the vcpkg toolchain file from the presets
get_filename_component(VCPKG_SCRIPTS_DIR ${CMAKE_TOOLCHAIN_FILE} DIRECTORY)
get_filename_component(VCPKG_SCRIPTS_DIR ${VCPKG_SCRIPTS_DIR} DIRECTORY)
get_filename_component(CORRECT_VCPKG_ROOT ${VCPKG_SCRIPTS_DIR} DIRECTORY)

# Force the correct VCPKG_ROOT path into the cache, overriding any bad values
set(VCPKG_ROOT ${CORRECT_VCPKG_ROOT} CACHE PATH "vcpkg root folder" FORCE)
message(STATUS "Forcibly setting VCPKG_ROOT to: ${VCPKG_ROOT}")

cmake_minimum_required(VERSION 3.24)
project(RoboticsSoftware LANGUAGES CXX)

# ??? Global compiler & Qt meta-object settings ???????????????????????????????
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ??? Third-party packages ?????????????????????????????????????????????????????
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Sql Concurrent)
find_package(OpenGL REQUIRED)
find_package(glm     REQUIRED)
find_package(assimp  CONFIG REQUIRED)
find_package(urdfdom CONFIG REQUIRED)

set(realsense2_DIR "C:/Users/kaedi/vcpkg/installed/x64-windows/share/realsense2")
find_package(realsense2 REQUIRED)

find_package(OpenCV REQUIRED COMPONENTS core imgproc features2d calib3d)

# ??? 0) Debug: show where your EXE will go ??????????????????????????????????????
message(STATUS "CMAKE_RUNTIME_OUTPUT_DIRECTORY = ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

# ??? 1) Locate Qt’s own “sqldrivers” folder via qtpaths ?????????????????????????
find_program(QTPATHS_EXECUTABLE
  NAMES qtpaths
  DOC "qtpaths tool from your Qt installation"
)
if(NOT QTPATHS_EXECUTABLE)
  message(FATAL_ERROR "qtpaths not found – needed to locate Qt SQL plugins")
endif()

execute_process(
  COMMAND "${QTPATHS_EXECUTABLE}" --plugin-dir sqldrivers
  OUTPUT_VARIABLE QT_SQL_DRIVERS_SOURCE
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "Qt SQL plugin dir (will be deployed): ${QT_SQL_DRIVERS_SOURCE}")

# ??? 2) FetchContent for QtADS, QtNodes, sqlite_official_src ???????????????????
include(FetchContent)

# Qt Advanced Docking System
FetchContent_Declare(
  qtads
  GIT_REPOSITORY https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git
  GIT_TAG        4.4.0
  GIT_SHALLOW    TRUE
)
set(ADS_SKIP_GIT_VERSION ON CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF       CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(qtads)

# Node Editor
FetchContent_Declare(
  QtNodes
  GIT_REPOSITORY https://github.com/paceholder/nodeeditor.git
  GIT_TAG        3.0.11
  GIT_SHALLOW    TRUE
)
set(QTNODES_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(QtNodes)

# SQLite official amalgamation
FetchContent_Declare(
  sqlite_official_src
  URL        https://www.sqlite.org/2024/sqlite-amalgamation-3460000.zip
  URL_HASH   SHA256=712a7d09d2a22652fb06a49af516e051979a3984adb067da86760e60ed51a7f5
)
FetchContent_Populate(sqlite_official_src)
add_library(sqlite3 STATIC "${sqlite_official_src_SOURCE_DIR}/sqlite3.c")
target_include_directories(sqlite3 PUBLIC "${sqlite_official_src_SOURCE_DIR}")

# ??? 3) Collect project sources ????????????????????????????????????????????????
file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/include/*Headers/*.hpp")
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Nodes/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Object/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Rendering/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/SLAM/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UI/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Utility/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Resources/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/pugixml/*.cpp"
)

message(STATUS ">>> RoboticsSoftware sources: ${SOURCE_FILES}")

file(GLOB ROOT_QRC_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.qrc"
)
list(APPEND SOURCE_FILES ${ROOT_QRC_FILES})

# ??? 4) Define the executable ??????????????????????????????????????????????????
qt_add_executable(RoboticsSoftware
    MANUAL_FINALIZATION
    ${SOURCE_FILES}
    ${HEADER_FILES}
)
set_target_properties(RoboticsSoftware PROPERTIES OUTPUT_NAME "KRStudio")

# ??? 5) Include paths ??????????????????????????????????????????????????????????
target_include_directories(RoboticsSoftware PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ObjectHeaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/RenderingHeaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/UIHeaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/UtilityHeaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/SLAMHeaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/NodeHeaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ResourcesHeaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/external"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/pugixml"
    "${qtads_SOURCE_DIR}/src"
    ${urdfdom_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
)

# ??? 6) Compile definitions / options ??????????????????????????????????????????
target_compile_definitions(RoboticsSoftware PRIVATE
    GLM_ENABLE_EXPERIMENTAL
    RS_DEBUG_DUMP
    NOMINMAX
)

if(MSVC)
    target_compile_options(RoboticsSoftware PRIVATE
        /GS
        $<$<CONFIG:Debug>:/RTC1>
    )
endif()

# ??? 7) Link libraries ?????????????????????????????????????????????????????????
target_link_libraries(RoboticsSoftware PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Sql Qt6::Concurrent
    OpenGL::GL
    glm::glm
    assimp::assimp
    ${urdfdom_LIBRARIES}
    ads::qtadvanceddocking-qt6
    QtNodes
    realsense2::realsense2
    ${OpenCV_LIBS}
    sqlite3
)

if(MSVC)
  target_link_options(RoboticsSoftware PRIVATE "/MAP")
endif()

qt_finalize_executable(RoboticsSoftware)

# ???????????????????????????????????????????????????????????????????????????????
#  8) Post-build Deployment
# ???????????????????????????????????????????????????????????????????????????????
if (WIN32)
    # --- Part 1: Run windeployqt and copy common assets for all configs ---
    add_custom_command(TARGET RoboticsSoftware POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "---- Deploying dependencies for $<CONFIG> build into $<TARGET_FILE_DIR:RoboticsSoftware> ----"

        # Step 1: Use windeployqt to deploy all standard Qt components.
        COMMAND $<TARGET_FILE:Qt6::windeployqt>
            $<$<CONFIG:Debug>:--debug>
            $<$<CONFIG:Release>:--release>
            --dir $<TARGET_FILE_DIR:RoboticsSoftware>
            $<TARGET_FILE:RoboticsSoftware>

        # Step 2: Copy third-party runtimes (ADS & Nodes).
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ads::qtadvanceddocking-qt6>
            $<TARGET_FILE_DIR:RoboticsSoftware>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:QtNodes>
            $<TARGET_FILE_DIR:RoboticsSoftware>

        # Step 3: Copy project assets.
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
            "$<TARGET_FILE_DIR:RoboticsSoftware>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/simple_arm.urdf"
            "$<TARGET_FILE_DIR:RoboticsSoftware>/simple_arm.urdf"

        COMMENT "Deploying Qt runtime, third-party DLLs, and project assets..."
    )

    # --- Part 2: Copy DEBUG-ONLY dependencies ---
    if(MSVC)
        # Find all MSVC debug runtime DLLs.
        file(GLOB MSVC_DEBUG_CRT_DLLS
            "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Redist/MSVC/*/debug_nonredist/x64/*.dll"
        )

        # Create a conditional post-build command to copy each debug runtime DLL.
        foreach(file_path ${MSVC_DEBUG_CRT_DLLS})
            add_custom_command(TARGET RoboticsSoftware POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<$<CONFIG:Debug>:${file_path}>"
                    "$<TARGET_FILE_DIR:RoboticsSoftware>"
            )
        endforeach()

        # Create a conditional post-build command to copy the debug sqlite3.dll.
        add_custom_command(TARGET RoboticsSoftware POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<$<CONFIG:Debug>:${VCPKG_ROOT}/installed/x64-windows/debug/bin/sqlite3.dll>"
                "$<TARGET_FILE_DIR:RoboticsSoftware>"
            COMMENT "Deploying debug-only dependencies (MSVC Runtimes, sqlite3)..."
        )
    endif()
endif()

# ??? 9) Install rules ???????????????????????????????????????????????????????????
install(TARGETS RoboticsSoftware RUNTIME DESTINATION . BUNDLE DESTINATION .)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/shaders" DESTINATION shaders)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/icons"   DESTINATION icons)
install(FILES "${CMAKE_SOURCE_DIR}/simple_arm.urdf" DESTINATION .)
