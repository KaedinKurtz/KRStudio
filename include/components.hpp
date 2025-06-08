#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include "Grid.hpp"
#include "Camera.hpp"

struct SceneProperties
{
    bool fogEnabled = true;
    glm::vec3 fogColor = { 0.1f, 0.1f, 0.1f };
    float fogStartDistance = 10.0f;
    float fogEndDistance = 75.0f;
};

struct TransformComponent
{
    glm::vec3 translation = { 0.0f, 0.0f, 0.0f };
    glm::quat rotation = { 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec3 scale = { 1.0f, 1.0f, 1.0f };

    glm::mat4 getTransform() const {
        return glm::translate(glm::mat4(1.0f), translation) *
            glm::mat4_cast(rotation) *
            glm::scale(glm::mat4(1.0f), scale);
    }
};

struct GridComponent
{
    // --- ADD THIS LINE ---
    bool visible = true;
    bool visible2 = true;
    bool visible3 = true;
    bool visible4 = true;
    bool visible5 = true;

    std::vector<GridLevel> levels;
    float baseLineWidthPixels = 1.0f;

    bool showAxes = true;
    bool isMetric = true;

    glm::vec3 xAxisColor = { 1.0f, 0.2f, 0.2f };
    glm::vec3 zAxisColor = { 0.2f, 0.2f, 1.0f };

    glm::vec3 xAxis2Color = { 1.0f, 0.2f, 0.2f };
    glm::vec3 zAxis2Color = { 0.2f, 0.2f, 1.0f };

    glm::vec3 xAxis3Color = { 1.0f, 0.2f, 0.2f };
    glm::vec3 zAxis3Color = { 0.2f, 0.2f, 1.0f };

    glm::vec3 xAxis4Color = { 1.0f, 0.2f, 0.2f };
    glm::vec3 zAxis4Color = { 0.2f, 0.2f, 1.0f };

    glm::vec3 xAxis5Color = { 1.0f, 0.2f, 0.2f };
    glm::vec3 zAxis5Color = { 0.2f, 0.2f, 1.0f };

    glm::vec3 origin = { 1.0f, 0.2f, 0.2f };

    glm::vec3 eulerOrient = { 1.0f, 0.2f, 0.2f };
	glm::vec4 quaternionOrient = { 1.0f, 0.2f, 0.2f, 0.3f };

    float axisLineWidthPixels = 1.4f;
};

struct RenderableMeshComponent
{
    glm::vec4 color = { 0.8f, 0.8f, 0.8f, 1.0f };
};

struct TagComponent
{
    std::string tag;
    TagComponent() = default;
    TagComponent(const TagComponent&) = default;
    TagComponent(const std::string& t) : tag(t) {}
};

struct CameraComponent
{
    Camera camera;
    bool isPrimary = true;
};
