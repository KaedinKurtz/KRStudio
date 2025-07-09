get_filename_component(VCPKG_IMPORT_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../../" ABSOLUTE)
if (urdfdom_CONFIG_INCLUDED)
  return()
endif()
set(urdfdom_CONFIG_INCLUDED TRUE)

get_filename_component(_IMPORT_PREFIX "${CMAKE_CURRENT_LIST_FILE}" PATH)
get_filename_component(_IMPORT_PREFIX "${_IMPORT_PREFIX}" PATH)
get_filename_component(_IMPORT_PREFIX "${_IMPORT_PREFIX}" PATH)
 
set(urdfdom_INCLUDE_DIRS "${_IMPORT_PREFIX}/include" "${VCPKG_IMPORT_PREFIX}/include")

if (0)
foreach(lib urdfdom_sensor;urdfdom_model_state;urdfdom_model;urdfdom_world)
  set(onelib "${lib}-NOTFOUND")
  set(onelibd "${lib}-NOTFOUND")
  find_library(onelib ${lib}
    PATHS "${urdfdom_DIR}/../lib"
    NO_DEFAULT_PATH)
  find_library(onelibd ${lib}d
    PATHS "${urdfdom_DIR}/../lib"
    NO_DEFAULT_PATH)
  if(onelib-NOTFOUND AND onelibd-NOTFOUND)
    message(FATAL_ERROR "Library '${lib}' in package urdfdom is not installed properly")
  endif()
  if(onelib AND onelibd)
    list(APPEND urdfdom_LIBRARIES $<$<NOT:$<CONFIG:Debug>>:${onelib}>)
    list(APPEND urdfdom_LIBRARIES $<$<CONFIG:Debug>:${onelibd}>)
  else()
    if(onelib)
      list(APPEND urdfdom_LIBRARIES ${onelib})
    else()
      list(APPEND urdfdom_LIBRARIES ${onelibd})
    endif()
  endif()
  list(APPEND urdfdom_TARGETS urdfdom::${lib})
endforeach()
endif()

include(CMakeFindDependencyMacro)
find_dependency(console_bridge)

foreach(dep urdfdom_headers;console_bridge)
  if(NOT ${dep}_FOUND)
    find_dependency(${dep})
  endif()
  list(APPEND urdfdom_INCLUDE_DIRS ${${dep}_INCLUDE_DIRS})
  list(APPEND urdfdom_LIBRARIES ${${dep}_LIBRARIES})
endforeach()

foreach(exp urdfdom)
  include(${urdfdom_DIR}/${exp}Export.cmake)
endforeach()

set(urdfdom_LIBRARIES urdfdom::urdfdom_model urdfdom::urdfdom_world urdfdom::urdfdom_sensor urdfdom::urdfdom_model_state)
