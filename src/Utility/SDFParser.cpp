#include "SDFParser.hpp"
#include "pugixml.hpp"
#include <stdexcept>

// This is a stub implementation for the SDF parser.
// SDF parsing is complex, often involving multiple <model> tags.
// For now, we'll assume we're parsing the first model in the file.
RobotDescription SDFParser::parse(const std::string& filepath)
{
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(filepath.c_str());

    if (!result) {
        throw std::runtime_error("Failed to load or parse SDF file: " + std::string(result.description()));
    }

    // TODO: Add SDF parsing logic here. It's more complex than URDF
    // as it involves finding <model> tags within a <world> or <sdf> tag.

    RobotDescription robotDesc;
    robotDesc.name = "SDF_Robot_TODO";

    return robotDesc;
}
