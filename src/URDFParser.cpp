#include "URDFParser.hpp"
#include "pugixml.hpp"
#include <stdexcept>
#include <iostream>
#include <sstream>

// Helper function to parse a "x y z" string into a glm::vec3
static glm::vec3 parseVec3(const char* str)
{
    if (!str) return glm::vec3(0.0f);
    glm::vec3 result;
    std::stringstream ss(str);
    ss >> result.x >> result.y >> result.z;
    return result;
}

RobotDescription URDFParser::parse(const std::string& filepath)
{
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(filepath.c_str());

    if (!result)
    {
        throw std::runtime_error("Failed to load or parse URDF file: " + std::string(result.description()));
    }

    pugi::xml_node robotNode = doc.child("robot");
    if (!robotNode)
    {
        throw std::runtime_error("URDF does not contain a <robot> element.");
    }

    RobotDescription robotDesc;
    robotDesc.name = robotNode.attribute("name").as_string("DefaultRobotName");

    // --- Parse Links ---
    for (pugi::xml_node linkNode : robotNode.children("link"))
    {
        LinkDescription linkDesc;
        linkDesc.name = linkNode.attribute("name").as_string();
        if (linkDesc.name.empty()) continue; // Skip links without names

        // NOTE: This is a simplified parser. We are only parsing the visual mesh and name.
        // The user will enrich the rest of the data in the URDFImporterDialog.
        if (pugi::xml_node visualNode = linkNode.child("visual"))
        {
            if (pugi::xml_node originNode = visualNode.child("origin")) {
                linkDesc.visual_origin_xyz = parseVec3(originNode.attribute("xyz").as_string());
                linkDesc.visual_origin_rpy = parseVec3(originNode.attribute("rpy").as_string());
            }
            if (pugi::xml_node geometryNode = visualNode.child("geometry")) {
                if (pugi::xml_node meshNode = geometryNode.child("mesh")) {
                    linkDesc.mesh_filepath = meshNode.attribute("filename").as_string();
                }
            }
        }

        // Assign a default material, which the user can edit.
        linkDesc.material = MaterialDescription();

        robotDesc.links.push_back(linkDesc);
    }

    // --- Parse Joints ---
    for (pugi::xml_node jointNode : robotNode.children("joint"))
    {
        JointDescription jointDesc;
        jointDesc.name = jointNode.attribute("name").as_string();
        if (jointDesc.name.empty()) continue; // Skip joints without names

        const char* typeStr = jointNode.attribute("type").as_string();
        if (strcmp(typeStr, "revolute") == 0) jointDesc.type = JointType::REVOLUTE;
        else if (strcmp(typeStr, "continuous") == 0) jointDesc.type = JointType::CONTINUOUS;
        else if (strcmp(typeStr, "prismatic") == 0) jointDesc.type = JointType::PRISMATIC;
        else if (strcmp(typeStr, "fixed") == 0) jointDesc.type = JointType::FIXED;
        else if (strcmp(typeStr, "floating") == 0) jointDesc.type = JointType::FLOATING;
        else if (strcmp(typeStr, "planar") == 0) jointDesc.type = JointType::PLANAR;

        if (pugi::xml_node parentNode = jointNode.child("parent")) {
            jointDesc.parent_link_name = parentNode.attribute("link").as_string();
        }
        if (pugi::xml_node childNode = jointNode.child("child")) {
            jointDesc.child_link_name = childNode.attribute("link").as_string();
        }
        if (pugi::xml_node originNode = jointNode.child("origin")) {
            jointDesc.origin_xyz = parseVec3(originNode.attribute("xyz").as_string());
            jointDesc.origin_rpy = parseVec3(originNode.attribute("rpy").as_string());
        }
        if (pugi::xml_node axisNode = jointNode.child("axis")) {
            jointDesc.axis = parseVec3(axisNode.attribute("xyz").as_string());
        }
        if (pugi::xml_node limitNode = jointNode.child("limit")) {
            jointDesc.limits.lower = limitNode.attribute("lower").as_double();
            jointDesc.limits.upper = limitNode.attribute("upper").as_double();
            jointDesc.limits.effort_limit = limitNode.attribute("effort").as_double();
            jointDesc.limits.velocity_limit = limitNode.attribute("velocity").as_double();
        }

        robotDesc.joints.push_back(jointDesc);
    }

    return robotDesc;
}
