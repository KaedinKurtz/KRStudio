include(FindPackageHandleStandardArgs)
include(SelectLibraryConfigurations)

find_path(MOSQUITTO_INCLUDE_DIR mosquitto.h)

# OVERLAY FIX (KRStudio): upstream marks the DEBUG find_library calls REQUIRED,
# which hard-errors on release-only triplets (VCPKG_BUILD_TYPE release builds no
# debug/lib at all — seen on ci-mac run 136 and applicable to x64-linux-ci).
# Debug lookups are optional here; select_library_configurations falls back to
# the release library when no debug variant exists.
find_library(MOSQUITTO_LIBRARY_DEBUG NAMES mosquitto libmosquitto mosquitto_static libmosquitto_static NAMES_PER_DIR PATH_SUFFIXES lib PATHS "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug" NO_DEFAULT_PATH)
find_library(MOSQUITTO_LIBRARY_RELEASE NAMES mosquitto libmosquitto mosquitto_static libmosquitto_static NAMES_PER_DIR PATH_SUFFIXES lib PATHS "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}" NO_DEFAULT_PATH REQUIRED)
find_library(MOSQUITTOPP_LIBRARY_DEBUG NAMES mosquittopp libmosquittopp mosquittopp_static libmosquittopp_static NAMES_PER_DIR PATH_SUFFIXES lib PATHS "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug" NO_DEFAULT_PATH)
find_library(MOSQUITTOPP_LIBRARY_RELEASE NAMES mosquittopp libmosquittopp mosquittopp_static libmosquittopp_static NAMES_PER_DIR PATH_SUFFIXES lib PATHS "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}" NO_DEFAULT_PATH REQUIRED)

select_library_configurations(MOSQUITTO)
select_library_configurations(MOSQUITTOPP)

set(MOSQUITTO_INCLUDE_DIRS ${MOSQUITTO_INCLUDE_DIR})
set(MOSQUITTO_LIBRARIES ${MOSQUITTO_LIBRARY} ${MOSQUITTOPP_LIBRARY})
