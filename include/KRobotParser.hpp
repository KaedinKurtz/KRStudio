#pragma once

#include "RobotDescription.hpp"
#include <string>

// A static utility class for parsing your native .krobot files.
class KRobotParser
{
public:
    // Parses the .krobot file at the given path.
    // This will be the most comprehensive parser, filling ALL fields.
    // Throws a std::runtime_error if parsing fails.
    static RobotDescription parse(const std::string& filepath);
};
