# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles\\RoboticsSoftware_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\RoboticsSoftware_autogen.dir\\ParseCache.txt"
  "RoboticsSoftware_autogen"
  )
endif()
