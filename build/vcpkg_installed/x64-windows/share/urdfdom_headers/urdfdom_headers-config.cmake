if (urdfdom_headers_CONFIG_INCLUDED)
  return()
endif()
set(urdfdom_headers_CONFIG_INCLUDED TRUE)

set(urdfdom_headers_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../include")

include("${CMAKE_CURRENT_LIST_DIR}/urdfdom_headersExport.cmake")

list(APPEND urdfdom_headers_TARGETS urdfdom_headers::urdfdom_headers)
