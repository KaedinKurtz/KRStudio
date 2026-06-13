# Minimum CMake version and project definition.
cmake_minimum_required(VERSION 3.24)
project(RoboticsSoftware LANGUAGES CXX)

# Set new policy for FindBoost to reduce noise from OpenVDB's find script.
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

# ---------- BEGIN robust vcpkg/Qt fallback (no toolchain case) ----------
# This block provides a fallback to find Qt if CMAKE_TOOLCHAIN_FILE is not set.
# It's useful for developers who might configure the project without a CMake preset.
if(NOT CMAKE_TOOLCHAIN_FILE)
  # Pick a default vcpkg triplet based on the OS if none is set in the environment.
  set(_trip "$ENV{VCPKG_DEFAULT_TRIPLET}")
  if(NOT _trip)
    if(WIN32)
      set(_trip "x64-windows")
    elseif(APPLE)
      set(_trip "arm64-osx")
    else()
      set(_trip "x64-linux")
    endif()
  endif()

  # A list of common locations for the vcpkg root directory.
  set(_roots "")
  if(DEFINED ENV{VCPKG_ROOT})
    list(APPEND _roots "$ENV{VCPKG_ROOT}")
  endif()
  if(DEFINED ENV{vcpkg})
    list(APPEND _roots "$ENV{vcpkg}")
  endif()
  list(APPEND _roots
        "C:/vcpkg"
        "$ENV{USERPROFILE}/vcpkg"
        "$ENV{LOCALAPPDATA}/vcpkg")

  # Iterate through the possible vcpkg roots to find a valid Qt installation.
  set(_qt_ok FALSE)
  foreach(_r IN LISTS _roots)
    file(TO_CMAKE_PATH "${_r}" _rN)
    if(EXISTS "${_rN}/installed/${_trip}")
      set(_pref "${_rN}/installed/${_trip}")
      # Check for Qt6 first.
      if(EXISTS "${_pref}/lib/cmake/Qt6/Qt6Config.cmake")
        list(APPEND CMAKE_PREFIX_PATH "${_pref}")
        set(Qt6_DIR "${_pref}/lib/cmake/Qt6" CACHE PATH "Qt6 dir from vcpkg fallback" FORCE)
        if(WIN32)
          list(APPEND CMAKE_PREFIX_PATH "${_pref}/tools/Qt6")
        endif()
        message(STATUS "Using Qt6 from: ${Qt6_DIR}")
        set(_qt_ok TRUE)
        break()
      endif()
      # Optional: Check for Qt5 as a fallback.
      if(EXISTS "${_pref}/lib/cmake/Qt5/Qt5Config.cmake")
        list(APPEND CMAKE_PREFIX_PATH "${_pref}")
        set(Qt5_DIR "${_pref}/lib/cmake/Qt5" CACHE PATH "Qt5 dir from vcpkg fallback" FORCE)
        if(WIN32)
          list(APPEND CMAKE_PREFIX_PATH "${_pref}/tools/Qt5")
        endif()
        message(STATUS "Using Qt5 from: ${Qt5_DIR}")
        set(_qt_ok TRUE)
        break()
      endif()
    endif()
  endforeach()

  # If no Qt installation was found, issue a warning with instructions.
  if(NOT _qt_ok)
    message(WARNING
      "CMAKE_TOOLCHAIN_FILE is not set and no Qt found in vcpkg prefixes.\n"
      "Fix one of these:\n"
      "  1) Select a CMake Preset (uses vcpkg toolchain), or\n"
      "  2) Define VCPKG_ROOT to your vcpkg (e.g. C:/Users/YourUser/vcpkg) and reconfigure, or\n"
      "  3) Run: vcpkg.exe install qtbase --triplet ${_trip}")
  endif()
endif()
# ---------- END robust fallback ----------


# -----------------------------
#  Global Configuration
# -----------------------------
# Default to an optimized build — an unoptimized Debug build of this engine
# runs at a fraction of the framerate. Use the *-debug presets to debug.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable Qt's automatic meta-object compiler, UI compiler, and resource compiler.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# On MSVC, enable the /bigobj flag to handle large object files.
if(MSVC)
  add_compile_options(/bigobj)
  # OpenVDB 12 headers require a standards-conformant compiler; only the
  # SDF baking TU includes them, so scope /permissive- to that file.
  set_source_files_properties("${CMAKE_SOURCE_DIR}/src/Rendering/SdfBaker.cpp"
    PROPERTIES COMPILE_OPTIONS "/permissive-;/Zc:__cplusplus")
  set_source_files_properties("${CMAKE_SOURCE_DIR}/src/Rendering/FluidMesher.cpp"
    PROPERTIES COMPILE_OPTIONS "/permissive-;/Zc:__cplusplus")
endif()


# -----------------------------
#  Options to Toggle Dependencies
# -----------------------------
option(ENABLE_PHYSX           "Link PhysX 5 (rigid+soft+cloth)" ON)
option(ENABLE_OPENVDB         "Use OpenVDB for SDF baking"      ON)
option(ENABLE_NANOVDB         "Use NanoVDB for GPU SDFs (requires overlay)" OFF)
option(ENABLE_IGL             "Use libigl (geom utils)"         ON)
option(ENABLE_FCL             "Use FCL/libccd proximity"        ON)
option(ENABLE_CHRONO          "Enable Project Chrono (optional)" OFF)
option(ENABLE_SPLISH_SPLASH   "Build SPlisHSPlasH from submodule" ON)
option(ENABLE_COACD           "Build CoACD from submodule (CLI/lib)" ON)

# Force NanoVDB disabled unless you explicitly enable it in cache/presets
set(ENABLE_NANOVDB OFF CACHE BOOL "Disable NanoVDB unless overlay provided" FORCE)
# Tell the vcpkg OpenVDB wrapper not to try NanoVDB
set(OPENVDB_ENABLE_NANOVDB OFF CACHE BOOL "Disable NanoVDB integration in OpenVDB wrapper" FORCE)


# -----------------------------
#  Find Required Dependencies
# -----------------------------
# Qt (Tools is needed for the windeployqt target on Windows)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Sql Network Concurrent Tools)
find_package(OpenGL REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(urdfdom CONFIG REQUIRED)
find_package(urdfdom_headers CONFIG REQUIRED)
find_package(realsense2 CONFIG REQUIRED)
find_package(assimp QUIET)
find_package(OpenCV QUIET)
find_package(imgui CONFIG REQUIRED)


# -----------------------------
#  Fetch Dependencies at Configure Time
# -----------------------------
include(FetchContent)

# Qt Advanced Docking System
FetchContent_Declare(
  qtads
  GIT_REPOSITORY https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git
  GIT_TAG        4.4.0
  GIT_SHALLOW    TRUE
)
set(ADS_SKIP_GIT_VERSION ON CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
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
  URL      https://www.sqlite.org/2024/sqlite-amalgamation-3460000.zip
  URL_HASH SHA256=712a7d09d2a22652fb06a49af516e051979a3984adb067da86760e60ed51a7f5
)
FetchContent_Populate(sqlite_official_src)
add_library(sqlite3 STATIC "${sqlite_official_src_SOURCE_DIR}/sqlite3.c")
target_include_directories(sqlite3 PUBLIC "${sqlite_official_src_SOURCE_DIR}")


# -----------------------------
#  Add Submodules (Optional)
# -----------------------------
# SPlisHSPlasH is built as an isolated ExternalProject superbuild: its CMake
# overwrites global flags and the runtime output dir (CACHE INTERNAL), and
# clones its own deps (CompactNSearch/Discregrid/GenericParameters/PBD) at
# build time — add_subdirectory would hijack this project's build graph.
set(KR_HAS_SPLISH 0)
if(ENABLE_SPLISH_SPLASH AND EXISTS "${CMAKE_SOURCE_DIR}/external/SPlisHSPlasH/CMakeLists.txt")
  include(ExternalProject)
  set(SPLISH_SRC   "${CMAKE_SOURCE_DIR}/external/SPlisHSPlasH")
  set(SPLISH_BUILD "${CMAKE_BINARY_DIR}/splishsplash-build")
  set(SPLISH_EIGEN "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/eigen3")
  ExternalProject_Add(splishsplash_ext
    SOURCE_DIR "${SPLISH_SRC}"
    BINARY_DIR "${SPLISH_BUILD}"
    PREFIX "${CMAKE_BINARY_DIR}/splishsplash-prefix"
    CMAKE_ARGS
      -DSPH_LIBS_ONLY=On
      -DUSE_PYTHON_BINDINGS=Off
      -DUSE_DOUBLE_PRECISION=Off
      -DUSE_AVX=On
      -DUSE_OpenMP=On
      -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_POLICY_VERSION_MINIMUM=3.10
      -DEIGEN3_INCLUDE_DIR:PATH=${SPLISH_EIGEN}
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS
      "${SPLISH_BUILD}/lib/SPlisHSPlasH.lib"
      "${SPLISH_BUILD}/lib/Utilities.lib"
      "${SPLISH_BUILD}/lib/partio.lib"
      "${SPLISH_BUILD}/lib/zlib.lib"
      "${SPLISH_BUILD}/lib/tinyexpr.lib"
      "${SPLISH_BUILD}/lib/MD5.lib"
      "${SPLISH_BUILD}/extern/install/NeighborhoodSearch/lib/CompactNSearch.lib"
      "${SPLISH_BUILD}/extern/install/Discregrid/lib/Discregrid.lib"
  )
  set(KR_HAS_SPLISH 1)
endif()

if(ENABLE_COACD AND EXISTS "${CMAKE_SOURCE_DIR}/external/CoACD/CMakeLists.txt")
  add_subdirectory(external/CoACD)
endif()


# -----------------------------
#  Define Executable and Sources
# -----------------------------
# Use the verbose, specific GLOB logic from your older file to correctly discover all source files.
file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/include/*Headers/*.hpp")

# FIX: Use specific, non-overlapping GLOB calls to prevent searching the build directory.
# This is the definitive fix for the "multiple rules" error.
file(GLOB_RECURSE NODE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/Nodes/*.cpp")
file(GLOB_RECURSE OBJECT_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/Object/*.cpp")
file(GLOB_RECURSE RENDERING_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/Rendering/*.cpp")
file(GLOB_RECURSE SLAM_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/SLAM/*.cpp")
file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/UI/*.cpp")
file(GLOB_RECURSE UTILITY_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/Utility/*.cpp")
file(GLOB_RECURSE RESOURCES_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/Resources/*.cpp")
file(GLOB_RECURSE PUGIXML_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/external/pugixml/*.cpp")

set(SOURCE_FILES
    ${NODE_SOURCES}
    ${OBJECT_SOURCES}
    ${RENDERING_SOURCES}
    ${SLAM_SOURCES}
    ${UI_SOURCES}
    ${UTILITY_SOURCES}
    ${RESOURCES_SOURCES}
    ${PUGIXML_SOURCES}
    "${CMAKE_SOURCE_DIR}/src/main.cpp"
)

file(GLOB ROOT_QRC_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.qrc"
)
list(APPEND SOURCE_FILES ${ROOT_QRC_FILES})
list(REMOVE_DUPLICATES SOURCE_FILES)


# Define the executable with its sources.
# Do NOT pass header files directly to qt_add_executable. This confuses AUTOMOC
# and is a likely cause of the "multiple rules" error.
qt_add_executable(RoboticsSoftware
  MANUAL_FINALIZATION
  ${SOURCE_FILES}
)

# Associate headers with the target for IDEs, but don't compile them.
target_sources(RoboticsSoftware PRIVATE ${HEADER_FILES})


# -----------------------------
#  Include Directories
# -----------------------------
# Restore the specific include directories required by your project structure.
target_include_directories(RoboticsSoftware PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/ObjectHeaders"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/RenderingHeaders"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/UIHeaders"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/UtilityHeaders"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/SLAMHeaders"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/NodeHeaders"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/ResourcesHeaders"
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/external"
)


# -----------------------------
#  Link Libraries
# -----------------------------
# Use a combination of modern targets and older library variables, as dictated by each package.
target_link_libraries(RoboticsSoftware PRIVATE
  # Qt components
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Sql Qt6::Network Qt6::Concurrent

  # Core libraries
  OpenGL::GL
  glm::glm
  realsense2::realsense2
  urdfdom_headers::urdfdom_headers
  imgui::imgui

  # Fetched content targets
  ads::qtadvanceddocking-qt6
  QtNodes # Use direct target name as in the old file
  sqlite3
)

# HIL bridges: high-resolution timer (winmm) + Winsock for the UDP loopback
# camera/CAN transport on Windows. SocketCAN/v4l2loopback backends are Linux.
if(WIN32)
  target_link_libraries(RoboticsSoftware PRIVATE winmm ws2_32)
endif()

# Link urdfdom using the older variable-based method.
if(urdfdom_LIBRARIES)
  target_link_libraries(RoboticsSoftware PRIVATE ${urdfdom_LIBRARIES})
else()
  message(WARNING "find_package(urdfdom) did not set urdfdom_LIBRARIES.")
endif()
if(urdfdom_INCLUDE_DIRS)
    target_include_directories(RoboticsSoftware PRIVATE ${urdfdom_INCLUDE_DIRS})
endif()


# Link optional libraries found by find_package.
if(assimp_FOUND)
  target_link_libraries(RoboticsSoftware PRIVATE assimp::assimp)
endif()
if(OpenCV_FOUND)
  target_link_libraries(RoboticsSoftware PRIVATE ${OpenCV_LIBS})
  target_include_directories(RoboticsSoftware PRIVATE ${OpenCV_INCLUDE_DIRS})
endif()


# -----------------------------
#  Conditional Linking & Compile Definitions
# -----------------------------
# This section sets up linking and C++ preprocessor definitions based on the ENABLE_* options.

# --- PhysX ---
set(KR_HAS_PHYSX 0)
if(ENABLE_PHYSX)
  find_package(unofficial-omniverse-physx-sdk CONFIG QUIET)
  if(TARGET unofficial::omniverse-physx-sdk::sdk)
    target_link_libraries(RoboticsSoftware PRIVATE unofficial::omniverse-physx-sdk::sdk)
    set(KR_HAS_PHYSX 1)
    message(STATUS "PhysX found and linked (unofficial-omniverse-physx-sdk)")
  else()
    message(WARNING "ENABLE_PHYSX is ON but the package was not found. Install 'physx' with vcpkg.")
  endif()
endif()

# --- V-HACD (header-only approximate convex decomposition) ---
set(KR_HAS_VHACD 0)
find_path(VHACD_INCLUDE_DIRS "VHACD.h")
if(VHACD_INCLUDE_DIRS)
  target_include_directories(RoboticsSoftware PRIVATE ${VHACD_INCLUDE_DIRS})
  set(KR_HAS_VHACD 1)
  message(STATUS "V-HACD found: ${VHACD_INCLUDE_DIRS}")
else()
  message(WARNING "V-HACD not found. Install 'v-hacd' with vcpkg for convex decomposition.")
endif()

# --- libigl ---
set(KR_HAS_IGL 0)
if(ENABLE_IGL)
  find_package(libigl CONFIG QUIET)
  if(TARGET igl::core)
    target_link_libraries(RoboticsSoftware PRIVATE igl::core)
    set(KR_HAS_IGL 1)
    message(STATUS "libigl found and linked (igl::core)")
  else()
    message(WARNING "ENABLE_IGL is ON but the package was not found. Install 'libigl' with vcpkg.")
  endif()
endif()

# --- FCL ---
set(KR_HAS_FCL 0)
if(ENABLE_FCL)
  find_package(fcl CONFIG QUIET)
  find_package(ccd CONFIG QUIET)
  if(TARGET fcl::fcl AND TARGET ccd::ccd)
    target_link_libraries(RoboticsSoftware PRIVATE fcl::fcl ccd::ccd)
    set(KR_HAS_FCL 1)
    message(STATUS "FCL and libccd found and linked.")
  else()
    message(WARNING "ENABLE_FCL is ON but fcl/ccd packages were not found. Install 'fcl' and 'ccd' with vcpkg.")
  endif()
endif()

# --- OpenVDB ---
if(ENABLE_OPENVDB)
  find_package(OpenVDB CONFIG QUIET)
  find_package(TBB CONFIG QUIET)
  if(TARGET OpenVDB::openvdb AND TARGET TBB::tbb)
    target_link_libraries(RoboticsSoftware PRIVATE OpenVDB::openvdb TBB::tbb)
    message(STATUS "OpenVDB and TBB found and linked.")
  else()
    message(WARNING "ENABLE_OPENVDB is ON but OpenVDB/TBB packages were not found. Install 'openvdb' with vcpkg.")
  endif()
endif()

# --- OpenCASCADE (STEP CAD ingestion + feature recognition, Phase 4) ---
# Link the config's full toolkit set rather than naming TKSTEP/TKBRep directly:
# OCCT 7.8/8.0 reorganised the data-exchange toolkits (TKSTEP -> TKDESTEP), so
# ${OpenCASCADE_LIBRARIES} is the version-robust way to pull in the readers.
find_package(OpenCASCADE CONFIG QUIET)
if(OpenCASCADE_FOUND)
  target_include_directories(RoboticsSoftware PRIVATE ${OpenCASCADE_INCLUDE_DIR})
  target_link_libraries(RoboticsSoftware PRIVATE ${OpenCASCADE_LIBRARIES})
  target_compile_definitions(RoboticsSoftware PRIVATE KR_WITH_OCCT)
  message(STATUS "OpenCASCADE ${OpenCASCADE_VERSION} found; STEP import enabled (KR_WITH_OCCT).")
else()
  message(STATUS "OpenCASCADE not found; STEP import compiled out (CadImporter stub).")
endif()

# --- Chrono ---
set(KR_HAS_CHRONO 0)
if(ENABLE_CHRONO)
  find_package(Chrono QUIET COMPONENTS ChronoEngine)
  if(TARGET ChronoEngine)
    target_link_libraries(RoboticsSoftware PRIVATE ChronoEngine)
    set(KR_HAS_CHRONO 1)
  endif()
endif()

# --- Submodules ---
if(KR_HAS_SPLISH)
  add_dependencies(RoboticsSoftware splishsplash_ext)
  target_include_directories(RoboticsSoftware PRIVATE
    "${SPLISH_SRC}"
    "${SPLISH_BUILD}/extern/install/NeighborhoodSearch/include"
    "${SPLISH_BUILD}/extern/install/Discregrid/include"
    "${SPLISH_BUILD}/extern/install/GenericParameters/include"
    "${SPLISH_EIGEN}"
  )
  target_link_libraries(RoboticsSoftware PRIVATE
    "${SPLISH_BUILD}/lib/SPlisHSPlasH.lib"
    "${SPLISH_BUILD}/lib/Utilities.lib"
    "${SPLISH_BUILD}/extern/install/NeighborhoodSearch/lib/CompactNSearch.lib"
    "${SPLISH_BUILD}/extern/install/Discregrid/lib/Discregrid.lib"
    "${SPLISH_BUILD}/lib/partio.lib"
    "${SPLISH_BUILD}/lib/zlib.lib"
    "${SPLISH_BUILD}/lib/tinyexpr.lib"
    "${SPLISH_BUILD}/lib/MD5.lib"
  )
  find_package(OpenMP REQUIRED)
  target_link_libraries(RoboticsSoftware PRIVATE OpenMP::OpenMP_CXX)
  # The adapter TU must compile with the SAME defines/arch as the library —
  # SPlisHSPlasH headers change type layouts with these.
  set_source_files_properties("${CMAKE_SOURCE_DIR}/src/Rendering/DfsphBackend.cpp"
    PROPERTIES
      COMPILE_OPTIONS "/arch:AVX2"
      COMPILE_DEFINITIONS "USE_AVX;USE_PERFORMANCE_OPTIMIZATION;USE_THIRD_PARTY_METHODS;EIGEN_DISABLE_UNALIGNED_ARRAY_ASSERT;_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING")
endif()
if(ENABLE_COACD AND TARGET CoACD) # Assuming the target is named 'CoACD'
  target_link_libraries(RoboticsSoftware PRIVATE CoACD)
endif()


# Set preprocessor definitions for use in C++ code.
target_compile_definitions(RoboticsSoftware PRIVATE
  GLM_ENABLE_EXPERIMENTAL
  NOMINMAX
  $<$<BOOL:${ENABLE_OPENVDB}>:KR_WITH_OPENVDB=1>
  $<$<BOOL:${KR_HAS_PHYSX}>:KR_WITH_PHYSX=1>
  $<$<BOOL:${KR_HAS_VHACD}>:KR_WITH_VHACD=1>
  $<$<BOOL:${KR_HAS_IGL}>:KR_WITH_IGL=1>
  $<$<BOOL:${KR_HAS_FCL}>:KR_WITH_FCL=1>
  $<$<BOOL:${KR_HAS_CHRONO}>:KR_WITH_CHRONO=1>
  $<$<BOOL:${KR_HAS_SPLISH}>:KR_WITH_SPLISHSPLASH=1>
  $<$<BOOL:${ENABLE_COACD}>:KR_WITH_COACD=1>
)

# Finalize the Qt executable (handles moc, uic, rcc steps).
qt_finalize_executable(RoboticsSoftware)


# -----------------------------
#  Deployment and Installation
# -----------------------------
if(WIN32)
  # This command runs after a successful build on Windows.
  # It uses Qt's deployment tool to copy all necessary Qt DLLs into the build directory.
  add_custom_command(TARGET RoboticsSoftware POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "---- Deploying Qt runtime into $<TARGET_FILE_DIR:RoboticsSoftware> ----"
    COMMAND "$<TARGET_FILE:Qt6::windeployqt>"
            $<$<CONFIG:Debug>:--debug>
            $<$<CONFIG:Release>:--release>
            --no-translations
            --dir "$<TARGET_FILE_DIR:RoboticsSoftware>"
            "$<TARGET_FILE:RoboticsSoftware>"
    COMMENT "Deploying Qt runtime and dependencies..."
  )

  # Copy DLLs from FetchContent dependencies to the output directory so the EXE can run.
  # get_target_property must be called *before* the if() statement.
  if(TARGET ads::qtadvanceddocking-qt6)
    get_target_property(ADS_LIB_TYPE ads::qtadvanceddocking-qt6 TYPE)
    if(ADS_LIB_TYPE STREQUAL "SHARED_LIBRARY")
      add_custom_command(TARGET RoboticsSoftware POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:ads::qtadvanceddocking-qt6>
                $<TARGET_FILE_DIR:RoboticsSoftware>)
    endif()
  endif()
  if(TARGET QtNodes)
    get_target_property(QTNODES_LIB_TYPE QtNodes TYPE)
    if(QTNODES_LIB_TYPE STREQUAL "SHARED_LIBRARY")
      add_custom_command(TARGET RoboticsSoftware POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:QtNodes>
                $<TARGET_FILE_DIR:RoboticsSoftware>)
    endif()
  endif()

  # Copy project assets like shaders and icons to the output directory.
  add_custom_command(TARGET RoboticsSoftware POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:RoboticsSoftware>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/icons"   "$<TARGET_FILE_DIR:RoboticsSoftware>/icons"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/simple_arm.urdf" "$<TARGET_FILE_DIR:RoboticsSoftware>/simple_arm.urdf"
  )
  add_custom_command(TARGET RoboticsSoftware POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E echo "---- Deploying assets to $<TARGET_FILE_DIR:RoboticsSoftware> ----"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/assets"
          "$<TARGET_FILE_DIR:RoboticsSoftware>/assets"
)

  # windeployqt copies the qsqlite plugin but not its vcpkg dependency:
  # without sqlite3.dll next to the exe the QSQLITE driver fails to load
  # and the app exits at database init.
  if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    add_custom_command(TARGET RoboticsSoftware POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}$<$<CONFIG:Debug>:/debug>/bin/sqlite3.dll"
              "$<TARGET_FILE_DIR:RoboticsSoftware>")
  endif()
endif()

# Define installation rules for creating a distributable package.
install(TARGETS RoboticsSoftware RUNTIME DESTINATION bin BUNDLE DESTINATION .)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/shaders" DESTINATION bin/shaders)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/icons"   DESTINATION bin/icons)
install(FILES "${CMAKE_SOURCE_DIR}/simple_arm.urdf" DESTINATION bin)
