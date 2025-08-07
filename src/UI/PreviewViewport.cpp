#include "PreviewViewport.hpp"
#include "RenderingSystem.hpp"
#include "Scene.hpp"
#include "Camera.hpp"
#include "components.hpp"
#include "SceneBuilder.hpp"

#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLFunctions_4_3_Core>
#include <QDebug>
#include <vector> // Required for the clearPreview temporary vector

// The constructor now creates its own world.
PreviewViewport::PreviewViewport(QWidget* parent)
    : ViewportWidget(nullptr, nullptr, entt::null, parent)
{
    m_previewScene = std::make_unique<Scene>();
    m_previewRenderSystem = std::make_unique<RenderingSystem>(this);

    auto& registry = m_previewScene->getRegistry();
    registry.ctx().emplace<SceneProperties>();

    m_cameraEntity = SceneBuilder::createCamera(*m_scene, { 0, 1, 3 }, {1, 1, 1});
    m_previewScene->setPrimaryCamera(m_cameraEntity);

    m_scene = m_previewScene.get();
    m_renderingSystem = m_previewRenderSystem.get();
}

// The destructor ensures a clean shutdown of its own RenderingSystem.
PreviewViewport::~PreviewViewport()
{
    // FIX: The new shutdown() takes no arguments.
    if (m_previewRenderSystem) {
        m_previewRenderSystem->shutdown();
    }
}

// We override initializeGL to set up our OWN rendering system.
void PreviewViewport::initializeGL()
{
    // The base class initializeGL still handles basic GL setup.
    QOpenGLWidget::initializeGL();
    if (m_renderingSystem) {
        m_renderingSystem->initialize(m_scene);
        m_renderingSystem->onViewportAdded(this);
    }
}

// Public API to load a robot into the private scene.
void PreviewViewport::loadRobotForPreview(const RobotDescription& description)
{
    if (!m_previewScene) return;
    clearPreview();
    SceneBuilder::spawnRobot(*m_previewScene, description);
    update();
}

// Helper to clear the scene.
void PreviewViewport::clearPreview()
{
    if (!m_previewScene) return;
    auto& registry = m_previewScene->getRegistry();

    // This is the robust way to destroy entities while iterating.
    // We collect them into a vector first to avoid invalidating the view's iterator.
    auto view = registry.view<entt::entity>();
    std::vector<entt::entity> to_destroy;
    for (auto entity : view) {
        if (entity != m_cameraEntity) {
            to_destroy.push_back(entity);
        }
    }
    registry.destroy(to_destroy.begin(), to_destroy.end());
}