cmake_policy(PUSH)
cmake_policy(SET CMP0012 NEW)
cmake_policy(SET CMP0054 NEW)
cmake_policy(SET CMP0057 NEW)

set(OPENSSL_VERSION_MAJOR 3)
set(OPENSSL_VERSION_MINOR 5)
set(OPENSSL_VERSION_FIX 1)

if(OPENSSL_USE_STATIC_LIBS)
    if("dynamic" STREQUAL "dynamic")
        message(WARNING "OPENSSL_USE_STATIC_LIBS is set, but vcpkg port openssl was built with dynamic linkage")
    endif()
    set(OPENSSL_USE_STATIC_LIBS_BAK "${OPENSSL_USE_STATIC_LIBS}")
    set(OPENSSL_USE_STATIC_LIBS FALSE)
endif()

if(DEFINED OPENSSL_ROOT_DIR)
    set(OPENSSL_ROOT_DIR_BAK "${OPENSSL_ROOT_DIR}")
endif()
get_filename_component(OPENSSL_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)
get_filename_component(OPENSSL_ROOT_DIR "${OPENSSL_ROOT_DIR}" DIRECTORY)
find_path(OPENSSL_INCLUDE_DIR NAMES openssl/ssl.h PATH "${OPENSSL_ROOT_DIR}/include" NO_DEFAULT_PATH)
if(MSVC)
    find_library(LIB_EAY_DEBUG NAMES libcrypto PATHS "${OPENSSL_ROOT_DIR}/debug/lib" NO_DEFAULT_PATH)
    find_library(LIB_EAY_RELEASE NAMES libcrypto PATHS "${OPENSSL_ROOT_DIR}/lib" NO_DEFAULT_PATH)
    find_library(SSL_EAY_DEBUG NAMES libssl PATHS "${OPENSSL_ROOT_DIR}/debug/lib" NO_DEFAULT_PATH)
    find_library(SSL_EAY_RELEASE NAMES libssl PATHS "${OPENSSL_ROOT_DIR}/lib" NO_DEFAULT_PATH)
elseif(WIN32)
    find_library(LIB_EAY NAMES libcrypto crypto NAMES_PER_DIR)
    find_library(SSL_EAY NAMES libssl ssl NAMES_PER_DIR)
else()
    find_library(OPENSSL_CRYPTO_LIBRARY NAMES crypto)
    find_library(OPENSSL_SSL_LIBRARY NAMES ssl)
endif()

_find_package(${ARGS})

unset(OPENSSL_ROOT_DIR)
if(DEFINED OPENSSL_ROOT_DIR_BAK)
    set(OPENSSL_ROOT_DIR "${OPENSSL_ROOT_DIR_BAK}")
    unset(OPENSSL_ROOT_DIR_BAK)
endif()

if(DEFINED OPENSSL_USE_STATIC_LIBS_BAK)
    set(OPENSSL_USE_STATIC_LIBS "${OPENSSL_USE_STATIC_LIBS_BAK}")
    unset(OPENSSL_USE_STATIC_LIBS_BAK)
endif()

if(OPENSSL_FOUND AND "dynamic" STREQUAL "static")
    if(WIN32)
        list(APPEND OPENSSL_LIBRARIES crypt32 ws2_32)
        if(TARGET OpenSSL::Crypto)
            set_property(TARGET OpenSSL::Crypto APPEND PROPERTY INTERFACE_LINK_LIBRARIES "crypt32;ws2_32")
        endif()
        if(TARGET OpenSSL::SSL)
            set_property(TARGET OpenSSL::SSL APPEND PROPERTY INTERFACE_LINK_LIBRARIES "crypt32;ws2_32")
        endif()
    else()
        find_library(OPENSSL_DL_LIBRARY NAMES dl)
        if(OPENSSL_DL_LIBRARY)
            list(APPEND OPENSSL_LIBRARIES "dl")
            if(TARGET OpenSSL::Crypto)
                set_property(TARGET OpenSSL::Crypto APPEND PROPERTY INTERFACE_LINK_LIBRARIES "dl")
            endif()
        endif()

        if("REQUIRED" IN_LIST ARGS)
           find_package(Threads REQUIRED)
        else()
           find_package(Threads)
        endif()
        list(APPEND OPENSSL_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})
        if(TARGET OpenSSL::Crypto)
            set_property(TARGET OpenSSL::Crypto APPEND PROPERTY INTERFACE_LINK_LIBRARIES "Threads::Threads")
        endif()
        if(TARGET OpenSSL::SSL)
            set_property(TARGET OpenSSL::SSL APPEND PROPERTY INTERFACE_LINK_LIBRARIES "Threads::Threads")
        endif()
    endif()
endif()
cmake_policy(POP)
