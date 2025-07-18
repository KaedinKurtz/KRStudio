
#ifndef CONSOLE_BRIDGE_DLLAPI_H
#define CONSOLE_BRIDGE_DLLAPI_H

#if 0
#  define CONSOLE_BRIDGE_DLLAPI
#  define CONSOLE_BRIDGE_NO_EXPORT
#else
#  ifndef CONSOLE_BRIDGE_DLLAPI
#    ifdef console_bridge_EXPORTS
        /* We are building this library */
#      define CONSOLE_BRIDGE_DLLAPI __declspec(dllexport)
#    else
        /* We are using this library */
#      define CONSOLE_BRIDGE_DLLAPI __declspec(dllimport)
#    endif
#  endif

#  ifndef CONSOLE_BRIDGE_NO_EXPORT
#    define CONSOLE_BRIDGE_NO_EXPORT 
#  endif
#endif

#ifndef CONSOLE_BRIDGE_DEPRECATED
#  define CONSOLE_BRIDGE_DEPRECATED __declspec(deprecated)
#endif

#ifndef CONSOLE_BRIDGE_DEPRECATED_EXPORT
#  define CONSOLE_BRIDGE_DEPRECATED_EXPORT CONSOLE_BRIDGE_DLLAPI CONSOLE_BRIDGE_DEPRECATED
#endif

#ifndef CONSOLE_BRIDGE_DEPRECATED_NO_EXPORT
#  define CONSOLE_BRIDGE_DEPRECATED_NO_EXPORT CONSOLE_BRIDGE_NO_EXPORT CONSOLE_BRIDGE_DEPRECATED
#endif

/* NOLINTNEXTLINE(readability-avoid-unconditional-preprocessor-if) */
#if 0 /* DEFINE_NO_DEPRECATED */
#  ifndef CONSOLE_BRIDGE_NO_DEPRECATED
#    define CONSOLE_BRIDGE_NO_DEPRECATED
#  endif
#endif

#endif /* CONSOLE_BRIDGE_DLLAPI_H */
