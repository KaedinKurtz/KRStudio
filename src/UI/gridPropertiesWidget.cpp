#include "gridPropertiesWidget.hpp"
#include "ui_gridPropertiesWidget.h"
#include "Scene.hpp"
#include "components.hpp"
#include "DatabaseManager.hpp"
#include "IMenu.hpp"
#include "MenuFactory.hpp"

#include <QColorDialog>
#include <QSignalBlocker>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QToolButton>
#include <QFrame>
#include <QLabel>
#include <QGroupBox>
#include <QOverload>
#include <QJsonObject>
#include <QJsonDocument>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>


gridPropertiesWidget::gridPropertiesWidget(Scene* scene, QWidget* parent)
    : gridPropertiesWidget(scene, entt::null, parent)
{
}

gridPropertiesWidget::gridPropertiesWidget(Scene* scene,
    entt::entity entity,
    QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::gridPropertiesWidget)
    , m_scene(scene)
    , m_entity(entity)
    , m_updating(false)
{
    ui->setupUi(this);

    // --- UI Element Setup ---
    ui->originInputX->setDecimals(4);
    ui->originInputY->setDecimals(4);
    ui->originInputZ->setDecimals(4);
    ui->originInputX->setRange(-9999.0, 9999.0);
    ui->originInputY->setRange(-9999.0, 9999.0);
    ui->originInputZ->setRange(-9999.0, 9999.0);

    ui->angleInputEulerX->setDecimals(2);
    ui->angleInputEulerY->setDecimals(2);
    ui->angleInputEulerZ->setDecimals(2);
    ui->angleInputEulerX->setWrapping(false);
    ui->angleInputEulerY->setWrapping(false);
    ui->angleInputEulerZ->setWrapping(false);
    ui->angleInputEulerX->setRange(-10000.0, 10000.0);
    ui->angleInputEulerY->setRange(-10000.0, 10000.0);
    ui->angleInputEulerZ->setRange(-10000.0, 10000.0);

    ui->lineThicknessBox->setRange(0.0, 5.0);
    ui->lineThicknessBox->setSingleStep(0.01);
    ui->lineThicknessBox->setDecimals(2);

    ui->visualizationCombo->clear();
    ui->visualizationCombo->addItems({ "Lines", "Dots" });
    ui->gridSnapToggleButton->setCheckable(true);

    setupConnections();
    initializeUI();
}

gridPropertiesWidget::~gridPropertiesWidget()
{
    delete ui;
}

void gridPropertiesWidget::initializeUI()
{
    QSignalBlocker blocker(this);
    if (!m_scene->getRegistry().valid(m_entity)) return;

    auto& grid = m_scene->getRegistry().get<GridComponent>(m_entity);
    auto& tag = m_scene->getRegistry().get<TagComponent>(m_entity);

    ui->gridNameInput->setText(QString::fromStdString(tag.tag));
    ui->masterVisibilityCheck->setChecked(grid.masterVisible);
    ui->lineThicknessBox->setValue(grid.baseLineWidthPixels);
    ui->visualizationCombo->setCurrentIndex(grid.isDotted ? 1 : 0);
    ui->gridSnapToggleButton->setChecked(grid.snappingEnabled);
    onUnitSystemChanged();
}

void gridPropertiesWidget::setupConnections()
{
    connect(ui->deleteButton, &QToolButton::clicked, this, [this]() {
        if (m_scene->getRegistry().valid(m_entity)) {
            m_scene->getRegistry().destroy(m_entity);
        }
        });

    connect(ui->gridNameInput, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (m_scene->getRegistry().valid(m_entity)) {
            m_scene->getRegistry().get<TagComponent>(m_entity).tag = text.toStdString();
        }
        });

    connect(ui->unitInputBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &gridPropertiesWidget::onUnitSystemChanged);

    connect(ui->lineThicknessBox, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (m_scene->getRegistry().valid(m_entity)) {
            m_scene->getRegistry().get<GridComponent>(m_entity).baseLineWidthPixels = static_cast<float>(value);
        }
        });

    connect(ui->visualizationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_scene->getRegistry().valid(m_entity)) {
            m_scene->getRegistry().get<GridComponent>(m_entity).isDotted = (index == 1);
        }
        });

    connect(ui->gridSnapToggleButton, &QToolButton::toggled, this, [this](bool checked) {
        if (m_scene->getRegistry().valid(m_entity)) {
            m_scene->getRegistry().get<GridComponent>(m_entity).snappingEnabled = checked;
        }
        });

    connect(ui->masterVisibilityCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_scene->getRegistry().valid(m_entity)) {
            m_scene->getRegistry().get<GridComponent>(m_entity).masterVisible = checked;
        }
        });

    auto connect_axis_color = [&](QToolButton* button, QFrame* frame, bool is_x_axis) {
        connect(button, &QToolButton::clicked, this, [this, frame, is_x_axis]() {
            if (!m_scene->getRegistry().valid(m_entity)) return;
            auto& grid = m_scene->getRegistry().get<GridComponent>(m_entity);
            glm::vec3& color_vec = is_x_axis ? grid.xAxisColor : grid.zAxisColor;

            QColor initialColor(color_vec.r * 255, color_vec.g * 255, color_vec.b * 255);
            QColor newColor = QColorDialog::getColor(initialColor, this, "Select Axis Color");
            if (newColor.isValid()) {
                color_vec = { newColor.redF(), newColor.greenF(), newColor.blueF() };
                frame->setStyleSheet(QString("background-color: %1;").arg(newColor.name()));
            }
            });
        };
    connect_axis_color(ui->xAxisColorButton, ui->xAxisColorFrame, true);
    connect_axis_color(ui->zAxisColorButton, ui->zAxisColorFrame, false);

    QCheckBox* level_vis_checks[] = { ui->visibilityCheck1, ui->visibilityCheck2, ui->visibilityCheck3, ui->visibilityCheck4, ui->visibilityCheck5 };
    QToolButton* level_color_buttons[] = { ui->colorInput1, ui->colorInput2, ui->colorInput3, ui->colorInput4, ui->colorInput5 };
    QFrame* level_color_frames[] = { ui->level1ColorFrame, ui->level2ColorFrame, ui->level3ColorFrame, ui->level4ColorFrame, ui->level5ColorFrame };

    for (int i = 0; i < 5; ++i) {
        if (level_vis_checks[i]) {
            connect(level_vis_checks[i], &QCheckBox::toggled, this, [this, i](bool checked) {
                if (m_scene->getRegistry().valid(m_entity)) {
                    m_scene->getRegistry().get<GridComponent>(m_entity).levelVisible[i] = checked;
                }
                });
        }
        if (level_color_buttons[i] && level_color_frames[i]) {
            connect(level_color_buttons[i], &QToolButton::clicked, this, [this, frame = level_color_frames[i], i]() {
                if (!m_scene->getRegistry().valid(m_entity)) return;
                auto& grid = m_scene->getRegistry().get<GridComponent>(m_entity);
                if (static_cast<size_t>(i) < grid.levels.size()) {
                    glm::vec3& color_ref = grid.levels[i].color;
                    QColor initialColor(color_ref.r * 255, color_ref.g * 255, color_ref.b * 255);
                    QColor newColor = QColorDialog::getColor(initialColor, this, "Select Level Color");
                    if (newColor.isValid()) {
                        color_ref = { newColor.redF(), newColor.greenF(), newColor.blueF() };
                        frame->setStyleSheet(QString("background-color: %1;").arg(newColor.name()));
                    }
                }
                });
        }
    }

    auto connect_origin = [&](QDoubleSpinBox* spinbox, int component_index) {
        connect(spinbox, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, component_index](double value) {
            if (m_updating) return;
            if (!m_scene->getRegistry().valid(m_entity)) return;
            auto& transform = m_scene->getRegistry().get<TransformComponent>(m_entity);
            auto& grid = m_scene->getRegistry().get<GridComponent>(m_entity);
            float final_value = grid.isMetric ? value : value * 0.0254f;

            if (component_index == 0) transform.translation.x = final_value;
            else if (component_index == 1) transform.translation.y = final_value;
            else if (component_index == 2) transform.translation.z = final_value;
            });
        };
    connect_origin(ui->originInputX, 0);
    connect_origin(ui->originInputY, 1);
    connect_origin(ui->originInputZ, 2);

    connect(ui->angleInputEulerX, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &gridPropertiesWidget::onEulerChanged);
    connect(ui->angleInputEulerY, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &gridPropertiesWidget::onEulerChanged);
    connect(ui->angleInputEulerZ, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &gridPropertiesWidget::onEulerChanged);
    connect(ui->angleInputQuatW, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &gridPropertiesWidget::onQuaternionChanged);
    connect(ui->angleInputQuatX, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &gridPropertiesWidget::onQuaternionChanged);
    connect(ui->angleInputQuatY, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &gridPropertiesWidget::onQuaternionChanged);
    connect(ui->angleInputQuatZ, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &gridPropertiesWidget::onQuaternionChanged);
}

void gridPropertiesWidget::onUnitSystemChanged()
{
    m_updating = true;
    if (!m_scene->getRegistry().valid(m_entity)) { m_updating = false; return; }

    auto& grid = m_scene->getRegistry().get<GridComponent>(m_entity);
    auto& transform = m_scene->getRegistry().get<TransformComponent>(m_entity);
    grid.isMetric = (ui->unitInputBox->currentIndex() == 0);

    const char* imperial_labels[] = { "10 thou", "100 thou", "1/2\"", "1\"", "1'" };
    const float imperial_spacing[] = { 0.000254f, 0.00254f, 0.0127f, 0.0254f, 0.3048f };
    const char* metric_labels[] = { "1 mm", "1 cm", "10 cm", "1 m", "10 m" };
    const float metric_spacing[] = { 0.001f, 0.01f, 0.1f, 1.0f, 10.0f };

    QLabel* unit_labels[] = { ui->level1UnitLabel, ui->level2UnitLabel, ui->level3UnitLabel, ui->level4UnitLabel, ui->level5UnitLabel };

    if (grid.isMetric) {
        for (int i = 0; i < 5; ++i) {
            if (unit_labels[i]) unit_labels[i]->setText(metric_labels[i]);
            if (static_cast<size_t>(i) < grid.levels.size()) grid.levels[i].spacing = metric_spacing[i];
        }
        ui->originInputX->setValue(transform.translation.x);
        ui->originInputY->setValue(transform.translation.y);
        ui->originInputZ->setValue(transform.translation.z);
    }
    else {
        for (int i = 0; i < 5; ++i) {
            if (unit_labels[i]) unit_labels[i]->setText(imperial_labels[i]);
            if (static_cast<size_t>(i) < grid.levels.size()) grid.levels[i].spacing = imperial_spacing[i];
        }
        ui->originInputX->setValue(transform.translation.x / 0.0254);
        ui->originInputY->setValue(transform.translation.y / 0.0254);
        ui->originInputZ->setValue(transform.translation.z / 0.0254);
    }

    QCheckBox* level_vis_checks[] = { ui->visibilityCheck1, ui->visibilityCheck2, ui->visibilityCheck3, ui->visibilityCheck4, ui->visibilityCheck5 };
    QFrame* level_color_frames[] = { ui->level1ColorFrame, ui->level2ColorFrame, ui->level3ColorFrame, ui->level4ColorFrame, ui->level5ColorFrame };
    for (int i = 0; i < 5; ++i) {
        if (level_vis_checks[i]) {
            level_vis_checks[i]->setChecked(grid.levelVisible[i]);
        }
        if (level_color_frames[i] && static_cast<size_t>(i) < grid.levels.size()) {
            QColor initialColor(grid.levels[i].color.r * 255, grid.levels[i].color.g * 255, grid.levels[i].color.b * 255);
            level_color_frames[i]->setStyleSheet(QString("background-color: %1;").arg(initialColor.name()));
        }
    }

    QColor xColor(grid.xAxisColor.r * 255, grid.xAxisColor.g * 255, grid.xAxisColor.b * 255);
    ui->xAxisColorFrame->setStyleSheet(QString("background-color: %1;").arg(xColor.name()));
    QColor zColor(grid.zAxisColor.r * 255, grid.zAxisColor.g * 255, grid.zAxisColor.b * 255);
    ui->zAxisColorFrame->setStyleSheet(QString("background-color: %1;").arg(zColor.name()));

    updateOrientationInputs(transform.rotation);
    m_updating = false;
}

void gridPropertiesWidget::updateOrientationInputs(const glm::quat& rotation)
{
    if (m_updating) return;
    m_updating = true;

    ui->angleInputQuatW->setValue(rotation.w);
    ui->angleInputQuatX->setValue(rotation.x);
    ui->angleInputQuatY->setValue(rotation.y);
    ui->angleInputQuatZ->setValue(rotation.z);

    glm::vec3 euler = glm::degrees(glm::eulerAngles(rotation));
    ui->angleInputEulerX->setValue(euler.x);
    ui->angleInputEulerY->setValue(euler.y);
    ui->angleInputEulerZ->setValue(euler.z);

    m_updating = false;
}

void gridPropertiesWidget::onEulerChanged()
{
    if (m_updating) return;
    if (!m_scene->getRegistry().valid(m_entity)) return;

    glm::vec3 euler_deg(
        ui->angleInputEulerX->value(),
        ui->angleInputEulerY->value(),
        ui->angleInputEulerZ->value()
    );
    glm::quat new_rotation = glm::quat(glm::radians(euler_deg));

    m_scene->getRegistry().get<TransformComponent>(m_entity).rotation = new_rotation;
    updateOrientationInputs(new_rotation);
}

void gridPropertiesWidget::onQuaternionChanged()
{
    if (m_updating) return;
    if (!m_scene->getRegistry().valid(m_entity)) return;

    glm::quat new_rotation = glm::normalize(glm::quat(
        (float)ui->angleInputQuatW->value(),
        (float)ui->angleInputQuatX->value(),
        (float)ui->angleInputQuatY->value(),
        (float)ui->angleInputQuatZ->value()
    ));

    m_scene->getRegistry().get<TransformComponent>(m_entity).rotation = new_rotation;
    updateOrientationInputs(new_rotation);
}

void gridPropertiesWidget::initializeFresh()
{
    // if no entity was passed, or it was null, make/find one now
    if (m_entity == entt::null || !m_scene->getRegistry().valid(m_entity))
    {
        auto& reg = m_scene->getRegistry();
        auto view = reg.view<GridComponent>();
        if (view.begin() != view.end())
            m_entity = *view.begin();
        else
        {
            m_entity = reg.create();
            reg.emplace<TransformComponent>(m_entity);
            reg.emplace<GridComponent>(m_entity);
        }
    }
    // now populate the UI from that component:
    initializeUI();
}

void gridPropertiesWidget::initializeFromDatabase()
{
    // Attempt to load previous UI state
    QString blob = db::DatabaseManager::instance()
        .loadMenuState(db::DatabaseManager::menuTypeToString(MenuType::GridProperties));
    if (blob.isEmpty()) {
        // no saved state -> fresh UI
        initializeUI();
        return;
    }

    // parse JSON
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(blob.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "gridPropertiesWidget: failed to parse saved state:"
            << parseError.errorString();
        initializeUI();
        return;
    }
    QJsonObject obj = doc.object();

    // Restore each control (add any additional keys you need)
    ui->gridNameInput->setText(obj.value("gridName").toString());
    ui->masterVisibilityCheck->setChecked(obj.value("masterVisible").toBool());
    ui->lineThicknessBox->setValue(obj.value("lineThickness").toDouble());
    ui->visualizationCombo->setCurrentIndex(obj.value("dotted").toBool() ? 1 : 0);
    ui->gridSnapToggleButton->setChecked(obj.value("snapEnabled").toBool());

    // and update scene if needed
}

void gridPropertiesWidget::shutdownAndSave()
{

    QJsonObject obj;
    obj["gridName"] = ui->gridNameInput->text();
    obj["masterVisible"] = ui->masterVisibilityCheck->isChecked();
    obj["lineThickness"] = ui->lineThicknessBox->value();
    obj["dotted"] = (ui->visualizationCombo->currentIndex() == 1);
    obj["snapEnabled"] = ui->gridSnapToggleButton->isChecked();
        // …collect colour/spacing fields as needed…
        
        QJsonDocument doc(obj);
        db::DatabaseManager::instance().saveMenuState("GridProperties", doc.toJson());
}