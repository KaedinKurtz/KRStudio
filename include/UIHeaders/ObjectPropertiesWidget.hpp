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
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QToolButton;
class QPushButton;
class QFormLayout;
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

signals:
    // Phase B (C3): a rigid-body edit (esp. bodyType) must be LIVE-applied via
    // SimulationController::notifyEntityChanged -- otherwise it only materializes on the next
    // stop()/play(), which restores the authored pose (the "flip resets" symptom). MainWindow
    // routes this to the sim, mirroring PhysicsPropertiesWidget::entityComponentsChanged.
    void entityComponentsChanged(entt::entity entity);

public slots:
    /**
     * @brief Populates the entire panel with data from the given entity.
     * @param entity The entity to inspect. If entt::null, the panel will be cleared and disabled.
     */
    void setEntity(entt::entity entity);

    // --- Headless self-test hooks (KRS_LIGHTUI_SELFTEST): drive the REAL Light controls so a
    //     grab can pixel-verify the per-object lighting menu hot-updates the scene. ---
    void selfTestSelectLightTab();   // bring the Light tab to front (so it shows in a capture)
    void selfTestNudgeLight();       // change colour(Kelvin)+intensity via the real spinboxes
    void selfTestAddAndGlowEmitter();// run "Add Light Emitter" on the selection + crank a bright
                                     // coloured glow (proves emissive hot-update on TEXTURED bodies)

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

    // --- Light Tab Slots ---
    void onLightTypeChanged(int index);
    void onLightPropertyChanged();
    void onLightSizeChanged();   // RectArea Width/Height -> transform scale (separate so colour
                                 // edits never rewrite/reset the user's scale)
    void onLightColorPickerClicked();
    void onLightKelvinChanged(double kelvin);  // set colour from a blackbody temperature
    void onAddLightEmitterClicked();
    void onRemoveLightEmitterClicked();

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

    // --- Light tab (built in code, not from the .ui) ---
    void buildLightTab();                       // construct + add the "Light" tab once
    void populateLightTab();                    // reflect the selected entity's LightComponent
    void updateLightComponent();                // write the UI back to the LightComponent
    void updateLightFieldVisibility(int type);  // show only the rows relevant to the type
    void updateLightColorPreview();             // repaint the colour swatch from m_lightColor
    // For DEDICATED light primitives (LightEmitterTag) only: swap the visible mesh to match
    // the new type (sphere bulb <-> quad panel) and reset scale/orientation defaults. A no-op
    // for ordinary bodies that merely had a light added, so their mesh is never replaced.
    void rebuildEmitterMeshForType(entt::entity e, int type);

    // --- Component Update Helpers ---
    void updateTransformComponent();
    void updateRigidBodyComponent();
    void updateSoftBodyComponent();
    void updateMaterialComponent();

    // --- UI State Helpers ---
    void updateTransformInputs(const TransformComponent& transform);
    // Apply the user's display-units (Settings: units/*) to the transform spin
    // boxes: suffix, decimals and range for the length (position) and angle
    // (euler) fields. Called at init and whenever a units/* setting changes.
    void applyUnitFormatting();
    void setUIColor(const glm::vec3& color, QFrame* frame, QSpinBox* r, QSpinBox* g, QSpinBox* b);

    // Pointer to the Qt Designer UI class
    Ui::Form* ui;

    // Pointer to the scene, providing access to the entity registry
    Scene* m_scene;

    // The currently selected entity whose properties are being displayed
    entt::entity m_currentEntity;

    // Flag to prevent signal-slot feedback loops during programmatic UI updates
    bool m_isUpdatingUI;

    // --- Light tab widgets (created in buildLightTab) ---
    QWidget*        m_lightTab        = nullptr;  // the tab page
    QPushButton*    m_addLightBtn     = nullptr;  // shown when the entity is NOT a light
    QWidget*        m_lightControls   = nullptr;  // container for the editor (shown when it IS)
    QFormLayout*    m_lightForm       = nullptr;
    QComboBox*      m_lightTypeCombo  = nullptr;
    QCheckBox*      m_lightEnabled    = nullptr;
    QToolButton*    m_lightColorBtn   = nullptr;
    QFrame*         m_lightColorSwatch= nullptr;
    QDoubleSpinBox* m_lightKelvin     = nullptr;
    QDoubleSpinBox* m_lightIntensity  = nullptr;
    QDoubleSpinBox* m_lightRange      = nullptr;
    QDoubleSpinBox* m_lightInner      = nullptr;
    QDoubleSpinBox* m_lightOuter      = nullptr;
    QDoubleSpinBox* m_lightSizeX      = nullptr;
    QDoubleSpinBox* m_lightSizeY      = nullptr;
    QCheckBox*      m_lightTwoSided   = nullptr;
    QPushButton*    m_removeLightBtn  = nullptr;
    glm::vec3       m_lightColor      = glm::vec3(1.0f);  // backing store for the swatch

};
