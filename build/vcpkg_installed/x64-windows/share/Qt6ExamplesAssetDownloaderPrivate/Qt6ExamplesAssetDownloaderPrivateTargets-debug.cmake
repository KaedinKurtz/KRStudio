#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Qt6::ExamplesAssetDownloaderPrivate" for configuration "Debug"
set_property(TARGET Qt6::ExamplesAssetDownloaderPrivate APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(Qt6::ExamplesAssetDownloaderPrivate PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/debug/lib/Qt6ExamplesAssetDownloaderd.lib"
  )

list(APPEND _cmake_import_check_targets Qt6::ExamplesAssetDownloaderPrivate )
list(APPEND _cmake_import_check_files_for_Qt6::ExamplesAssetDownloaderPrivate "${_IMPORT_PREFIX}/debug/lib/Qt6ExamplesAssetDownloaderd.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
