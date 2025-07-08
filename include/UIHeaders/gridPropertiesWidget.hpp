#pragma once

#include <QWidget>
#include "gridPropertiesWidget.hpp"
#include "entt/entt.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp> // For quaternion operations
#include <glm/gtx/vector_angle.hpp> // For angle calculations
#include <glm/gtx/euler_angles.hpp> // For Euler angles conversions
#include <glm/gtx/transform.hpp> // For transformation matrices
#include <glm/gtx/string_cast.hpp> // For string conversions of glm types
#include <QColorDialog> // For color picking dialog
#include <QDebug> // For debugging output
#include <QWidget> // Base class for all UI widgets



namespace Ui {
    class gridPropertiesWidget; // Correct class name
}
class Scene;

class gridPropertiesWidget : public QWidget
{
    Q_OBJECT

public:
    explicit gridPropertiesWidget(Scene* scene, entt::entity entity, QWidget* parent = nullptr);
    ~gridPropertiesWidget();

private slots:
    // Slots to handle UI changes
    void onEulerChanged();
    void onQuaternionChanged();
    void onUnitSystemChanged();

private:
    void initializeUI();
    void setupConnections();
    void updateOrientationInputs(const glm::quat& rotation);
    void pickColor(glm::vec3& colorToUpdate);

    // This is the corrected line:
    Ui::gridPropertiesWidget* ui;

    Scene* m_scene;
    entt::entity m_entity;
    bool m_updating; // Flag to prevent signal-slot loops
};