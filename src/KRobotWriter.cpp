#include "KRobotWriter.hpp"
#include "pugixml.hpp"

// This is a placeholder implementation. A full implementation would be very large,
// serializing every single field from your structs into XML attributes and nodes.
// This demonstrates the basic structure.

bool KRobotWriter::save(const RobotDescription& description, const std::string& filepath)
{
    pugi::xml_document doc;
    pugi::xml_node robotNode = doc.append_child("krobot");

    robotNode.append_attribute("name") = description.name.c_str();

    // --- Serialize Links ---
    pugi::xml_node linksNode = robotNode.append_child("links");
    for (const auto& link : description.links)
    {
        pugi::xml_node linkNode = linksNode.append_child("link");
        linkNode.append_attribute("name") = link.name.c_str();
        linkNode.append_attribute("mesh") = link.mesh_filepath.c_str();
        // ... here you would serialize ALL other link properties ...
        // e.g., linkNode.append_attribute("mass") = link.mass;
    }

    // --- Serialize Joints ---
    pugi::xml_node jointsNode = robotNode.append_child("joints");
    for (const auto& joint : description.joints)
    {
        pugi::xml_node jointNode = jointsNode.append_child("joint");
        jointNode.append_attribute("name") = joint.name.c_str();
        // ... here you would serialize ALL other joint properties ...
    }

    return doc.save_file(filepath.c_str());
}
