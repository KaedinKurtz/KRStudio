#pragma once

#include <QWidget>
#include <QColor>
#include <QMap>
#include <glm/glm.hpp>
#include "components.hpp" // For FieldVisualizerComponent and its enums

// Include all necessary Qt headers to be safe with MOC
#include <QSlider>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QLinearGradient>
#include <QButtonGroup>

class PreviewViewport;

namespace Ui {
    class FlowVisualizerMenu;
}

class FlowVisualizerMenu : public QWidget
{
    Q_OBJECT

public:
    explicit FlowVisualizerMenu(QWidget* parent = nullptr);
    ~FlowVisualizerMenu();

    void updateControlsFromComponent(const FieldVisualizerComponent& component);

    void commitToComponent(FieldVisualizerComponent& vis);        // <-- defined in .cpp
    std::vector<ColorStop> getGradientFromTable(QTableWidget*) const;

    // --- PUBLIC GETTERS FOR MAINWINDOW ---
    bool isMasterVisible() const;
    glm::vec3 getFieldPosition() const;
    glm::vec3 getFieldOrientation() const;
    AABB getBounds() const;
    glm::vec3 getCentre() const; ///< NEW
    glm::vec3 getEuler()  const; ///< NEW (deg)
    FieldVisualizerComponent::DisplayMode getDisplayMode() const;

    // Static Arrow Getters
    glm::ivec3 getArrowDensity() const;
    float getArrowBaseSize() const;
    float getArrowHeadScale() const;
    float getArrowIntensityMultiplier() const;
    float getArrowCullingThreshold() const;
    bool isArrowLengthScaled() const;
    float getArrowLengthScaleMultiplier() const;
    bool isArrowThicknessScaled() const;
    float getArrowThicknessScaleMultiplier() const;
    FieldVisualizerComponent::ColoringMode getArrowColoringMode() const;
    glm::vec4 getArrowDirColor(int index) const;
    std::vector<ColorStop> getArrowIntensityGradient() const;

    // Flow Getters
    int getFlowParticleCount() const;
    float getFlowLifetime() const;
    float getFlowBaseSpeed() const;
    float getFlowSpeedIntensityMult() const;
    float getFlowBaseSize() const;
    float getFlowHeadScale() const;
    float getFlowPeakSizeMult() const;
    float getFlowMinSize() const;
    float getFlowGrowthPercent() const;
    float getFlowShrinkPercent() const;
    float getFlowRandomWalk() const;
    bool isFlowLengthScaled() const;
    float getFlowLengthScaleMultiplier() const;
    bool isFlowThicknessScaled() const;
    float getFlowThicknessScaleMultiplier() const;
    FieldVisualizerComponent::ColoringMode getFlowColoringMode() const;

    // Particle Getters
    bool isParticleSolid() const;
    int getParticleCount() const;
    float getParticleLifetime() const;
    float getParticleBaseSpeed() const;
    float getParticleSpeedIntensityMult() const;
    float getParticleBaseSize() const;
    float getParticlePeakSizeMult() const;
    float getParticleMinSize() const;
    float getParticleBaseGlow() const;
    float getParticlePeakGlowMult() const;
    float getParticleMinGlow() const;
    float getParticleRandomWalk() const;
    FieldVisualizerComponent::ColoringMode getParticleColoringMode() const;
    glm::vec4 getParticleDirColor(int index) const;
    std::vector<ColorStop> getParticleIntensityGradient() const;
    std::vector<ColorStop> getParticleLifetimeGradient() const;
    Ui::FlowVisualizerMenu* getUi() { return ui; }
    void updateGradientPreviewFromTable(QLabel* previewLabel, QTableWidget* table);

public slots:
    void onSettingChanged();

Q_SIGNALS:
    void settingsChanged();
	void transformChanged();
    void testViewportRequested();

private slots:
    void onMasterVisibilityChanged(bool checked);
    void onResetVisualizerClicked();
    void onVisualizationTypeChanged(int index);
    void onBoundaryTypeChanged(int index);
    void onStaticColoringStyleChanged(int index);
    void onStaticDirectionalColorClicked();
    void onStaticAddColorStop();
    void onStaticRemoveColorStop();
    void onStaticColorStopTableChanged();
    void onDynamicColoringStyleChanged(int index);
    void onDynamicDirectionalColorClicked();
    void onDynamicAddIntensityStop();
    void onDynamicRemoveIntensityStop();
    void onDynamicIntensityTableChanged();
    void onDynamicAddLifetimeStop();
    void onDynamicRemoveLifetimeStop();
    void onDynamicLifetimeTableChanged();
    void onParticleTypeToggleChanged();
    void onParticleColoringStyleChanged(int index);
    void onParticleDirectionalColorClicked();
    void onParticleAddIntensityStop();
    void onParticleRemoveIntensityStop();
    void onParticleIntensityTableChanged();
    void onParticleAddLifetimeStop();
    void onParticleRemoveLifetimeStop();
    void onParticleLifetimeTableChanged();
    

private:
    void initializeState();
    void setupConnections();
    void linkSliderAndSpinBox(QSlider* slider, QDoubleSpinBox* spinBox, double scale = 100.0);
    void linkSliderAndSpinBox(QSlider* slider, QSpinBox* spinBox);
    void pickColorForButton(QPushButton* button, QColor& colorMember);
    void updateAxisGradientPreview(QLabel* label, const QColor& negColor, const QColor& posColor);
    
    void setupColorButtonMap();
    glm::vec4 qColorToGlm(const QColor& color) const;

    Ui::FlowVisualizerMenu* ui;
    QMap<QPushButton*, QColor*> m_colorButtonMap;
    QColor m_staticXPos, m_staticXNeg, m_staticYPos, m_staticYNeg, m_staticZPos, m_staticZNeg;
    QColor m_dynamicXPos, m_dynamicXNeg, m_dynamicYPos, m_dynamicYNeg, m_dynamicZPos, m_dynamicZNeg;
    QColor m_particleXPos, m_particleXNeg, m_particleYPos, m_particleYNeg, m_particleZPos, m_particleZNeg;
    PreviewViewport* m_staticPreview;
    PreviewViewport* m_dynamicPreview;
    PreviewViewport* m_particlePreview;
};
