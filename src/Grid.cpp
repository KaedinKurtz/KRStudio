
#define _USE_MATH_DEFINES // For M_PI
#include <cmath> 

#include "Grid.hpp"
#include "Shader.hpp"
#include "Mesh.hpp"
#include "Camera.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp> // For glm::rotation
#include <QDebug>
#include <algorithm> 

Grid::Grid(QOpenGLFunctions_3_3_Core* glFunctions)
    : m_gl(glFunctions),
    m_gridShader(nullptr),
    m_gridMesh(nullptr),
    m_sphereShader(nullptr),
    m_sphereMesh(nullptr),
    m_transform(glm::mat4(1.0f)),
    m_baseLineWidthPixels(1.0f),
    m_showAxes(true),
    m_xAxisColor(1.0f, 0.2f, 0.2f),
    m_zAxisColor(0.2f, 0.2f, 1.0f),
    m_axisLineWidthPixels(1.5f),
    m_showOriginSphere(true),
    m_originSphereRadius(0.05f),
    m_originSphereColor(1.0f, 1.0f, 1.0f),
    m_useFog(true), // Set to false by default if you want to disable fog initially
    m_fogColor(0.12f, 0.12f, 0.13f),
    m_fogStartDistance(200.0f), // Start fog far away to ensure it's not the issue
    m_fogEndDistance(250.0f)   // End fog even further
{
    if (!m_gl) {
        qCritical() << "Grid Constructor: Received NULL QOpenGLFunctions pointer!";
        return;
    }

    try {
        m_gridShader = std::make_unique<Shader>(m_gl, "shaders/grid_vert.glsl", "shaders/grid_frag.glsl");
        if (!m_gridShader || m_gridShader->ID == 0) {
            qWarning() << "Grid: Failed to load or compile grid shaders.";
            // No return here, allow sphere to load
        }

        m_sphereShader = std::make_unique<Shader>(m_gl, "shaders/sphere_vert.glsl", "shaders/sphere_frag.glsl");
        if (!m_sphereShader || m_sphereShader->ID == 0) {
            qWarning() << "Grid: Failed to load or compile sphere shaders.";
        }
    }
    catch (const std::exception& e) {
        qWarning() << "Grid: Exception during shader creation: " << e.what();
        // Potentially return or ensure shaders are null
        m_gridShader.reset();
        m_sphereShader.reset();
        return;
    }

    createDefaultQuadMesh();
    createSphereMesh(1.0f, 32, 16);

    // Your specified default levels
    addLevel(0.001f, glm::vec3(0.6f, 0.6f, 0.4f), 0.80f, .4f);
    addLevel(0.01f, glm::vec3(0.25f, 0.3f, 0.4f), 2.0f, 1.0f);
    addLevel(0.1f, glm::vec3(0.9f, 0.85f, 0.6f), 10.0f, 5.0f);
    addLevel(1.0f, glm::vec3(0.7f, 0.5f, 0.2f), 200.0f, 7.0f);
    addLevel(10.0f, glm::vec3(0.2f, 0.7f, 0.9f), 200.0f, 20.0f);
}

Grid::~Grid() {
    qDebug() << "Grid destructor called.";
    if (m_gl) {
        if (m_gridVAO) m_gl->glDeleteVertexArrays(1, &m_gridVAO);
        if (m_gridVBO) m_gl->glDeleteBuffers(1, &m_gridVBO);
        if (m_sphereVAO) m_gl->glDeleteVertexArrays(1, &m_sphereVAO);
        if (m_sphereVBO) m_gl->glDeleteBuffers(1, &m_sphereVBO);
        if (m_sphereEBO) m_gl->glDeleteBuffers(1, &m_sphereEBO);
    }
}

void Grid::createDefaultQuadMesh() {
    const float halfSize = 1000.0f; // Large quad for the grid
    const std::vector<float> quad_vertices = {
        // Positions          
        -halfSize, 0.0f, -halfSize,
         halfSize, 0.0f, -halfSize,
         halfSize, 0.0f,  halfSize,
         halfSize, 0.0f,  halfSize,
        -halfSize, 0.0f,  halfSize,
        -halfSize, 0.0f, -halfSize
    };
    try {
        m_gridMesh = std::make_unique<Mesh>(quad_vertices);
        if (m_gl) {
            m_gl->glGenVertexArrays(1, &m_gridVAO);
            m_gl->glGenBuffers(1, &m_gridVBO);
            m_gl->glBindVertexArray(m_gridVAO);
            m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_gridVBO);
            m_gl->glBufferData(GL_ARRAY_BUFFER, quad_vertices.size() * sizeof(float), quad_vertices.data(), GL_STATIC_DRAW);
            m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
            m_gl->glEnableVertexAttribArray(0);
            m_gl->glBindVertexArray(0);
        }
    }
    catch (const std::exception& e) {
        qWarning() << "Grid: Exception during quad mesh creation: " << e.what();
    }
}

void Grid::createSphereMesh(float radius, int sectorCount, int stackCount) {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;
    float x, y, z, xy;
    float sectorStep = 2.0f * M_PI / sectorCount;
    float stackStep = M_PI / stackCount;
    float sectorAngle, stackAngle;

    for (int i = 0; i <= stackCount; ++i) {
        stackAngle = M_PI / 2.0f - i * stackStep;
        xy = radius * cosf(stackAngle);
        z = radius * sinf(stackAngle);
        for (int j = 0; j <= sectorCount; ++j) {
            sectorAngle = j * sectorStep;
            x = xy * cosf(sectorAngle);
            y = xy * sinf(sectorAngle);
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
        }
    }
    int k1, k2;
    for (int i = 0; i < stackCount; ++i) {
        k1 = i * (sectorCount + 1);
        k2 = k1 + sectorCount + 1;
        for (int j = 0; j < sectorCount; ++j, ++k1, ++k2) {
            if (i != 0) {
                indices.push_back(k1); indices.push_back(k2); indices.push_back(k1 + 1);
            }
            if (i != (stackCount - 1)) {
                indices.push_back(k1 + 1); indices.push_back(k2); indices.push_back(k2 + 1);
            }
        }
    }
    try {
        m_sphereMesh = std::make_unique<Mesh>(vertices, indices);
        if (m_gl) {
            m_gl->glGenVertexArrays(1, &m_sphereVAO);
            m_gl->glGenBuffers(1, &m_sphereVBO);
            m_gl->glGenBuffers(1, &m_sphereEBO);
            m_gl->glBindVertexArray(m_sphereVAO);
            m_gl->glBindBuffer(GL_ARRAY_BUFFER, m_sphereVBO);
            m_gl->glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
            m_gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_sphereEBO);
            m_gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
            m_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
            m_gl->glEnableVertexAttribArray(0);
            m_gl->glBindVertexArray(0);
        }
    }
    catch (const std::exception& e) {
        qWarning() << "Grid: Exception during sphere mesh creation: " << e.what();
    }
}

void Grid::draw(const Camera& camera, float aspectRatio) {
    if (m_gridShader && m_gridShader->ID != 0 && m_gridMesh && m_gridVAO != 0 && !m_levels.empty()) {
        m_gridShader->use();
        updateGridShaderUniforms(camera, aspectRatio);
        m_gl->glBindVertexArray(m_gridVAO);
        m_gl->glDrawArrays(GL_TRIANGLES, 0, static_cast<int>(m_gridMesh->vertices().size() / 3));
        m_gl->glBindVertexArray(0);
    }

    if (m_showOriginSphere && m_sphereShader && m_sphereShader->ID != 0 && m_sphereMesh && m_sphereVAO != 0) {
        m_sphereShader->use();
        updateSphereShaderUniforms(camera, aspectRatio);
        m_gl->glBindVertexArray(m_sphereVAO);
        m_gl->glDrawElements(GL_TRIANGLES, static_cast<int>(m_sphereMesh->indices().size()), GL_UNSIGNED_INT, 0);
        m_gl->glBindVertexArray(0);
    }
}

void Grid::updateGridShaderUniforms(const Camera& camera, float aspectRatio) { // Added aspectRatio
    m_gridShader->setMat4("u_gridModelMatrix", m_transform);
    m_gridShader->setMat4("u_viewMatrix", camera.getViewMatrix());
    m_gridShader->setMat4("u_projectionMatrix", camera.getProjectionMatrix(aspectRatio)); // Use passed aspectRatio
    m_gridShader->setVec3("u_cameraPos", camera.getPosition());
    m_gridShader->setFloat("u_cameraDistanceToFocal", camera.getDistance());

    int numLevelsToSend = static_cast<int>(m_levels.size());
    numLevelsToSend = std::min(numLevelsToSend, MAX_GRID_LEVELS); // Clamp to shader max
    m_gridShader->setInt("u_numLevels", numLevelsToSend);

    for (int i = 0; i < numLevelsToSend; ++i) {
        std::string base = "u_levels[" + std::to_string(i) + "].";
        m_gridShader->setFloat((base + "spacing").c_str(), m_levels[i].spacing);
        m_gridShader->setVec3((base + "color").c_str(), m_levels[i].color);
        m_gridShader->setFloat((base + "fadeInCameraDistanceEnd").c_str(), m_levels[i].fadeInCameraDistanceEnd);
        m_gridShader->setFloat((base + "fadeInCameraDistanceStart").c_str(), m_levels[i].fadeInCameraDistanceStart);
    }

    m_gridShader->setFloat("u_baseLineWidthPixels", m_baseLineWidthPixels);

    m_gridShader->setBool("u_showAxes", m_showAxes);
    m_gridShader->setVec3("u_xAxisColor", m_xAxisColor);
    m_gridShader->setVec3("u_zAxisColor", m_zAxisColor);
    m_gridShader->setFloat("u_axisLineWidthPixels", m_axisLineWidthPixels);

    m_gridShader->setBool("u_useFog", m_useFog);
    m_gridShader->setVec3("u_fogColor", m_fogColor);
    m_gridShader->setFloat("u_fogStartDistance", m_fogStartDistance);
    m_gridShader->setFloat("u_fogEndDistance", m_fogEndDistance);
}

void Grid::updateSphereShaderUniforms(const Camera& camera, float aspectRatio) {
    glm::mat4 sphereModelMatrix = glm::translate(m_transform, glm::vec3(0.0f)); // Position sphere at grid's origin
    sphereModelMatrix = glm::scale(sphereModelMatrix, glm::vec3(m_originSphereRadius));

    m_sphereShader->setMat4("u_modelMatrix", sphereModelMatrix);
    m_sphereShader->setMat4("u_viewMatrix", camera.getViewMatrix());
    m_sphereShader->setMat4("u_projectionMatrix", camera.getProjectionMatrix(aspectRatio));
    m_sphereShader->setVec3("u_color", m_originSphereColor);
    m_sphereShader->setVec3("u_cameraPos", camera.getPosition()); // For sphere fog

    m_sphereShader->setBool("u_useFog", m_useFog);
    m_sphereShader->setVec3("u_fogColor", m_fogColor);
    m_sphereShader->setFloat("u_fogStartDistance", m_fogStartDistance);
    m_sphereShader->setFloat("u_fogEndDistance", m_fogEndDistance);
}

// --- Transform Customization ---
void Grid::setTransform(const glm::mat4& transform) { m_transform = transform; }
void Grid::setTransform(const glm::vec3& center, const glm::quat& orientation) {
    m_transform = glm::translate(glm::mat4(1.0f), center) * glm::mat4_cast(orientation);
}
void Grid::setTransformFromEuler(const glm::vec3& center, const glm::vec3& eulerAngles) {
    glm::quat orientation = glm::quat(glm::radians(eulerAngles));
    m_transform = glm::translate(glm::mat4(1.0f), center) * glm::mat4_cast(orientation);
}
void Grid::setTransformFromNormal(const glm::vec3& center, const glm::vec3& normal) {
    glm::vec3 localUp = glm::vec3(0.0f, 1.0f, 0.0f); // Assuming grid's "up" is local Y
    glm::vec3 targetNormal = glm::normalize(normal);
    if (glm::length(targetNormal) < 0.0001f) {
        m_transform = glm::translate(glm::mat4(1.0f), center); return;
    }
    glm::quat orientation = glm::rotation(localUp, targetNormal);
    m_transform = glm::translate(glm::mat4(1.0f), center) * glm::mat4_cast(orientation);
}

// --- Level Customization ---
void Grid::setLevels(const std::vector<GridLevel>& levels) {
    m_levels = levels;
    std::sort(m_levels.begin(), m_levels.end(), [](const GridLevel& a, const GridLevel& b) {
        return a.spacing < b.spacing;
        });
}
void Grid::addLevel(float spacing, const glm::vec3& color, float fadeInCamDistEnd, float fadeInCamDistStart) {
    m_levels.emplace_back(spacing, color, fadeInCamDistEnd, fadeInCamDistStart);
    std::sort(m_levels.begin(), m_levels.end(), [](const GridLevel& a, const GridLevel& b) {
        return a.spacing < b.spacing;
        });
}
void Grid::clearLevels() { m_levels.clear(); }

// --- Appearance Customization ---
void Grid::setBaseLineWidthPixels(float width) {
    m_baseLineWidthPixels = glm::max(0.1f, width);
}

// --- Axes Customization ---
void Grid::setShowAxes(bool show) { m_showAxes = show; }
void Grid::setAxisProperties(const glm::vec3& xAxisColor, const glm::vec3& zAxisColor, float lineWidthPixels) {
    m_xAxisColor = xAxisColor; m_zAxisColor = zAxisColor;
    m_axisLineWidthPixels = glm::max(0.1f, lineWidthPixels);
}

// --- Origin Sphere Customization ---
void Grid::setShowOriginSphere(bool show) { m_showOriginSphere = show; }
void Grid::setOriginSphereProperties(float radius, const glm::vec3& color) {
    m_originSphereRadius = glm::max(0.001f, radius); m_originSphereColor = color;
}

// --- Fog/Fade Customization ---
void Grid::setFog(bool enabled, const glm::vec3& color, float startDistance, float endDistance) {
    m_useFog = enabled; m_fogColor = color;
    m_fogStartDistance = glm::max(0.0f, startDistance);
    m_fogEndDistance = glm::max(m_fogStartDistance + 0.1f, endDistance);
}
