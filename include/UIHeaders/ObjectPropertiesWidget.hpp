#pragma once

#include <QWidget>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "Scene.hpp" // Include the Scene class for entity management

#include "IMenu.hpp" // Include the interface for menu management

// Forward declarations to keep the header clean and reduce compile times.
namespace Ui {
    class Form; // The class name from ui_ObjectProperties.h is Ui_Form
}

namespace entt {
    template<typename>
    class sink;
}

class QFrame;     // <-- ADD THIS
class QSpinBox;
struct TransformComponent;
struct RigidBodyComponent;
struct SoftBodyComponent;
struct MaterialComponent;
struct PhysicsMaterial;

/**
 * @class ObjectPropertiesWidget
 * @brief A comprehensive widget for displaying and editing all properties of a selected scene entity.
 *
 * This panel is the central hub for modifying an object's transformation, physics behaviors
 * (both rigid and soft body), and its complex physical and visual material properties.
 * It is designed to be docked in the main window and updates dynamically based on the
 * entity selected in the scene outliner.
 */
class ObjectPropertiesWidget : public QWidget, public IMenu
{
    Q_OBJECT

public:
    /**
     * @brief Constructs the properties widget.
     * @param scene A pointer to the main scene object, used to access the entity registry.
     * @param parent The parent widget, typically the MainWindow.
     */
    explicit ObjectPropertiesWidget(Scene* scene, QWidget* parent = nullptr);

    /**
     * @brief Destructor.
     */
    ~ObjectPropertiesWidget() override;

    // --- IMenu Interface Implementation ---
    void initializeFresh() override;
    void initializeFromDatabase() override;
    void shutdownAndSave() override;
    QWidget* widget() override { return this; }


public slots:
    /**
     * @brief Populates the entire panel with data from the given entity.
     * @param entity The entity to inspect. If entt::null, the panel will be cleared and disabled.
     */
    void setEntity(entt::entity entity);

private slots:
    // --- Scene Outliner Slots ---
    void onSceneSelectionChanged();

    // --- Object Tab Slots ---
    void onNameChanged(const QString& newName);
    void onTransformChanged();
    void onRotationTypeChanged(int index);

    // --- Physics Tab Slots ---
    void onRigidBodyPropertyChanged();
    void onSoftBodyPropertyChanged();
    void onSoftBodyToggled(bool checked);

    // --- Material Tab Slots ---
    void onMaterialSelectionChanged();
    void onPhysicalPropertiesChanged();
    void onAppearancePropertiesChanged();
    void onAlbedoColorPickerClicked();
    void onEmissiveColorPickerClicked();
    void onDisplacementTypeChanged(int index);

private:
    // --- Initialization Helpers ---
    void initializeUI();
    void setupConnections();

    // --- UI Population Helpers ---
    void populateAllTabs();
    void populateObjectTab();
    void populatePhysicsTab();
    void populateMaterialTab();
    void populateAppearanceTab(const MaterialComponent& mat);
    void updateDerivedMechanicalProperties();

    // --- Component Update Helpers ---
    void updateTransformComponent();
    void updateRigidBodyComponent();
    void updateSoftBodyComponent();
    void updateMaterialComponent();

    // --- UI State Helpers ---
    void updateTransformInputs(const TransformComponent& transform);
    void setUIColor(const glm::vec3& color, QFrame* frame, QSpinBox* r, QSpinBox* g, QSpinBox* b);

    // Pointer to the Qt Designer UI class
    Ui::Form* ui;

    // Pointer to the scene, providing access to the entity registry
    Scene* m_scene;

    // The currently selected entity whose properties are being displayed
    entt::entity m_currentEntity;

    // Flag to prevent signal-slot feedback loops during programmatic UI updates
    bool m_isUpdatingUI;

};
