#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "console_bridge::console_bridge" for configuration "Debug"
set_property(TARGET console_bridge::console_bridge APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(console_bridge::console_bridge PROPERTIES
  IMPORTED_IMPLIB_DEBUG "${_IMPORT_PREFIX}/debug/lib/console_bridge.lib"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/debug/bin/console_bridge.dll"
  )

list(APPEND _cmake_import_check_targets console_bridge::console_bridge )
list(APPEND _cmake_import_check_files_for_console_bridge::console_bridge "${_IMPORT_PREFIX}/debug/lib/console_bridge.lib" "${_IMPORT_PREFIX}/debug/bin/console_bridge.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
