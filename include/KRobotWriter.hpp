#pragma once

#include "RobotDescription.hpp"
#include <string>

// A static utility class for writing .krobot files.
class KRobotWriter
{
public:
    // Saves the provided RobotDescription to a .krobot file at the given path.
    // Returns true on success, false on failure.
    static bool save(const RobotDescription& description, const std::string& filepath);
};
