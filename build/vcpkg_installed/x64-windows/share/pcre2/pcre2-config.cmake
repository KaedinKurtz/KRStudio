# pcre2-config.cmake
# ----------------
#
# Finds the PCRE2 library, specify the starting search path in PCRE2_ROOT.
#
# Static vs. shared
# -----------------
# To force using the static library instead of the shared one, one needs
# to set the variable PCRE2_USE_STATIC_LIBS to ON before calling find_package.
# If the variable is not set, the static library will be used if only that has
# been built, otherwise the shared library will be used.
#
# The following components are supported: 8BIT, 16BIT, 32BIT and POSIX.
# They used to be required but not anymore; all available targets will
# be defined regardless of the requested components.
# Example:
#   set(PCRE2_USE_STATIC_LIBS ON)
#   find_package(PCRE2 CONFIG)
#
# This will define the following variables:
#
#   PCRE2_FOUND   - True if the system has the PCRE2 library.
#   PCRE2_VERSION - The version of the PCRE2 library which was found.
#
# and the following imported targets:
#
#   PCRE2::8BIT  - The 8 bit PCRE2 library.
#   PCRE2::16BIT - The 16 bit PCRE2 library.
#   PCRE2::32BIT - The 32 bit PCRE2 library.
#   PCRE2::POSIX - The POSIX PCRE2 library.


####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was pcre2-config.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

include(CMakeFindDependencyMacro)
if("") # REQUIRE_PTHREAD
  find_dependency(Threads)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/pcre2-targets.cmake")

# Set version
set(PCRE2_VERSION "10.45.0")

# Chooses the linkage of the library to expose in the
# unsuffixed edition of the target.
macro(_pcre2_add_component_target component target)
  # If the static library exists and either PCRE2_USE_STATIC_LIBS
  # is defined, or the dynamic library does not exist, use the static library.
  if(NOT TARGET PCRE2::${component})
    if(TARGET pcre2::pcre2-${target}-static AND (PCRE2_USE_STATIC_LIBS OR NOT TARGET pcre2::pcre2-${target}-shared))
      add_library(PCRE2::${component} ALIAS pcre2::pcre2-${target}-static)
      set(PCRE2_${component}_FOUND TRUE)
    # Otherwise use the dynamic library if it exists.
    elseif(TARGET pcre2::pcre2-${target}-shared AND NOT PCRE2_USE_STATIC_LIBS)
      add_library(PCRE2::${component} ALIAS pcre2::pcre2-${target}-shared)
      set(PCRE2_${component}_FOUND TRUE)
    endif()
    if(PCRE2_${component}_FOUND)
      get_target_property(PCRE2_${component}_LIBRARY PCRE2::${component} IMPORTED_LOCATION)
      set(PCRE2_LIBRARIES ${PCRE2_LIBRARIES} ${PCRE2_${component}_LIBRARY})
    endif()
  endif()
endmacro()
_pcre2_add_component_target(8BIT 8)
_pcre2_add_component_target(16BIT 16)
_pcre2_add_component_target(32BIT 32)
_pcre2_add_component_target(POSIX posix)

# When POSIX component has been specified make sure that also 8BIT component is specified.
set(PCRE2_8BIT_COMPONENT FALSE)
set(PCRE2_POSIX_COMPONENT FALSE)
foreach(component ${PCRE2_FIND_COMPONENTS})
  if(component STREQUAL "8BIT")
    set(PCRE2_8BIT_COMPONENT TRUE)
  elseif(component STREQUAL "POSIX")
    set(PCRE2_POSIX_COMPONENT TRUE)
  endif()
endforeach()

if(PCRE2_POSIX_COMPONENT AND NOT PCRE2_8BIT_COMPONENT)
  message(
    FATAL_ERROR
    "The component POSIX is specified while the 8BIT one is not. This is not allowed. Please, also specify the 8BIT component."
  )
endif()
unset(PCRE2_8BIT_COMPONENT)
unset(PCRE2_POSIX_COMPONENT)

# Check for required components.
check_required_components("PCRE2")
