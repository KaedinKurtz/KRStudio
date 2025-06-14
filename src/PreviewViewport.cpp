#include "PreviewViewport.hpp"
#include "SceneBuilder.hpp"
#include "Shader.hpp"
#include "Mesh.hpp"
#include "Camera.hpp"
#include "components.hpp"

#include <QTimer>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <QDebug>
#include <cmath>
#include <stdexcept>

PreviewViewport::PreviewViewport(QWidget* parent)
    : QOpenGLWidget(parent)
{
    m_scene = std::make_unique<Scene>();
    m_animationTimer = new QTimer(this);
}

PreviewViewport::~PreviewViewport() = default;

void PreviewViewport::updateRobot(const RobotDescription& description)
{
    makeCurrent();
    m_robotDesc = std::make_unique<RobotDescription>(description);
    m_scene->getRegistry().clear();

    m_cameraEntity = m_scene->getRegistry().create();
    m_scene->getRegistry().emplace<CameraComponent>(m_cameraEntity).camera.forceRecalculateView(glm::vec3(1.5f, 1.5f, 2.0f), glm::vec3(0.0f, 0.5f, 0.0f), 0.0f);

    for (const auto& link : m_robotDesc->links) {
        if (!link.mesh_filepath.empty() && m_meshCache.find(link.mesh_filepath) == m_meshCache.end()) {
            qInfo() << "[Preview] Caching new mesh:" << QString::fromStdString(link.mesh_filepath);
            try {
                // TODO: Replace this with a real mesh loader. For now, we use a placeholder.
                m_meshCache[link.mesh_filepath] = std::make_unique<Mesh>(this, Mesh::getLitCubeVertices(), Mesh::getLitCubeIndices(), true);
            }
            catch (const std::exception& e) {
                qWarning() << "Failed to load mesh:" << e.what();
            }
        }
    }

    SceneBuilder::spawnRobot(*m_scene, *m_robotDesc);
    update();
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
        m_meshCache["placeholder"] = std::make_unique<Mesh>(this, Mesh::getLitCubeVertices(), Mesh::getLitCubeIndices(), true);
    }
    catch (const std::exception& e) {
        qCritical() << "PreviewViewport failed to initialize resources:" << e.what();
    }

    connect(m_animationTimer, &QTimer::timeout, this, &PreviewViewport::onAnimationTick);
    m_animationTimer->start(16);
}

void PreviewViewport::onAnimationTick()
{
    m_totalTime += 0.016f * m_animationSpeed;
    if (m_totalTime > 10000.0f) m_totalTime -= 10000.0f;
    update();
}

void PreviewViewport::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!m_scene || !m_phongShader || m_meshCache.empty() || !m_robotDesc) return;
    auto& registry = m_scene->getRegistry();
    if (!registry.valid(m_cameraEntity)) return;
    auto& camera = registry.get<CameraComponent>(m_cameraEntity).camera;

    float orbitAngle = m_totalTime * 0.1f;
    glm::vec3 camPos = glm::vec3(2.5f * cos(orbitAngle), 1.5f, 2.5f * sin(orbitAngle));
    camera.forceRecalculateView(camPos, glm::vec3(0.0f, 0.5f, 0.0f), 0.0f);

    const float aspectRatio = (height() > 0) ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;
    const glm::mat4 viewMatrix = camera.getViewMatrix();
    const glm::mat4 projectionMatrix = camera.getProjectionMatrix(aspectRatio);

    m_phongShader->use();
    m_phongShader->setMat4("view", viewMatrix);
    m_phongShader->setMat4("projection", projectionMatrix);
    m_phongShader->setVec3("lightPos", camPos);
    m_phongShader->setVec3("viewPos", camPos);
    m_phongShader->setVec3("lightColor", glm::vec3(1.0f, 1.0f, 1.0f));

    // --- FORWARD KINEMATICS AND RENDERING PASS ---
    // This map will store the final world transform for each link.
    std::unordered_map<entt::entity, glm::mat4> worldTransforms;

    // Initialize all link transforms to their local transform first.
    auto linkView = registry.view<TransformComponent, TagComponent>();
    for (auto entity : linkView) {
        worldTransforms[entity] = registry.get<TransformComponent>(entity).getTransform();
    }

    // Iterate through the joints in the order they were defined. This assumes
    // a simple chain structure, which is true for most URDFs.
    for (const auto& jointDesc : m_robotDesc->joints) {
        auto jointView = registry.view<const JointComponent, const TagComponent>();
        entt::entity currentJointEntity = entt::null;

        // Find the entity for the current joint description.
        for (auto entity : jointView) {
            if (jointView.get<const TagComponent>(entity).tag == jointDesc.name) {
                currentJointEntity = entity;
                break;
            }
        }
        if (!registry.valid(currentJointEntity)) continue;

        const auto& joint = jointView.get<const JointComponent>(currentJointEntity);
        if (!registry.valid(joint.parentLink) || !registry.valid(joint.childLink)) continue;

        // Get the parent link's final world transform.
        glm::mat4 parentWorldTransform = worldTransforms.at(joint.parentLink);

        // Calculate the current joint angle for animation.
        float currentAngle = 0.0f;
        if (joint.description.type == JointType::REVOLUTE) {
            float range = joint.description.limits.upper - joint.description.limits.lower;
            if (std::abs(range) > 0.001) {
                float phase = 0.5f * (1.0f + std::sin(m_totalTime));
                currentAngle = joint.description.limits.lower + range * phase;
            }
        }
        else if (joint.description.type == JointType::CONTINUOUS) {
            currentAngle = m_totalTime;
        }

        // Calculate the joint's transform (rotation around its axis).
        glm::mat4 jointTransform = glm::rotate(glm::mat4(1.0f), currentAngle, joint.description.axis);

        // Get the child link's local transform relative to the joint.
        glm::mat4 childLocalTransform = registry.get<TransformComponent>(joint.childLink).getTransform();

        // Final transform = Parent's World Transform * Joint's Transform * Child's Local Transform
        worldTransforms[joint.childLink] = parentWorldTransform * jointTransform * childLocalTransform;
    }

    // --- RENDER ALL LINKS ---
    // Now that all world transforms are calculated, draw each link.
    for (const auto& [entity, transformMatrix] : worldTransforms) {
        m_phongShader->setMat4("model", transformMatrix);

        const auto& tag = registry.get<const TagComponent>(entity);
        auto it = std::find_if(m_robotDesc->links.begin(), m_robotDesc->links.end(),
            [&](const LinkDescription& desc) { return desc.name == tag.tag; });

        if (it != m_robotDesc->links.end()) {
            if (!it->mesh_filepath.empty() && m_meshCache.count(it->mesh_filepath)) {
                m_meshCache.at(it->mesh_filepath)->draw();
            }
            else {
                m_meshCache.at("placeholder")->draw();
            }
        }
    }
}

void PreviewViewport::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}
