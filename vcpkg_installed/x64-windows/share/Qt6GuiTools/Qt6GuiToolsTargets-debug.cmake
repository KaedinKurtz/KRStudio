#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::qvkgen" for configuration "Debug"
set_property(TARGET Qt6::qvkgen APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::qvkgen PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/tools/Qt6/bin/qvkgen.exe"
  )

list(APPEND _cmake_import_check_targets Qt6::qvkgen )
list(APPEND _cmake_import_check_files_for_Qt6::qvkgen "${_IMPORT_PREFIX}/tools/Qt6/bin/qvkgen.exe" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
