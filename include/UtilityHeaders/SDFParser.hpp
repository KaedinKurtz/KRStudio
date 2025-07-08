#pragma once

#include "RobotDescription.hpp"
#include <string>

// A static utility class for parsing SDF files.
class SDFParser
{
public:
    // Parses the SDF file at the given path.
    // SDF is more complex than URDF, but will still result in one RobotDescription.
    // Throws a std::runtime_error if parsing fails.
    static RobotDescription parse(const std::string& filepath);
};
