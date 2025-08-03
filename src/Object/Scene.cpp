#include "Scene.hpp"
#include <QDebug>

Scene::Scene()
{
    // The constructor can be empty for now.
    // We will create entities in ViewportWidget::initializeGL.
    ////qDebug() << ">>>>>> Scene constructed at:" << this;
}

Scene::~Scene()
{
    ////qDebug() << "[LIFETIME] Scene Destructor ~Scene() called. The registry is now gone.";
}

void Scene::setPrimaryCamera(entt::entity camera)
{
    m_primaryCamera = camera;
}

entt::entity Scene::getPrimaryCamera() const
{
    return m_primaryCamera;
}