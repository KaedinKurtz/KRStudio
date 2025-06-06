#include "PropertiesPanel.hpp"
#include "ui_propertiespanel.h"

#include "Scene.hpp"
#include "components.hpp"

#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QColorDialog>
#include <QAbstractButton>
#include <QSpacerItem>


PropertiesPanel::PropertiesPanel(Scene* scene, QWidget* parent) :
    QWidget(parent),
    ui(std::make_unique<Ui::PropertiesPanel>()),
    m_scene(scene)
{
    ui->setupUi(this);

    // This line correctly finds the QVBoxLayout named "gridLayout" inside your UI.
    m_gridLayout = ui->gridLayout;

    m_scene->getRegistry().on_construct<GridComponent>().connect<&PropertiesPanel::onGridAdded>(this);

    connect(ui->addGridButton, &QAbstractButton::clicked, this, [this]() {
        auto& registry = m_scene->getRegistry();
        auto newGrid = registry.create();

        static int gridCount = 2;
        registry.emplace<TagComponent>(newGrid, "Grid " + std::to_string(gridCount++));
        registry.emplace<TransformComponent>(newGrid);
        registry.emplace<GridComponent>(newGrid);
        });

    auto initialGrids = m_scene->getRegistry().view<GridComponent>();
    for (auto entity : initialGrids) {
        onGridAdded(m_scene->getRegistry(), entity);
    }
}

PropertiesPanel::~PropertiesPanel() = default;


void PropertiesPanel::onGridAdded(entt::registry& registry, entt::entity entity)
{
    addGridEditor(entity);
}

void PropertiesPanel::addGridEditor(entt::entity entity)
{
    if (!m_scene->getRegistry().valid(entity) || !m_scene->getRegistry().all_of<TagComponent>(entity))
    {
        return;
    }

    auto& registry = m_scene->getRegistry();
    auto& tag = registry.get<TagComponent>(entity);

    QGroupBox* groupBox = new QGroupBox(QString::fromStdString(tag.tag), this);
    groupBox->setCheckable(true);

    if (registry.all_of<GridComponent>(entity)) {
        groupBox->setChecked(registry.get<GridComponent>(entity).visible);
    }

    connect(groupBox, &QGroupBox::toggled, this, [entity, &registry](bool checked) {
        if (registry.valid(entity) && registry.all_of<GridComponent>(entity)) {
            auto& gridComp = registry.get<GridComponent>(entity);
            gridComp.visible = checked;
        }
        });

    QVBoxLayout* layout = new QVBoxLayout(groupBox);



    // --- Origin Controls ---
    if (registry.all_of<TransformComponent>(entity)) {
        auto& transform = registry.get<TransformComponent>(entity);
        QHBoxLayout* originLayout = new QHBoxLayout();
        originLayout->addWidget(new QLabel("Origin X:"));
        QDoubleSpinBox* xSpin = new QDoubleSpinBox();
        xSpin->setRange(-1000, 1000);
        xSpin->setValue(transform.translation.x);
        originLayout->addWidget(xSpin);
        layout->addLayout(originLayout);

        connect(xSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [entity, &registry](double value) {
            if (registry.valid(entity) && registry.all_of<TransformComponent>(entity)) {
                auto& transformToUpdate = registry.get<TransformComponent>(entity);
                transformToUpdate.translation.x = value;
            }
            });
    }

    // --- Color Controls ---
    if (registry.all_of<GridComponent>(entity) && !registry.get<GridComponent>(entity).levels.empty()) {
        QPushButton* colorButton = new QPushButton("Level 1 Color");
        layout->addWidget(colorButton);

        connect(colorButton, &QPushButton::clicked, this, [this, entity, &registry]() {
            if (!registry.valid(entity) || !registry.all_of<GridComponent>(entity)) return;

            auto& gridComp = registry.get<GridComponent>(entity);
            if (gridComp.levels.empty()) return;

            QColor initialColor(gridComp.levels[0].color.r * 255, gridComp.levels[0].color.g * 255, gridComp.levels[0].color.b * 255);
            QColor newColor = QColorDialog::getColor(initialColor, this, "Select Color");

            if (newColor.isValid()) {
                auto& gridToUpdate = registry.get<GridComponent>(entity);
                gridToUpdate.levels[0].color.r = newColor.redF();
                gridToUpdate.levels[0].color.g = newColor.greenF();
                gridToUpdate.levels[0].color.b = newColor.blueF();
            }
            });
    }

    m_gridLayout->insertWidget(0, groupBox);
    m_entityWidgetMap[entity] = groupBox;
}
