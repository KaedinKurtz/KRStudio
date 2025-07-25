#include "InteractionNodes.hpp"
#include <iostream> 
#include <memory>   // Required for std::make_unique

namespace NodeLibrary {

    // --- Free Function Implementations (Placeholders) ---
    // (Implementations remain the same)
    void drawDebugSphere(const glm::vec3& center, float radius, const glm::vec4& color, float duration_s) {
        std::cout << "DEBUG_DRAW: Sphere at (" << center.x << ", " << center.y << ", " << center.z << ") with radius " << radius << " for " << duration_s << "s.\n";
    }
    void drawDebugLine(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color, float duration_s) {
        std::cout << "DEBUG_DRAW: Line from (" << start.x << ", " << start.y << ", " << start.z << ") to (" << end.x << ", " << end.y << ", " << end.z << ") for " << duration_s << "s.\n";
    }
    RaycastHit getMouseRaycast(const Camera& camera, float mouse_x, float mouse_y) {
        std::cout << "INPUT: Performing raycast from mouse coordinates (" << mouse_x << ", " << mouse_y << ").\n";
        RaycastHit hit;
        hit.has_hit = true;
        hit.distance = 5.0f;
        // In a real implementation, you would calculate this based on camera projection
        // hit.world_point = camera.position + camera.forward * hit.distance; 
        return hit;
    }
    bool isKeyPressed(int key_code) {
        // std::cout << "INPUT: Checking if key " << key_code << " is pressed.\n";
        return false;
    }
    void printToConsole(const std::string& message) {
        std::cout << "CONSOLE_PRINT: " << message << std::endl;
    }

    // --- Node Implementations & Registrations ---

    // DrawDebugSphereNode
    DrawDebugSphereNode::DrawDebugSphereNode() {
	m_id = "interaction_draw_sphere";
        // FIX: Use nested initializer {name, unit} for DataType
        m_ports.push_back({ "Center", {"glm::vec3", "meters"}, Port::Direction::Input, this });
        m_ports.push_back({ "Radius", {"float", "meters"}, Port::Direction::Input, this });
        m_ports.push_back({ "Color", {"glm::vec4", "rgba"}, Port::Direction::Input, this });
        m_ports.push_back({ "Duration", {"float", "seconds"}, Port::Direction::Input, this });
    }

    void DrawDebugSphereNode::compute() {
        auto center = getInput<glm::vec3>("Center");
        auto radius = getInput<float>("Radius");
        auto color = getInput<glm::vec4>("Color");
        auto duration = getInput<float>("Duration");

        if (center && radius && color && duration) {
            drawDebugSphere(*center, *radius, *color, *duration);
        }
    }

    namespace {
        struct DrawDebugSphereRegistrar {
            DrawDebugSphereRegistrar() {
                NodeDescriptor desc = { "Draw Debug Sphere", "Interaction/Debug", "Draws a sphere in the world for debugging." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("interaction_draw_sphere", desc, []() { return std::make_unique<DrawDebugSphereNode>(); });
            }
        };
    }
    static DrawDebugSphereRegistrar g_drawDebugSphereRegistrar;

    // DrawDebugLineNode
    DrawDebugLineNode::DrawDebugLineNode() {
	m_id = "interaction_draw_line";
        // FIX: Use nested initializer {name, unit} for DataType
        m_ports.push_back({ "Start", {"glm::vec3", "meters"}, Port::Direction::Input, this });
        m_ports.push_back({ "End", {"glm::vec3", "meters"}, Port::Direction::Input, this });
        m_ports.push_back({ "Color", {"glm::vec4", "rgba"}, Port::Direction::Input, this });
        m_ports.push_back({ "Duration", {"float", "seconds"}, Port::Direction::Input, this });
    }

    void DrawDebugLineNode::compute() {
        auto start = getInput<glm::vec3>("Start");
        auto end = getInput<glm::vec3>("End");
        auto color = getInput<glm::vec4>("Color");
        auto duration = getInput<float>("Duration");

        if (start && end && color && duration) {
            drawDebugLine(*start, *end, *color, *duration);
        }
    }

    namespace {
        struct DrawDebugLineRegistrar {
            DrawDebugLineRegistrar() {
                NodeDescriptor desc = { "Draw Debug Line", "Interaction/Debug", "Draws a line in the world for debugging." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("interaction_draw_line", desc, []() { return std::make_unique<DrawDebugLineNode>(); });
            }
        };
    }
    static DrawDebugLineRegistrar g_drawDebugLineRegistrar;

    // GetMouseRaycastNode
    GetMouseRaycastNode::GetMouseRaycastNode() {
	m_id = "interaction_raycast";
        // FIX: Use nested initializer {name, unit} for DataType
        m_ports.push_back({ "Camera", {"Camera", "handle"}, Port::Direction::Input, this });
        m_ports.push_back({ "Mouse X", {"float", "pixels"}, Port::Direction::Input, this });
        m_ports.push_back({ "Mouse Y", {"float", "pixels"}, Port::Direction::Input, this });
        m_ports.push_back({ "Hit Info", {"RaycastHit", "data"}, Port::Direction::Output, this });
    }

    void GetMouseRaycastNode::compute() {
        auto camera = getInput<Camera>("Camera");
        auto mouseX = getInput<float>("Mouse X");
        auto mouseY = getInput<float>("Mouse Y");

        if (camera && mouseX && mouseY) {
            setOutput("Hit Info", getMouseRaycast(*camera, *mouseX, *mouseY));
        }
    }

    namespace {
        struct GetMouseRaycastRegistrar {
            GetMouseRaycastRegistrar() {
                NodeDescriptor desc = { "Get Mouse Raycast", "Interaction/Input", "Casts a ray from the mouse into the scene." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("interaction_raycast", desc, []() { return std::make_unique<GetMouseRaycastNode>(); });
            }
        };
    }
    static GetMouseRaycastRegistrar g_getMouseRaycastRegistrar;

    // IsKeyPressedNode
    IsKeyPressedNode::IsKeyPressedNode() {
	m_id = "interaction_is_key_pressed";
        // FIX: Use nested initializer {name, unit} for DataType
        m_ports.push_back({ "Pressed", {"bool", "boolean"}, Port::Direction::Output, this });
    }

    void IsKeyPressedNode::compute() {
        // The key_code member would be set by the IDE's property editor for this node instance.
        setOutput("Pressed", isKeyPressed(this->key_code));
    }

    namespace {
        struct IsKeyPressedRegistrar {
            IsKeyPressedRegistrar() {
                NodeDescriptor desc = { "Is Key Pressed", "Interaction/Input", "Checks if a specific keyboard key is currently pressed." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("interaction_is_key_pressed", desc, []() { return std::make_unique<IsKeyPressedNode>(); });
            }
        };
    }
    static IsKeyPressedRegistrar g_isKeyPressedRegistrar;

    // PrintToConsoleNode
    PrintToConsoleNode::PrintToConsoleNode() {
	m_id = "interaction_print";
        // FIX: Use nested initializer {name, unit} for DataType
        m_ports.push_back({ "Message", {"std::string", "text"}, Port::Direction::Input, this });
    }

    void PrintToConsoleNode::compute() {
        auto message = getInput<std::string>("Message");
        if (message) {
            printToConsole(*message);
        }
    }

    namespace {
        struct PrintToConsoleRegistrar {
            PrintToConsoleRegistrar() {
                NodeDescriptor desc = { "Print to Console", "Interaction/Output", "Prints a string message to the console log." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("interaction_print", desc, []() { return std::make_unique<PrintToConsoleNode>(); });
            }
        };
    }
    static PrintToConsoleRegistrar g_printToConsoleRegistrar;



QWidget* PrintToConsoleNode::createCustomWidget()
{
    // TODO: Implement custom widget for "PrintToConsoleNode"
    return nullptr;
}


QWidget* IsKeyPressedNode::createCustomWidget()
{
    // TODO: Implement custom widget for "IsKeyPressedNode"
    return nullptr;
}


QWidget* GetMouseRaycastNode::createCustomWidget()
{
    // TODO: Implement custom widget for "GetMouseRaycastNode"
    return nullptr;
}


QWidget* DrawDebugLineNode::createCustomWidget()
{
    // TODO: Implement custom widget for "DrawDebugLineNode"
    return nullptr;
}


QWidget* DrawDebugSphereNode::createCustomWidget()
{
    // TODO: Implement custom widget for "DrawDebugSphereNode"
    return nullptr;
}
} // namespace NodeLibrary