get_filename_component(VCPKG_IMPORT_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../../" ABSOLUTE)
# - Config file for the DBus1 package
# It defines the following variables
#  DBus1_FOUND - Flag for indicating that DBus1 package has been found
#  DBus1_DEFINITIONS  - compile definitions for DBus1 [1]
#  DBus1_INCLUDE_DIRS - include directories for DBus1 [1]
#  DBus1_LIBRARIES    - cmake targets to link against

# [1] This variable is not required if DBus1_LIBRARIES is added
#     to a target with target_link_libraries

# Compute paths
if(ON)
    get_filename_component(DBus1_INSTALL_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
else()
    set(DBus1_INSTALL_DIR "${VCPKG_IMPORT_PREFIX}")
endif()
# Our library dependencies (contains definitions for IMPORTED targets)
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/DBus1Targets.cmake")
    # do not additional search paths for implicit libraries
    # see https://cmake.org/cmake/help/v3.0/policy/CMP0003.html
    if(COMMAND cmake_policy)
        cmake_policy(SET CMP0003 NEW)
    endif(COMMAND cmake_policy)

    if(NOT TARGET dbus-1)
        include("${CMAKE_CURRENT_LIST_DIR}/DBus1Targets.cmake")
    endif()

    # for compatibility, get settings from cmake target
    get_target_property(DBus1_DEFINITIONS dbus-1 INTERFACE_COMPILE_DEFINITIONS)
    get_target_property(DBus1_INCLUDE_DIRS dbus-1 INTERFACE_INCLUDE_DIRECTORIES)
    get_target_property(DBus1_LIBRARY dbus-1 IMPORTED_IMPLIB)
else()
    message(FATAL_ERROR "Incomplete cmake support in DBus1 find_package configuration")
endif()
