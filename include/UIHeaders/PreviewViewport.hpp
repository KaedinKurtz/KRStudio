#pragma once

#include "ViewportWidget.hpp"
#include "RobotDescription.hpp"
#include <memory> // For std::unique_ptr

// Forward declarations
class Scene;
class RenderingSystem;

class PreviewViewport : public ViewportWidget
{
    Q_OBJECT

public:
    explicit PreviewViewport(QWidget* parent = nullptr);
    ~PreviewViewport() override;

    void loadRobotForPreview(const RobotDescription& description);
    void clearPreview();

protected:
    void initializeGL() override;
    // REMOVE these lines. They are inherited from ViewportWidget.
    // Scene* m_scene = nullptr;
    // entt::entity m_cameraEntity = entt::null;
    // RenderingSystem* m_renderingSystem = nullptr;
    // QPoint m_lastMousePos;

private:
    // ADD these two lines for ownership.
    std::unique_ptr<Scene> m_previewScene;
    std::unique_ptr<RenderingSystem> m_previewRenderSystem;

    // These members from your file are fine.
    int m_instanceId = 0;
    static int s_instanceCounter;
};