#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::QModernWindowsStylePlugin" for configuration "Debug"
set_property(TARGET Qt6::QModernWindowsStylePlugin APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::QModernWindowsStylePlugin PROPERTIES
  IMPORTED_COMMON_LANGUAGE_RUNTIME_DEBUG ""
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/debug/Qt6/plugins/styles/qmodernwindowsstyled.dll"
  )

list(APPEND _cmake_import_check_targets Qt6::QModernWindowsStylePlugin )
list(APPEND _cmake_import_check_files_for_Qt6::QModernWindowsStylePlugin "${_IMPORT_PREFIX}/debug/Qt6/plugins/styles/qmodernwindowsstyled.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
