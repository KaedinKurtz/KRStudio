#include "PropertiesPanel.hpp"
#include "ui_propertiespanel.h"
#include "Scene.hpp"
#include "components.hpp"
#include "gridPropertiesWidget.hpp"
#include <QAbstractButton>

PropertiesPanel::PropertiesPanel(Scene* scene, QWidget* parent) :
    QWidget(parent),
    ui(std::make_unique<Ui::PropertiesPanel>()),
    m_scene(scene)
{
    ui->setupUi(this);
    m_gridLayout = ui->gridLayout;

    // This is your original, correct signal/slot connection.
    m_scene->getRegistry().on_construct<GridComponent>().connect<&PropertiesPanel::onGridAdded>(this);
    m_scene->getRegistry().on_destroy<GridComponent>().connect<&PropertiesPanel::onGridRemoved>(this);

    connect(ui->addGridButton, &QAbstractButton::clicked, this, [this]() {
        auto& registry = m_scene->getRegistry();
        auto newGrid = registry.create();
        registry.emplace<TransformComponent>(newGrid);
        registry.emplace<GridComponent>(newGrid);
        });

    // This loop correctly handles grids that already exist when the panel is created.
    auto initialGridsView = m_scene->getRegistry().view<GridComponent>();
    for (auto entity : initialGridsView)
    {
        onGridAdded(m_scene->getRegistry(), entity);
    }
}

PropertiesPanel::~PropertiesPanel() = default;

void PropertiesPanel::onGridRemoved(entt::registry& registry, entt::entity entity)
{
    if (m_entityWidgetMap.count(entity)) {
        QWidget* widget = m_entityWidgetMap[entity];
        m_entityWidgetMap.erase(entity);
        m_gridLayout->removeWidget(widget);
        widget->deleteLater();
    }
}

void PropertiesPanel::ensureGridIsInitialized(entt::entity gridEntity)
{
    auto& registry = m_scene->getRegistry();
    if (!registry.valid(gridEntity)) return;

    if (!registry.all_of<TagComponent>(gridEntity)) {
        static int gridCount = 1;
        registry.emplace<TagComponent>(gridEntity, "Grid " + std::to_string(gridCount++));
    }

    auto& gridComp = registry.get<GridComponent>(gridEntity);
    if (gridComp.levels.empty()) {
        // FINAL FIX: Use parentheses () for the glm::vec3 constructor, not curly braces {}.
        gridComp.levels.emplace_back(0.001f, glm::vec3(0.5f, 0.5f, 0.5f), 0.0f, 0.5f);
        gridComp.levels.emplace_back(0.01f, glm::vec3(0.5f, 0.5f, 0.5f), 0.5f, 2.0f);
        gridComp.levels.emplace_back(0.1f, glm::vec3(0.5f, 0.5f, 0.5f), 2.0f, 10.0f);
        gridComp.levels.emplace_back(1.0f, glm::vec3(0.5f, 0.5f, 0.5f), 10.0f, 50.0f);
        gridComp.levels.emplace_back(10.0f, glm::vec3(0.5f, 0.5f, 0.5f), 50.0f, 200.0f);
    }
}

// This function correctly calls the two helper methods.
void PropertiesPanel::onGridAdded(entt::registry& registry, entt::entity entity)
{
    ensureGridIsInitialized(entity);
    addGridEditor(entity);
}

// This function remains unchanged.
void PropertiesPanel::addGridEditor(entt::entity entity)
{
    if (!m_scene->getRegistry().valid(entity)) return;
    gridPropertiesWidget* widget = new gridPropertiesWidget(m_scene, entity, this);
    m_gridLayout->addWidget(widget);
    m_entityWidgetMap[entity] = widget;
}