#pragma once

#include "Node.hpp"
#include "NodeFactory.hpp"
#include "components.hpp" // For Camera, entt::entity
#include <glm/glm.hpp>
#include <string>

namespace NodeLibrary {

    // --- Data Structures ---

    struct RaycastHit {
        bool has_hit = false;
        entt::entity hit_entity = entt::null;
        glm::vec3 world_point{ 0.f };
        glm::vec3 world_normal{ 0.f };
        float distance = 0.f;
    };

    // --- Free Functions (to be wrapped by nodes) ---

    void drawDebugSphere(const glm::vec3& center, float radius, const glm::vec4& color, float duration_s);
    void drawDebugLine(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color, float duration_s);
    RaycastHit getMouseRaycast(const Camera& camera, float mouse_x, float mouse_y);
    bool isKeyPressed(int key_code);
    void printToConsole(const std::string& message);

    // --- Node Classes ---

    class DrawDebugSphereNode : public Node {
    public:
        DrawDebugSphereNode();
        void compute() override;
    };

    class DrawDebugLineNode : public Node {
    public:
        DrawDebugLineNode();
        void compute() override;
    };

    class GetMouseRaycastNode : public Node {
    public:
        GetMouseRaycastNode();
        void compute() override;
    };

    class IsKeyPressedNode : public Node {
    public:
        int key_code = 0; // This would be configured in the IDE per-instance
        IsKeyPressedNode();
        void compute() override;
    };

    class PrintToConsoleNode : public Node {
    public:
        PrintToConsoleNode();
        void compute() override;
    };

} // namespace NodeLibrary