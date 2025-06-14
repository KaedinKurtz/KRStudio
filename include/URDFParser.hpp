#pragma once

#include "RobotDescription.hpp"
#include <string>

// A static utility class for parsing URDF files.
// It has no member variables and doesn't need to be instantiated.
class URDFParser
{
public:
    // Parses the URDF file at the given path.
    // Throws a std::runtime_error if parsing fails.
    // Returns a populated RobotDescription struct on success.
    static RobotDescription parse(const std::string& filepath);
};
