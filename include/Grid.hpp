#pragma once

#include <QOpenGLFunctions_3_3_Core>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <memory>
#include <string>
#include <algorithm>
#include "GridLevel.hpp"

class Shader;
class Mesh;
class Camera;

class Grid {
public:
    Grid(QOpenGLFunctions_3_3_Core* glFunctions);
    ~Grid();

    void draw(const Camera& camera, float aspectRatio);

    void setTransform(const glm::mat4& transform);
    void setTransform(const glm::vec3& center, const glm::quat& orientation);
    void setTransformFromEuler(const glm::vec3& center, const glm::vec3& eulerAngles);
    void setTransformFromNormal(const glm::vec3& center, const glm::vec3& normal);
    const glm::mat4& getTransform() const { return m_transform; }

    void setLevels(const std::vector<GridLevel>& levels);
    void addLevel(float spacing, const glm::vec3& color, float fadeInCamDistEnd, float fadeInCamDistStart);
    void clearLevels();

    void setBaseLineWidthPixels(float width);

    void setShowAxes(bool show);
    void setAxisProperties(const glm::vec3& xAxisColor, const glm::vec3& zAxisColor, float lineWidthPixels);

    void setShowOriginSphere(bool show);
    void setOriginSphereProperties(float radius, const glm::vec3& color);

    void setFog(bool enabled, const glm::vec3& color, float startDistance, float endDistance);

private:
    void updateGridShaderUniforms(const Camera& camera, float aspectRatio); // Ensure aspectRatio is here
    void updateSphereShaderUniforms(const Camera& camera, float aspectRatio);
    void createDefaultQuadMesh();
    void createSphereMesh(float radius, int sectorCount, int stackCount);

    QOpenGLFunctions_3_3_Core* m_gl;

    std::unique_ptr<Shader> m_gridShader;
    std::unique_ptr<Mesh>   m_gridMesh;
    unsigned int m_gridVAO = 0;
    unsigned int m_gridVBO = 0;

    std::unique_ptr<Shader> m_sphereShader;
    std::unique_ptr<Mesh>   m_sphereMesh;
    unsigned int m_sphereVAO = 0;
    unsigned int m_sphereVBO = 0;
    unsigned int m_sphereEBO = 0;

    glm::mat4 m_transform;
    std::vector<GridLevel> m_levels;
    float m_baseLineWidthPixels;

    bool m_showAxes;
    glm::vec3 m_xAxisColor;
    glm::vec3 m_zAxisColor;
    float m_axisLineWidthPixels;

    bool m_showOriginSphere;
    float m_originSphereRadius;
    glm::vec3 m_originSphereColor;

    bool m_useFog;
    glm::vec3 m_fogColor;
    float m_fogStartDistance;
    float m_fogEndDistance;

    static constexpr int MAX_GRID_LEVELS = 5; // Ensure GLSL matches this if array size is fixed
};
