#pragma once

#include <QWidget>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <map>
#include <memory>
#include "entt/entt.hpp"
#include "IMenu.hpp" // Assuming IMenu is defined in this header
#include <glm/glm.hpp>
#include "Scene.hpp"

namespace Ui {
    class PropertiesPanel;
}
class Scene;


class PropertiesPanel : public QWidget, public IMenu
{
    Q_OBJECT

public:
    explicit PropertiesPanel(Scene* scene, QWidget* parent = nullptr);
    ~PropertiesPanel();
    // IMenu:
    void initializeFresh() override;
    void initializeFromDatabase() override;
    void shutdownAndSave() override;

    QWidget* widget() override { return this; }

    void disconnectRegistry();

    // Public method to clear all grid widgets (used for scene reload)
    void clearAllGrids();

private slots:
    void onGridAdded(entt::registry& registry, entt::entity entity);
    void onGridRemoved(entt::registry& registry, entt::entity entity);

private:
    void ensureGridIsInitialized(entt::entity gridEntity);
    void addGridEditor(entt::entity entity);

    std::unique_ptr<Ui::PropertiesPanel> ui;

    Scene* m_scene;

    QVBoxLayout* m_gridLayout;

    std::map<entt::entity, QWidget*> m_entityWidgetMap;

    entt::entity         m_gridEntity = entt::null;

    static QJsonArray           toJsonColor(const glm::vec3& c);
    static glm::vec3            fromJsonColor(const QJsonArray& a);
};
