#include "KRobotParser.hpp"
#include "pugixml.hpp"
#include <stdexcept>

// This is a stub implementation. You will expand this to read
// every single attribute from your custom .krobot XML file.
RobotDescription KRobotParser::parse(const std::string& filepath)
{
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(filepath.c_str());

    if (!result) {
        throw std::runtime_error("Failed to load or parse .krobot file: " + std::string(result.description()));
    }

    pugi::xml_node robotNode = doc.child("krobot");
    if (!robotNode) {
        throw std::runtime_error(".krobot does not contain a <krobot> element.");
    }

    RobotDescription robotDesc;
    robotDesc.name = robotNode.attribute("name").as_string("Unnamed KRobot");

    // TODO: Add comprehensive parsing logic here for links, joints, materials,
    // sensors, PID values, etc., reading from the rich XML structure.

    return robotDesc;
}
