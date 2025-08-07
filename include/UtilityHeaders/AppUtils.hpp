#pragma once // Prevents this header from being included multiple times in one file.

#include <QCoreApplication>
#include <QDir>
#include <QString>

// For the low-level earlyLog function
#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#endif
#include <cstdio>
#include <string>

// A namespace to hold all general-purpose application utility functions.
namespace AppUtils {

    /**
     * @brief Gets the absolute path to the application's data directory.
     * * This function provides a robust way to locate your assets (shaders, models, textures)
     * by assuming they are in a "data" folder located next to your application's executable.
     * This makes your application portable, as it doesn't rely on hardcoded absolute paths.
     *
     * @return A QString containing the full, absolute path to the 'data' directory.
     * * @example
     * QString shaderPath = AppUtils::dataDir() + "/shaders/gbuffer.glsl";
     * QString modelPath = AppUtils::dataDir() + "/models/dragon.stl";
     */
    inline QString dataDir()
    {
        // Get the directory where the application executable is located.
        QDir exeDir(QCoreApplication::applicationDirPath());

        // Resolve the path to a "data" folder assumed to be a sibling of the executable.
        // Using absoluteFilePath is safer than manual string concatenation.
        return exeDir.absoluteFilePath("data");
    }

    /**
     * @brief A low-level logging function for early startup or critical errors.
     *
     * This function is useful for printing debug messages when the main application loop
     * or the standard qDebug stream may not be initialized yet. On Windows, it prints to the
     * IDE's debug output window. On all platforms, it prints to the standard error stream.
     *
     * @param msg The C-style string message to log.
     */
    inline void earlyLog(const char* msg)
    {
#ifdef _WIN32
        // On Windows, OutputDebugStringA sends the string directly to the debugger.
        ::OutputDebugStringA(msg);
        ::OutputDebugStringA("\n");
#endif
        // On all platforms, print to the standard error console stream.
        std::fprintf(stderr, "%s\n", msg);
        std::fflush(stderr); // Ensure the message is written immediately.
    }

    // Convenience overload for C++ strings.
    inline void earlyLog(const std::string& msg)
    {
        earlyLog(msg.c_str());
    }

} // namespace AppUtils