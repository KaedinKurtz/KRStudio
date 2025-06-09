#pragma once

#include <QWidget>
#include <QGridLayout>
#include <QVBoxLayout>

#include <map>
#include <memory>
#include "entt/entt.hpp"

namespace Ui {
    class PropertiesPanel;
}
class Scene;


class PropertiesPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PropertiesPanel(Scene* scene, QWidget* parent = nullptr);
    ~PropertiesPanel();

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
};
