#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "unofficial-tinyxml::unofficial-tinyxml" for configuration "Release"
set_property(TARGET unofficial-tinyxml::unofficial-tinyxml APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(unofficial-tinyxml::unofficial-tinyxml PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/tinyxml.lib"
  )

list(APPEND _cmake_import_check_targets unofficial-tinyxml::unofficial-tinyxml )
list(APPEND _cmake_import_check_files_for_unofficial-tinyxml::unofficial-tinyxml "${_IMPORT_PREFIX}/lib/tinyxml.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
