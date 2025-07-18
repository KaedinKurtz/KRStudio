
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was assimp-plain-config.cmake.in                            ########

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

if(NOT "ON")
    find_dependency(zip CONFIG)
    find_dependency(unofficial-minizip CONFIG)
    find_dependency(pugixml CONFIG)
    find_dependency(poly2tri CONFIG)
    find_dependency(polyclipping CONFIG)
    find_dependency(RapidJSON CONFIG)
    find_dependency(Stb MODULE)
    find_dependency(utf8cpp CONFIG)
    find_dependency(ZLIB)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/assimpTargets.cmake")

set(ASSIMP_ROOT_DIR ${PACKAGE_PREFIX_DIR})
set(ASSIMP_LIBRARIES assimp::assimp)
set(ASSIMP_BUILD_SHARED_LIBS ON)
get_property(ASSIMP_INCLUDE_DIRS TARGET assimp::assimp PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
set(ASSIMP_LIBRARY_DIRS "")
