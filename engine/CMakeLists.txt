# KRS Graphics — standalone library build. This project must configure, build,
# install, and test WITHOUT the KRStudio app or its vcpkg dependency set
# (ADR-003): third-party needs are Vulkan headers+loader only (system/SDK for
# now; volk+VMA arrive vendored in WP1).
cmake_minimum_required(VERSION 3.24)
project(KRSGraphics VERSION 0.1.0 LANGUAGES CXX)

option(KRSG_ENABLE_VULKAN "Build the Vulkan backend (requires Vulkan headers + loader)" ON)
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
  find_package(Vulkan REQUIRED)
  target_sources(krsg PRIVATE src/rhi/vulkan/vulkan_backend.cpp)
  target_link_libraries(krsg PRIVATE Vulkan::Vulkan)
  target_compile_definitions(krsg PRIVATE KRSG_HAS_VULKAN=1)
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

  add_executable(krsg_unit_tests tests/unit/test_device_null.cpp)
  target_link_libraries(krsg_unit_tests PRIVATE krsg)
  target_include_directories(krsg_unit_tests PRIVATE tests/unit)
  add_test(NAME unit COMMAND krsg_unit_tests)

  if(KRSG_ENABLE_VULKAN)
    # Built everywhere the backend builds; EXECUTED only where an ICD exists
    # (CI stage S4/S5 runs it explicitly; it is not part of the default ctest
    # set so S3 stays honest on GPU-less machines).
    add_executable(krsg_gpu_tests tests/gpu/test_vulkan_device.cpp)
    target_link_libraries(krsg_gpu_tests PRIVATE krsg)
  endif()
endif()
