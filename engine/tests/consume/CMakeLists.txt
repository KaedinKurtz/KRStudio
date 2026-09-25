# Packaging contract test (CI stage S2 step 3): a standalone consumer that knows
# NOTHING about the engine source tree — it builds against the INSTALLED
# KRSGraphics package only. If a public header leaks an internal include or an
# install rule misses a file, this breaks instead of a downstream user.
cmake_minimum_required(VERSION 3.24)
project(KrsgConsumeTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(KRSGraphics CONFIG REQUIRED)

add_executable(krsg_consume main.cpp)
target_link_libraries(krsg_consume PRIVATE krsg::krsg)
