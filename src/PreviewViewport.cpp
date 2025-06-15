#include "PreviewViewport.hpp"
#include "SceneBuilder.hpp"
#include "Shader.hpp"
#include "Mesh.hpp"
#include "Camera.hpp"
#include "components.hpp"
#include "DebugHelpers.hpp"

#include <QTimer>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <QDebug>
#include <cmath>
#include <stdexcept>
#include <iomanip>

// Recursive helper function for Forward Kinematics
glm::mat4 calculatePreviewWorldTransform(entt::entity entity, entt::registry& registry, int depth = 0)
{
    QString indent = QString(depth * 4, ' ');
    auto& tag = registry.get<TagComponent>(entity);
    qDebug().noquote() << indent << "[FK] Calculating for" << entity << "tagged as" << QString::fromStdString(tag.tag);

    auto& transformComp = registry.get<TransformComponent>(entity);
    glm::mat4 localTransform = transformComp.getTransform();

    glm::mat4 finalTransform = localTransform;

    if (registry.all_of<ParentComponent>(entity)) {
        auto& parentComp = registry.get<ParentComponent>(entity);
        if (registry.valid(parentComp.parent)) {
            qDebug().noquote() << indent << "  -> Found parent" << parentComp.parent << ". Recursing...";
            glm::mat4 parentWorldTransform = calculatePreviewWorldTransform(parentComp.parent, registry, depth + 1);
            finalTransform = parentWorldTransform * localTransform;
        }
        else {
            qDebug().noquote() << indent << "  -> Has ParentComponent but parent entity is INVALID.";
        }
    }
    else {
        qDebug().noquote() << indent << "  -> No parent. This is a root entity in the hierarchy.";
    }
    return finalTransform;
}


PreviewViewport::PreviewViewport(QWidget* parent)
    : QOpenGLWidget(parent)
{
    m_scene = std::make_unique<Scene>();
    m_animationTimer = new QTimer(this);
}

PreviewViewport::~PreviewViewport()
{
    if (isValid()) {
        makeCurrent();
        for (auto& [key, cached] : m_meshCache) {
            if (cached.VAO) glDeleteVertexArrays(1, &cached.VAO);
            if (cached.VBO) glDeleteBuffers(1, &cached.VBO);
            if (cached.EBO) glDeleteBuffers(1, &cached.EBO);
        }
    }
}

void PreviewViewport::updateRobot(const RobotDescription& description)
{
    qDebug() << "\n\n--- [PreviewViewport] Received new RobotDescription. Updating now. ---";
    makeCurrent();
    m_robotDesc = std::make_unique<RobotDescription>(description);

    qDebug() << "[PreviewViewport] Clearing"
             << m_scene->getRegistry().storage<entt::entity>().size()
             << "old entities from the scene.";
    m_scene->getRegistry().clear();

    m_cameraEntity = m_scene->getRegistry().create();
    m_scene->getRegistry().emplace<CameraComponent>(m_cameraEntity).camera.forceRecalculateView(glm::vec3(1.5f, 1.5f, 2.0f), glm::vec3(0.0f, 0.5f, 0.0f), 0.0f);
    qDebug() << "[PreviewViewport] Created new preview camera entity:" << m_cameraEntity;

    for (const auto& link : m_robotDesc->links) {
        if (!link.mesh_filepath.empty() && m_meshCache.find(link.mesh_filepath) == m_meshCache.end()) {
            qDebug() << "[PreviewViewport] Caching new mesh for:" << QString::fromStdString(link.mesh_filepath);
            try {
                CachedMesh cached;
                cached.mesh = std::make_unique<Mesh>(Mesh::getLitCubeVertices(), Mesh::getLitCubeIndices());
                glGenVertexArrays(1, &cached.VAO);
                glGenBuffers(1, &cached.VBO);
                glGenBuffers(1, &cached.EBO);
                glBindVertexArray(cached.VAO);
                glBindBuffer(GL_ARRAY_BUFFER, cached.VBO);
                const auto& verts = cached.mesh->vertices();
                glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cached.EBO);
                const auto& inds = cached.mesh->indices();
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, inds.size() * sizeof(unsigned int), inds.data(), GL_STATIC_DRAW);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
                glEnableVertexAttribArray(1);
                glBindVertexArray(0);
                m_meshCache.emplace(link.mesh_filepath, std::move(cached));
            }
            catch (const std::exception& e) {
                qWarning() << "[PreviewViewport] FAILED to load mesh:" << e.what();
            }
        }
    }

    SceneBuilder::spawnRobot(*m_scene, *m_robotDesc);
    update();
    qDebug() << "--- [PreviewViewport] Update complete. Triggering repaint. ---";
}

void PreviewViewport::setAnimationSpeed(int sliderValue)
{
    m_animationSpeed = static_cast<float>(sliderValue) / 50.0f;
}

void PreviewViewport::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.2f, 0.22f, 0.25f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);

    try {
        m_phongShader = std::make_unique<Shader>(this, "shaders/vertex_shader.glsl", "shaders/fragment_shader.glsl");
        CachedMesh placeholder;
        placeholder.mesh = std::make_unique<Mesh>(Mesh::getLitCubeVertices(), Mesh::getLitCubeIndices());
        glGenVertexArrays(1, &placeholder.VAO);
        glGenBuffers(1, &placeholder.VBO);
        glGenBuffers(1, &placeholder.EBO);
        glBindVertexArray(placeholder.VAO);
        glBindBuffer(GL_ARRAY_BUFFER, placeholder.VBO);
        const auto& pverts = placeholder.mesh->vertices();
        glBufferData(GL_ARRAY_BUFFER, pverts.size() * sizeof(float), pverts.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, placeholder.EBO);
        const auto& pinds = placeholder.mesh->indices();
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, pinds.size() * sizeof(unsigned int), pinds.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
        m_meshCache.emplace("placeholder", std::move(placeholder));
    }
    catch (const std::exception& e) {
        qCritical() << "[PreviewViewport] CRITICAL ERROR: failed to initialize resources:" << e.what();
    }

    connect(m_animationTimer, &QTimer::timeout, this, &PreviewViewport::onAnimationTick);
    m_animationTimer->start(16);
}

void PreviewViewport::onAnimationTick()
{
    m_totalTime += 0.016f * m_animationSpeed;
    if (!m_scene || !m_robotDesc || m_animationSpeed == 0) return;

    auto& registry = m_scene->getRegistry();
    auto jointView = registry.view<JointComponent, TransformComponent>();

    for (auto entity : jointView) {
        auto& joint = jointView.get<JointComponent>(entity);
        auto& transform = jointView.get<TransformComponent>(entity);

        float currentAngle = 0.0f;
        if (joint.description.type == JointType::REVOLUTE) {
            float range = joint.description.limits.upper - joint.description.limits.lower;
            if (std::abs(range) > 0.001f) {
                float phase = 0.5f * (1.0f + std::sin(m_totalTime));
                currentAngle = joint.description.limits.lower + range * phase;
            }
        }
        else if (joint.description.type == JointType::CONTINUOUS) {
            currentAngle = m_totalTime;
        }

        transform.rotation = glm::angleAxis(currentAngle, joint.description.axis);
    }

    update();
}


void PreviewViewport::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!m_scene || !m_phongShader || m_meshCache.empty() || !m_robotDesc) {
        qWarning() << "[PreviewViewport] paintGL aborted: Resources not ready.";
        return;
    }
    auto& registry = m_scene->getRegistry();
    if (!registry.valid(m_cameraEntity)) {
        qWarning() << "[PreviewViewport] paintGL aborted: Camera entity is invalid.";
        return;
    }
    auto& camera = registry.get<CameraComponent>(m_cameraEntity).camera;

    float orbitAngle = m_totalTime * 0.1f;
    glm::vec3 camPos = glm::vec3(2.5f * cos(orbitAngle), 1.5f, 2.5f * sin(orbitAngle));
    camera.forceRecalculateView(camPos, glm::vec3(0.0f, 0.5f, 0.0f), 0.0f);

    const float aspectRatio = (height() > 0) ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;
    const glm::mat4 viewMatrix = camera.getViewMatrix();
    const glm::mat4 projectionMatrix = camera.getProjectionMatrix(aspectRatio);

    qDebug() << "\n--- [PreviewViewport] paintGL Start ---";

    m_phongShader->use();
    m_phongShader->setMat4("view", viewMatrix);
    m_phongShader->setMat4("projection", projectionMatrix);
    m_phongShader->setVec3("lightPos", camPos);
    m_phongShader->setVec3("viewPos", camPos);
    m_phongShader->setVec3("lightColor", glm::vec3(1.0f, 1.0f, 1.0f));

    auto renderableView = registry.view<const TagComponent, const RenderableMeshComponent>();
    int renderableCount = 0;
    for (auto entity : renderableView) { (void)entity; renderableCount++; }
    qDebug() << "[PreviewViewport] Found" << renderableCount << "renderable entities to draw.";

    for (auto entity : renderableView) {
        const auto& tag = renderableView.get<const TagComponent>(entity);
        qDebug() << "  [PreviewViewport] --- Processing entity" << entity << "tagged as" << QString::fromStdString(tag.tag) << "---";

        glm::mat4 worldTransform = calculatePreviewWorldTransform(entity, registry);
        printMatrix(worldTransform, "    [PreviewViewport] Final World Transform for" + QString::fromStdString(tag.tag) + ":");
        m_phongShader->setMat4("model", worldTransform);

        auto it = std::find_if(m_robotDesc->links.begin(), m_robotDesc->links.end(),
            [&](const LinkDescription& desc) { return desc.name == tag.tag; });

        if (it != m_robotDesc->links.end()) {
            if (!it->mesh_filepath.empty() && m_meshCache.count(it->mesh_filepath)) {
                qDebug() << "    [PreviewViewport] Found mesh in cache:" << QString::fromStdString(it->mesh_filepath) << ". Drawing.";
                const auto& cached = m_meshCache.at(it->mesh_filepath);
                glBindVertexArray(cached.VAO);
                glDrawElements(GL_TRIANGLES, static_cast<int>(cached.mesh->indices().size()), GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);
            } else {
                qDebug() << "    [PreviewViewport] Mesh not found or specified. Drawing placeholder.";
                const auto& cached = m_meshCache.at("placeholder");
                glBindVertexArray(cached.VAO);
                glDrawElements(GL_TRIANGLES, static_cast<int>(cached.mesh->indices().size()), GL_UNSIGNED_INT, 0);
                glBindVertexArray(0);
            }
        }
        else {
            qDebug() << "    [PreviewViewport] Tag" << QString::fromStdString(tag.tag) << "is not a link. Drawing placeholder.";
            const auto& cached = m_meshCache.at("placeholder");
            glBindVertexArray(cached.VAO);
            glDrawElements(GL_TRIANGLES, static_cast<int>(cached.mesh->indices().size()), GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
        }
    }
    qDebug() << "--- [PreviewViewport] paintGL End ---\n";
}

void PreviewViewport::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}