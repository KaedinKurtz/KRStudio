# KRS Graphics — standalone library build. This project must configure, build,
# install, and test WITHOUT the KRStudio app or its vcpkg dependency set.
# Third-party policy (ADR-003): the ONLY dependencies are vendored, pinned
# FetchContent checkouts — Vulkan-Headers, volk (runtime loader, no SDK link),
# and VulkanMemoryAllocator — compiled INTO the library so the installed
# package exports no foreign targets and consumers need nothing but CMake.
cmake_minimum_required(VERSION 3.24)
project(KRSGraphics VERSION 0.2.0 LANGUAGES CXX)

option(KRSG_ENABLE_VULKAN "Build the Vulkan backend (vendored headers/loader; no SDK needed)" ON)
option(KRSG_WERROR "Treat warnings as errors (always ON in CI)" OFF)
option(KRSG_BUILD_TESTS "Build unit/gpu tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(krsg STATIC
  src/core/device.cpp
  src/core/version.cpp
  src/null/null_backend.cpp
)
add_library(krsg::krsg ALIAS krsg)

target_include_directories(krsg
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(KRSG_ENABLE_VULKAN)
  include(FetchContent)
  # Pinned vendored dependencies (ADR-003). Update ritual: bump all three to a
  # matching Vulkan SDK release in ONE commit, with the WP2 capability table
  # re-checked. GIT_SHALLOW keeps CI clones to seconds.
  FetchContent_Declare(krsg_vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        v1.3.290
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _no_cmake_ # populate only; we consume include/ directly
  )
  FetchContent_Declare(krsg_volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        vulkan-sdk-1.3.290.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _no_cmake_
  )
  FetchContent_Declare(krsg_vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.1.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _no_cmake_
  )
  FetchContent_MakeAvailable(krsg_vulkan_headers krsg_volk krsg_vma)

  target_sources(krsg PRIVATE
    src/rhi/vulkan/vulkan_backend.cpp
    src/rhi/vulkan/vk_resources.cpp
    src/rhi/vulkan/vk_compute.cpp
    src/rhi/vulkan/vk_vma.cpp
    ${krsg_volk_SOURCE_DIR}/volk.c
  )
  # volk.c is C; the project only enables CXX — compile it as C++ (volk supports
  # this) to keep the toolchain surface single-language.
  set_source_files_properties(${krsg_volk_SOURCE_DIR}/volk.c PROPERTIES LANGUAGE CXX)

  # SYSTEM silences third-party headers under our -Werror wall.
  target_include_directories(krsg SYSTEM PRIVATE
    ${krsg_vulkan_headers_SOURCE_DIR}/include
    ${krsg_volk_SOURCE_DIR}
    ${krsg_vma_SOURCE_DIR}/include
  )
  target_compile_definitions(krsg PRIVATE
    KRSG_HAS_VULKAN=1
    VK_NO_PROTOTYPES              # all entry points come through volk
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1
  )
  if(UNIX AND NOT APPLE)
    target_link_libraries(krsg PRIVATE ${CMAKE_DL_LIBS}) # volk dlopens the loader
  endif()
endif()

# Engine code is warning-clean by contract (CI_PIPELINE.md S2); the app tree is not
# held to this yet, which is why the flags live here and not at the repo root.
if(MSVC)
  target_compile_options(krsg PRIVATE /W4 $<$<BOOL:${KRSG_WERROR}>:/WX>)
else()
  target_compile_options(krsg PRIVATE -Wall -Wextra $<$<BOOL:${KRSG_WERROR}>:-Werror>)
endif()

# ---- Install & package export (the boundary contract, tested by tests/consume) ----
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS krsg EXPORT KRSGraphicsTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT KRSGraphicsTargets
  NAMESPACE krsg::
  DESTINATION ${CMAKE_INSTALL_DATADIR}/KRSGraphics
)

configure_package_config_file(
  cmake/KRSGraphicsConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/KRSGraphicsConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_DATADIR}/KRSGraphics
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/KRSGraphicsConfigVersion.cmake
  COMPATIBILITY SameMajorVersion
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/KRSGraphicsConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/KRSGraphicsConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_DATADIR}/KRSGraphics
)

# ---- Tests ----
if(KRSG_BUILD_TESTS)
  enable_testing()

  add_executable(krsg_unit_tests
    tests/unit/test_device_null.cpp
    tests/unit/test_resources_null.cpp
  )
  target_link_libraries(krsg_unit_tests PRIVATE krsg)
  target_include_directories(krsg_unit_tests PRIVATE tests/unit)
  add_test(NAME unit COMMAND krsg_unit_tests)

  if(KRSG_ENABLE_VULKAN)
    # Built everywhere the backend builds; EXECUTED only where an ICD exists
    # (CI stage S4/S5 runs it explicitly; it is not part of the default ctest
    # set so S3 stays honest on GPU-less machines).
    add_executable(krsg_gpu_tests tests/gpu/test_vulkan_device.cpp)
    target_link_libraries(krsg_gpu_tests PRIVATE krsg)
    target_include_directories(krsg_gpu_tests PRIVATE tests/gpu)
  endif()
endif()
