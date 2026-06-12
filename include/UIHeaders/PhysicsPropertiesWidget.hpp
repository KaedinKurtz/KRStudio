#pragma once

#include <QWidget>
#include <entt/entt.hpp>

class Scene;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QStackedWidget;

/**
 * @brief Blender-style per-object physics properties: rigid body settings,
 * collision shape, and fluid roles (emitter / volume) for the selected
 * entity. Edits apply straight to the ECS components.
 */
class PhysicsPropertiesWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PhysicsPropertiesWidget(Scene* scene, QWidget* parent = nullptr);

public slots:
    /// Point the panel at an entity (entt::null clears it).
    void setEntity(entt::entity entity);

private slots:
    void applyRigidBody();
    void applyCollider();
    void applyEmitter();
    void applyVolume();

private:
    void rebuildFromEntity();
    QWidget* buildRigidBodySection();
    QWidget* buildColliderSection();
    QWidget* buildFluidSection();

    Scene* m_scene = nullptr;
    entt::entity m_entity = entt::null;
    bool m_updating = false; // guards programmatic widget updates

    // header
    QLabel* m_objectName = nullptr;

    // rigid body
    QCheckBox* m_rbEnabled = nullptr;
    QComboBox* m_rbType = nullptr;
    QDoubleSpinBox* m_rbMass = nullptr;
    QDoubleSpinBox* m_rbLinDamp = nullptr;
    QDoubleSpinBox* m_rbAngDamp = nullptr;
    QDoubleSpinBox* m_rbFriction = nullptr;
    QDoubleSpinBox* m_rbRestitution = nullptr;

    // collider
    QComboBox* m_colShape = nullptr;
    QStackedWidget* m_colStack = nullptr;
    QDoubleSpinBox* m_colBoxX = nullptr;
    QDoubleSpinBox* m_colBoxY = nullptr;
    QDoubleSpinBox* m_colBoxZ = nullptr;
    QDoubleSpinBox* m_colSphereRadius = nullptr;
    QDoubleSpinBox* m_colCapRadius = nullptr;
    QDoubleSpinBox* m_colCapHeight = nullptr;
    QPushButton* m_colAutoFit = nullptr;

    // fluid
    QCheckBox* m_emEnabled = nullptr;
    QDoubleSpinBox* m_emRate = nullptr;
    QDoubleSpinBox* m_emSpeed = nullptr;
    QDoubleSpinBox* m_emSpread = nullptr;
    QDoubleSpinBox* m_emRadius = nullptr;
    QDoubleSpinBox* m_emLifetime = nullptr;
    QCheckBox* m_volEnabled = nullptr;
    QDoubleSpinBox* m_volX = nullptr;
    QDoubleSpinBox* m_volY = nullptr;
    QDoubleSpinBox* m_volZ = nullptr;
    QDoubleSpinBox* m_volSpacing = nullptr;
};
