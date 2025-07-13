# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "D:/RoboticsSoftware/build/_deps/eigen-src")
  file(MAKE_DIRECTORY "D:/RoboticsSoftware/build/_deps/eigen-src")
endif()
file(MAKE_DIRECTORY
  "D:/RoboticsSoftware/build/_deps/eigen-build"
  "D:/RoboticsSoftware/build/_deps/eigen-subbuild/eigen-populate-prefix"
  "D:/RoboticsSoftware/build/_deps/eigen-subbuild/eigen-populate-prefix/tmp"
  "D:/RoboticsSoftware/build/_deps/eigen-subbuild/eigen-populate-prefix/src/eigen-populate-stamp"
  "D:/RoboticsSoftware/build/_deps/eigen-subbuild/eigen-populate-prefix/src"
  "D:/RoboticsSoftware/build/_deps/eigen-subbuild/eigen-populate-prefix/src/eigen-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/RoboticsSoftware/build/_deps/eigen-subbuild/eigen-populate-prefix/src/eigen-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/RoboticsSoftware/build/_deps/eigen-subbuild/eigen-populate-prefix/src/eigen-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
