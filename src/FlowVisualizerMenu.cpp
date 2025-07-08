#include "FlowVisualizerMenu.hpp"
#include "ui_flowVisualizerMenu.h"
#include "PreviewViewport.hpp" // Include the preview viewport header here
#include "QtHelpers.hpp"

#include <QColorDialog>
#include <QPainter>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QTableWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QToolButton>
#include <QDebug>
#include <QLinearGradient>
#include <QButtonGroup>


// Constructor: Sets up the UI, initializes default values, and connects all signals/slots.
FlowVisualizerMenu::FlowVisualizerMenu(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::FlowVisualizerMenu)
{
    QSignalBlocker b(this);
    ui->setupUi(this);


    WH::unbounded(ui->originInputX);
    WH::unbounded(ui->originInputY);
    WH::unbounded(ui->originInputZ);



    WH::range180deg(ui->angleInputEulerX);
    WH::range180deg(ui->angleInputEulerY);
    WH::range180deg(ui->angleInputEulerZ);

    // --- Get pointers to the promoted widgets ---
    // qobject_cast is the safe way to downcast QObject pointers.
    // This assumes you have promoted the QGroupBox named "arrowPreviewViewport" etc.
    // to the class "PreviewViewport" in Qt Designer.
    m_staticPreview = qobject_cast<PreviewViewport*>(ui->arrowPreviewViewport);
    m_dynamicPreview = qobject_cast<PreviewViewport*>(ui->dynamic_PreviewViewport);
    m_particlePreview = qobject_cast<PreviewViewport*>(ui->particle_previewViewport);

    // Check if the casts were successful
    if (!m_staticPreview || !m_dynamicPreview || !m_particlePreview) {
        qWarning() << "FlowVisualizerMenu: Failed to cast one or more preview widgets. "
            << "Ensure they are correctly promoted to PreviewViewport in Qt Designer.";
    }

    initializeState();
    setupConnections();

    auto* boundaryGroup = new QButtonGroup(this);
    boundaryGroup->setExclusive(true);
    boundaryGroup->addButton(ui->rectangleBoundaryButton, 0);
    boundaryGroup->addButton(ui->sphericalBoundaryButton, 1);


}

// Destructor: Cleans up the UI pointer.
FlowVisualizerMenu::~FlowVisualizerMenu()
{
    delete ui;
}

// Sets initial default values for all UI controls and state variables.
void FlowVisualizerMenu::initializeState()
{
    // Block signals during initialization to prevent slots from firing prematurely.
    const QSignalBlocker blocker(this);

    // --- General Settings ---
    ui->masterVisibilityCheck->setChecked(true);
    ui->originInputX->setValue(0.0);
    ui->originInputY->setValue(0.0);
    ui->originInputZ->setValue(0.0);
    ui->angleInputEulerX->setValue(0.0);
    ui->angleInputEulerY->setValue(0.0);
    ui->angleInputEulerZ->setValue(0.0);

    // --- Static Arrow Defaults ---
    m_staticXPos = QColor(255, 80, 80); m_staticXNeg = QColor(255, 170, 170);
    m_staticYPos = QColor(80, 255, 80); m_staticYNeg = QColor(170, 255, 170);
    m_staticZPos = QColor(80, 80, 255); m_staticZNeg = QColor(170, 170, 255);

    // --- Dynamic Vector Defaults ---
    m_dynamicXPos = QColor(255, 80, 80); m_dynamicXNeg = QColor(255, 170, 170);
    m_dynamicYPos = QColor(80, 255, 80); m_dynamicYNeg = QColor(170, 255, 170);
    m_dynamicZPos = QColor(80, 80, 255); m_dynamicZNeg = QColor(170, 170, 255);

    // --- Particle Defaults ---
    m_particleXPos = QColor(255, 80, 80); m_particleXNeg = QColor(255, 170, 170);
    m_particleYPos = QColor(80, 255, 80); m_particleYNeg = QColor(170, 255, 170);
    m_particleZPos = QColor(80, 80, 255); m_particleZNeg = QColor(170, 170, 255);

	// --- Set default values for the color buttons and gradients ---

    // --- Default boundary conditions ---

    ui->boundaryLengthInput->setValue(10.0);  // 2×5
    ui->boundaryWidthInput->setValue(10.0);
    ui->boundaryHeightInput->setValue(10.0);
    ui->sphereBoundaryRadiusInput->setValue(5.0);

    // Use a map to associate buttons with their color variables for cleaner code.
    setupColorButtonMap();

    ui->densityControlRowsInput->setValue(15);
    ui->densityControlColumnsInput->setValue(15);
    ui->densityControlLayersInput->setValue(15);

    ui->baseSizeSpinBox->setValue(0.5);
    ui->headScaleSpinBox->setValue(0.4);
    ui->intensityMultiplierSpinBox->setValue(1.0);
    ui->cullingThresholdSpinBox->setValue(0.01);


    // Manually trigger the sync from spinbox to slider
    linkSliderAndSpinBox(ui->rowDensitySlider, ui->densityControlRowsInput);
    linkSliderAndSpinBox(ui->columnDensitySlider, ui->densityControlColumnsInput);
    linkSliderAndSpinBox(ui->layerDensitySlider, ui->densityControlLayersInput);
    linkSliderAndSpinBox(ui->baseSizeSlider, ui->baseSizeSpinBox);
    linkSliderAndSpinBox(ui->headScaleSlider, ui->headScaleSpinBox);
    linkSliderAndSpinBox(ui->intensityMultiplierSlider, ui->intensityMultiplierSpinBox);
    linkSliderAndSpinBox(ui->cullingThresholdSlider, ui->cullingThresholdSpinBox);

    // Update button stylesheets and gradient previews from the default colors.
    for (auto it = m_colorButtonMap.constBegin(); it != m_colorButtonMap.constEnd(); ++it) {
        it.key()->setStyleSheet(QString("background-color: %1").arg(it.value()->name()));
    }
    updateAxisGradientPreview(ui->label_XAxisGradient, m_staticXNeg, m_staticXPos);
    updateAxisGradientPreview(ui->label_YAxisGradient, m_staticYNeg, m_staticYPos);
    updateAxisGradientPreview(ui->label_ZAxisGradient, m_staticZNeg, m_staticZPos);
    updateAxisGradientPreview(ui->dynamic_label_XAxisGradient, m_dynamicXNeg, m_dynamicXPos);
    updateAxisGradientPreview(ui->dynamic_label_YAxisGradient, m_dynamicYNeg, m_dynamicYPos);
    updateAxisGradientPreview(ui->dynamic_label_ZAxisGradient, m_dynamicZNeg, m_dynamicZPos);
    updateAxisGradientPreview(ui->particle_label_XAxisGradient, m_particleXNeg, m_particleXPos);
    updateAxisGradientPreview(ui->particle_label_YAxisGradient, m_particleYNeg, m_particleYPos);
    updateAxisGradientPreview(ui->particle_label_ZAxisGradient, m_particleZNeg, m_particleZPos);

    // Set initial pages for all stacked widgets.
    onVisualizationTypeChanged(ui->visualizationTypeInput->currentIndex());
    onStaticColoringStyleChanged(ui->coloringStyleComboBox->currentIndex());
    onDynamicColoringStyleChanged(ui->dynamic_coloringStyleComboBox->currentIndex());
    onParticleColoringStyleChanged(ui->particle_coloringStyleComboBox->currentIndex());
}

// Connects all signals from UI widgets to their corresponding slots in this class.
void FlowVisualizerMenu::setupConnections()
{
    // --- Global / Top-Level Connections ---
    connect(ui->masterVisibilityCheck, &QCheckBox::toggled, this, &FlowVisualizerMenu::onMasterVisibilityChanged);
    connect(ui->resetVisualizerButton, &QToolButton::clicked, this, &FlowVisualizerMenu::onResetVisualizerClicked);
    connect(ui->visualizationTypeInput, &QComboBox::currentIndexChanged, this, &FlowVisualizerMenu::onVisualizationTypeChanged);
    // connect(ui->boundaryTypeComboBox, &QComboBox::currentIndexChanged, this, &FlowVisualizerMenu::onBoundaryTypeChanged); // Assuming you add this combo box
    connect(ui->btn_addStop, &QPushButton::clicked, this, &FlowVisualizerMenu::testViewportRequested);
    // --- Static Arrow Connections ---
    connect(ui->coloringStyleComboBox, &QComboBox::currentIndexChanged, this, &FlowVisualizerMenu::onStaticColoringStyleChanged);
    connect(ui->btn_XPosColor, &QPushButton::clicked, this, &FlowVisualizerMenu::onStaticDirectionalColorClicked);
    connect(ui->btn_XNegColor, &QPushButton::clicked, this, &FlowVisualizerMenu::onStaticDirectionalColorClicked);
    connect(ui->btn_YPosColor, &QPushButton::clicked, this, &FlowVisualizerMenu::onStaticDirectionalColorClicked);
    connect(ui->btn_YNegColor_2, &QPushButton::clicked, this, &FlowVisualizerMenu::onStaticDirectionalColorClicked);
    connect(ui->btn_ZPosColor, &QPushButton::clicked, this, &FlowVisualizerMenu::onStaticDirectionalColorClicked);
    connect(ui->btn_ZNegColor, &QPushButton::clicked, this, &FlowVisualizerMenu::onStaticDirectionalColorClicked);
    connect(ui->btn_addStop, &QPushButton::clicked, this, &FlowVisualizerMenu::onStaticAddColorStop);
    connect(ui->btn_removeStop, &QPushButton::clicked, this, &FlowVisualizerMenu::onStaticRemoveColorStop);
    connect(ui->table_colorStops, &QTableWidget::cellChanged, this, &FlowVisualizerMenu::onStaticColorStopTableChanged);
    linkSliderAndSpinBox(ui->baseSizeSlider, ui->baseSizeSpinBox);
    linkSliderAndSpinBox(ui->headScaleSlider, ui->headScaleSpinBox);
    linkSliderAndSpinBox(ui->intensityMultiplierSlider, ui->intensityMultiplierSpinBox);
    linkSliderAndSpinBox(ui->rowDensitySlider, ui->densityControlRowsInput);
    linkSliderAndSpinBox(ui->columnDensitySlider, ui->densityControlColumnsInput);
    linkSliderAndSpinBox(ui->layerDensitySlider, ui->densityControlLayersInput);
    linkSliderAndSpinBox(ui->cullingThresholdSlider, ui->cullingThresholdSpinBox);
    connect(ui->lengthScaleCheckButton, &QToolButton::toggled, this, &FlowVisualizerMenu::onSettingChanged);
    connect(ui->thicknessScaleCheckButton, &QToolButton::toggled, this, &FlowVisualizerMenu::onSettingChanged);
    connect(ui->lengthScaleMultiplier, &QDoubleSpinBox::valueChanged, this, &FlowVisualizerMenu::onSettingChanged);
    connect(ui->thicknessScaleMultiplier, &QDoubleSpinBox::valueChanged, this, &FlowVisualizerMenu::onSettingChanged);

    // --- Dynamic Vector (Flow) Connections ---
    connect(ui->dynamic_coloringStyleComboBox, &QComboBox::currentIndexChanged, this, &FlowVisualizerMenu::onDynamicColoringStyleChanged);
    connect(ui->dynamic_btn_XPosColor, &QPushButton::clicked, this, &FlowVisualizerMenu::onDynamicDirectionalColorClicked);
    connect(ui->dynamic_btn_XNegColor, &QPushButton::clicked, this, &FlowVisualizerMenu::onDynamicDirectionalColorClicked);
    connect(ui->dynamic_btn_YPosColor, &QPushButton::clicked, this, &FlowVisualizerMenu::onDynamicDirectionalColorClicked);
    connect(ui->dynamic_btn_YNegColor, &QPushButton::clicked, this, &FlowVisualizerMenu::onDynamicDirectionalColorClicked);
    connect(ui->dynamic_btn_ZPosColor, &QPushButton::clicked, this, &FlowVisualizerMenu::onDynamicDirectionalColorClicked);
    connect(ui->dynamic_btn_ZNegColor, &QPushButton::clicked, this, &FlowVisualizerMenu::onDynamicDirectionalColorClicked);
    connect(ui->dynamic_btn_addStop, &QPushButton::clicked, this, &FlowVisualizerMenu::onDynamicAddIntensityStop);
    connect(ui->dynamic_btn_removeStop, &QPushButton::clicked, this, &FlowVisualizerMenu::onDynamicRemoveIntensityStop);
    connect(ui->dynamic_table_colorStops, &QTableWidget::cellChanged, this, &FlowVisualizerMenu::onDynamicIntensityTableChanged);
    connect(ui->dynamic_btn_addStopAge, &QPushButton::clicked, this, &FlowVisualizerMenu::onDynamicAddLifetimeStop);
    connect(ui->dynamic_btn_removeStopAge, &QPushButton::clicked, this, &FlowVisualizerMenu::onDynamicRemoveLifetimeStop);
    connect(ui->dynamic_table_colorStopsAge, &QTableWidget::cellChanged, this, &FlowVisualizerMenu::onDynamicLifetimeTableChanged);
    linkSliderAndSpinBox(ui->dynamic_particleLifetimeSlider, ui->dynamic_particleLifetimeSpinBox);
    linkSliderAndSpinBox(ui->dynamic_particleBaseSpeedSlider, ui->dynamic_particleBaseSpeedSpinBox);
    linkSliderAndSpinBox(ui->dynamic_particleSpeedMulitplierSlider, ui->dynamic_particleSpeedMulitplierSpinBox);
    linkSliderAndSpinBox(ui->dynamic_particleBaseSizeSlider, ui->dynamic_particleBaseSizeSpinBox);
    linkSliderAndSpinBox(ui->dynamic_particleHeadScaleSlider, ui->dynamic_particleHeadScaleSpinBox);
    linkSliderAndSpinBox(ui->dynamic_particlePeakSizeSlider, ui->dynamic_particlePeakSizeSpinBox);
    linkSliderAndSpinBox(ui->dynamic_particleMinSizeSlider, ui->dynamic_particleMinSizeSpinBox);
    linkSliderAndSpinBox(ui->dynamic_particleGrowthPercentageSlider, ui->dynamic_particleGrowthPercentageSpinBox);
    linkSliderAndSpinBox(ui->dynamic_particleShrinkPercentageSlider, ui->dynamic_particleShrinkPercentageSpinBox);
    linkSliderAndSpinBox(ui->dynamic_particleRandomDriftSlider, ui->dynamic_particleRandomDriftSpinBox);
    connect(ui->dynamic_particleCountSpinBox, &QSpinBox::valueChanged, this, &FlowVisualizerMenu::onSettingChanged);

    // --- Particle Connections ---
    connect(ui->solid_particle_toggle_button, &QToolButton::toggled, this, &FlowVisualizerMenu::onParticleTypeToggleChanged);
    connect(ui->sprite_particle_toggle_button, &QToolButton::toggled, this, &FlowVisualizerMenu::onParticleTypeToggleChanged);
    connect(ui->particle_coloringStyleComboBox, &QComboBox::currentIndexChanged, this, &FlowVisualizerMenu::onParticleColoringStyleChanged);
    connect(ui->particle_btn_XPosColor, &QPushButton::clicked, this, &FlowVisualizerMenu::onParticleDirectionalColorClicked);
    connect(ui->particle_btn_XNegColor, &QPushButton::clicked, this, &FlowVisualizerMenu::onParticleDirectionalColorClicked);
    connect(ui->particle_btn_YPosColor, &QPushButton::clicked, this, &FlowVisualizerMenu::onParticleDirectionalColorClicked);
    connect(ui->particle_btn_YNegColor, &QPushButton::clicked, this, &FlowVisualizerMenu::onParticleDirectionalColorClicked);
    connect(ui->particle_btn_ZPosColor, &QPushButton::clicked, this, &FlowVisualizerMenu::onParticleDirectionalColorClicked);
    connect(ui->particle_btn_ZNegColor, &QPushButton::clicked, this, &FlowVisualizerMenu::onParticleDirectionalColorClicked);
    connect(ui->particle_btn_addStop, &QPushButton::clicked, this, &FlowVisualizerMenu::onParticleAddIntensityStop);
    connect(ui->particle_btn_removeStop, &QPushButton::clicked, this, &FlowVisualizerMenu::onParticleRemoveIntensityStop);
    connect(ui->particle_table_colorStops, &QTableWidget::cellChanged, this, &FlowVisualizerMenu::onParticleIntensityTableChanged);
    connect(ui->particle_btn_addStopAge, &QPushButton::clicked, this, &FlowVisualizerMenu::onParticleAddLifetimeStop);
    connect(ui->particle_btn_removeStopAge, &QPushButton::clicked, this, &FlowVisualizerMenu::onParticleRemoveLifetimeStop);
    connect(ui->particle_table_colorStopsAge, &QTableWidget::cellChanged, this, &FlowVisualizerMenu::onParticleLifetimeTableChanged);
    linkSliderAndSpinBox(ui->particle_particleLifetimeSlider, ui->particle_particleLifetimeSpinBox);
    linkSliderAndSpinBox(ui->particle_particleBaseSpeedSlider, ui->particle_particleBaseSpeedSpinBox);
    linkSliderAndSpinBox(ui->particle_intensitySpeedMultSlider, ui->particle_intensitySpeedMultSpinBox);
    linkSliderAndSpinBox(ui->particle_particleBaseSizeSlider, ui->particle_particleBaseSizeSpinBox);
    linkSliderAndSpinBox(ui->particle_particlePeakIntensitySlider, ui->particle_particlePeakIntensitySpinBox);
    linkSliderAndSpinBox(ui->particle_particleMinSizeSlider, ui->particle_particleMinSizeSpinBox);
    linkSliderAndSpinBox(ui->particle_spriteBaseSizeSlider, ui->particle_spriteBaseSizeSpinBox);
    linkSliderAndSpinBox(ui->particle_spritePeakIntensitySlider, ui->particle_spritePeakIntensitySpinBox);
    linkSliderAndSpinBox(ui->particle_spriteMinSizeSlider, ui->particle_spriteMinSizeSpinBox);
    linkSliderAndSpinBox(ui->particle_randomWalkSlider, ui->particle_randomWalkSpinBox);
    connect(ui->particle_particleCountSpinBox, &QSpinBox::valueChanged, this, &FlowVisualizerMenu::onSettingChanged);

    connect(ui->rectangleBoundaryButton, &QToolButton::toggled,
        this, [this](bool on) {
            if (on) ui->stackedWidget->setCurrentIndex(0), emit settingsChanged();
        });
    connect(ui->sphericalBoundaryButton, &QToolButton::toggled,
        this, [this](bool on) {
            if (on) ui->stackedWidget->setCurrentIndex(1), emit settingsChanged();
        });

    // ---- BOX DIMENSIONS ----
    auto boxDimChanged = [this] { emit settingsChanged(); };
    connect(ui->boundaryLengthInput, qOverload<double>(&QDoubleSpinBox::valueChanged), boxDimChanged);
    connect(ui->boundaryWidthInput, qOverload<double>(&QDoubleSpinBox::valueChanged), boxDimChanged);
    connect(ui->boundaryHeightInput, qOverload<double>(&QDoubleSpinBox::valueChanged), boxDimChanged);

    // ---- SPHERE RADIUS ----
    connect(ui->sphereBoundaryRadiusInput,
        qOverload<double>(&QDoubleSpinBox::valueChanged),
        this, [this] { emit settingsChanged(); });

    // ---- TRANSLATION & ROTATION ----
    auto xfChanged = [this] { emit transformChanged(); };
    connect(ui->originInputX, qOverload<double>(&QDoubleSpinBox::valueChanged), xfChanged);
    connect(ui->originInputY, qOverload<double>(&QDoubleSpinBox::valueChanged), xfChanged);
    connect(ui->originInputZ, qOverload<double>(&QDoubleSpinBox::valueChanged), xfChanged);

    connect(ui->angleInputEulerX, qOverload<double>(&QDoubleSpinBox::valueChanged), xfChanged);
    connect(ui->angleInputEulerY, qOverload<double>(&QDoubleSpinBox::valueChanged), xfChanged);
    connect(ui->angleInputEulerZ, qOverload<double>(&QDoubleSpinBox::valueChanged), xfChanged);




}

// MapsQPushButton pointers to their corresponding QColor member variables.
void FlowVisualizerMenu::setupColorButtonMap()
{
    m_colorButtonMap[ui->btn_XPosColor] = &m_staticXPos;
    m_colorButtonMap[ui->btn_XNegColor] = &m_staticXNeg;
    m_colorButtonMap[ui->btn_YPosColor] = &m_staticYPos;
    m_colorButtonMap[ui->btn_YNegColor_2] = &m_staticYNeg;
    m_colorButtonMap[ui->btn_ZPosColor] = &m_staticZPos;
    m_colorButtonMap[ui->btn_ZNegColor] = &m_staticZNeg;

    m_colorButtonMap[ui->dynamic_btn_XPosColor] = &m_dynamicXPos;
    m_colorButtonMap[ui->dynamic_btn_XNegColor] = &m_dynamicXNeg;
    m_colorButtonMap[ui->dynamic_btn_YPosColor] = &m_dynamicYPos;
    m_colorButtonMap[ui->dynamic_btn_YNegColor] = &m_dynamicYNeg;
    m_colorButtonMap[ui->dynamic_btn_ZPosColor] = &m_dynamicZPos;
    m_colorButtonMap[ui->dynamic_btn_ZNegColor] = &m_dynamicZNeg;

    m_colorButtonMap[ui->particle_btn_XPosColor] = &m_particleXPos;
    m_colorButtonMap[ui->particle_btn_XNegColor] = &m_particleXNeg;
    m_colorButtonMap[ui->particle_btn_YPosColor] = &m_particleYPos;
    m_colorButtonMap[ui->particle_btn_YNegColor] = &m_particleYNeg;
    m_colorButtonMap[ui->particle_btn_ZPosColor] = &m_particleZPos;
    m_colorButtonMap[ui->particle_btn_ZNegColor] = &m_particleZNeg;
}

// ===================================================================
// --- SLOT IMPLEMENTATIONS
// ===================================================================

void FlowVisualizerMenu::onMasterVisibilityChanged(bool checked) { onSettingChanged(); }
void FlowVisualizerMenu::onResetVisualizerClicked() { /* TODO: Reset logic */ emit settingsChanged(); }
void FlowVisualizerMenu::onVisualizationTypeChanged(int index) { ui->stackedWidget_2->setCurrentIndex(index); onSettingChanged(); }
void FlowVisualizerMenu::onBoundaryTypeChanged(int index) { ui->stackedWidget->setCurrentIndex(index); onSettingChanged(); }
void FlowVisualizerMenu::onSettingChanged() { emit settingsChanged(); }

// --- Static Arrow Slot Implementations ---
void FlowVisualizerMenu::onStaticColoringStyleChanged(int index) { ui->stackedWidget_3->setCurrentIndex(index); onSettingChanged(); }
void FlowVisualizerMenu::onStaticDirectionalColorClicked() {
    auto* button = qobject_cast<QPushButton*>(sender());
    if (m_colorButtonMap.contains(button)) {
        pickColorForButton(button, *m_colorButtonMap[button]);
        updateAxisGradientPreview(ui->label_XAxisGradient, m_staticXNeg, m_staticXPos);
        updateAxisGradientPreview(ui->label_YAxisGradient, m_staticYNeg, m_staticYPos);
        updateAxisGradientPreview(ui->label_ZAxisGradient, m_staticZNeg, m_staticZPos);
        onSettingChanged();
    }
}
void FlowVisualizerMenu::onStaticAddColorStop() { /* TODO */ onSettingChanged(); }
void FlowVisualizerMenu::onStaticRemoveColorStop() { /* TODO */ onSettingChanged(); }
void FlowVisualizerMenu::onStaticColorStopTableChanged() { updateGradientPreviewFromTable(ui->label_gradientPreview, ui->table_colorStops); onSettingChanged(); }

// --- Dynamic Vector (Flow) Slot Implementations ---
void FlowVisualizerMenu::onDynamicColoringStyleChanged(int index) { ui->stackedWidget_4->setCurrentIndex(index); onSettingChanged(); }
void FlowVisualizerMenu::onDynamicDirectionalColorClicked() {
    auto* button = qobject_cast<QPushButton*>(sender());
    if (m_colorButtonMap.contains(button)) {
        pickColorForButton(button, *m_colorButtonMap[button]);
        updateAxisGradientPreview(ui->dynamic_label_XAxisGradient, m_dynamicXNeg, m_dynamicXPos);
        updateAxisGradientPreview(ui->dynamic_label_YAxisGradient, m_dynamicYNeg, m_dynamicYPos);
        updateAxisGradientPreview(ui->dynamic_label_ZAxisGradient, m_dynamicZNeg, m_dynamicZPos);
        onSettingChanged();
    }
}
void FlowVisualizerMenu::onDynamicAddIntensityStop() { /* TODO */ onSettingChanged(); }
void FlowVisualizerMenu::onDynamicRemoveIntensityStop() { /* TODO */ onSettingChanged(); }
void FlowVisualizerMenu::onDynamicIntensityTableChanged() { updateGradientPreviewFromTable(ui->dynamic_label_gradientPreview, ui->dynamic_table_colorStops); onSettingChanged(); }
void FlowVisualizerMenu::onDynamicAddLifetimeStop() { /* TODO */ onSettingChanged(); }
void FlowVisualizerMenu::onDynamicRemoveLifetimeStop() { /* TODO */ onSettingChanged(); }
void FlowVisualizerMenu::onDynamicLifetimeTableChanged() { updateGradientPreviewFromTable(ui->dynamic_label_gradientPreviewAge, ui->dynamic_table_colorStopsAge); onSettingChanged(); }

// --- Particle Slot Implementations ---
void FlowVisualizerMenu::onParticleTypeToggleChanged() {
    auto* button = qobject_cast<QToolButton*>(sender());
    if (!button || !button->isChecked()) {
        // If the user un-checks a button, ensure at least one is still checked.
        if (!ui->solid_particle_toggle_button->isChecked() && !ui->sprite_particle_toggle_button->isChecked()) {
            button->setChecked(true); // Prevent un-checking the last one.
        }
        return;
    }
    // Ensure only one toggle is active
    if (button == ui->solid_particle_toggle_button) {
        ui->sprite_particle_toggle_button->setChecked(false);
    }
    else if (button == ui->sprite_particle_toggle_button) {
        ui->solid_particle_toggle_button->setChecked(false);
    }
    onSettingChanged();
}
void FlowVisualizerMenu::onParticleColoringStyleChanged(int index) { ui->stackedWidget_5->setCurrentIndex(index); onSettingChanged(); }
void FlowVisualizerMenu::onParticleDirectionalColorClicked() {
    auto* button = qobject_cast<QPushButton*>(sender());
    if (m_colorButtonMap.contains(button)) {
        pickColorForButton(button, *m_colorButtonMap[button]);
        updateAxisGradientPreview(ui->particle_label_XAxisGradient, m_particleXNeg, m_particleXPos);
        updateAxisGradientPreview(ui->particle_label_YAxisGradient, m_particleYNeg, m_particleYPos);
        updateAxisGradientPreview(ui->particle_label_ZAxisGradient, m_particleZNeg, m_particleZPos);
        onSettingChanged();
    }
}
void FlowVisualizerMenu::onParticleAddIntensityStop() { /* TODO */ onSettingChanged(); }
void FlowVisualizerMenu::onParticleRemoveIntensityStop() { /* TODO */ onSettingChanged(); }
void FlowVisualizerMenu::onParticleIntensityTableChanged() { updateGradientPreviewFromTable(ui->particle_label_gradientPreview, ui->particle_table_colorStops); onSettingChanged(); }
void FlowVisualizerMenu::onParticleAddLifetimeStop() { /* TODO */ onSettingChanged(); }
void FlowVisualizerMenu::onParticleRemoveLifetimeStop() { /* TODO */ onSettingChanged(); }
void FlowVisualizerMenu::onParticleLifetimeTableChanged() { updateGradientPreviewFromTable(ui->particle_label_gradientPreviewAge, ui->particle_table_colorStopsAge); onSettingChanged(); }

// ===================================================================
// --- HELPER IMPLEMENTATIONS
// ===================================================================

// Links a QSlider and a QDoubleSpinBox for synchronized control.
void FlowVisualizerMenu::linkSliderAndSpinBox(QSlider* slider, QDoubleSpinBox* spinBox, double scale)
{
    connect(slider, &QSlider::valueChanged, this, [spinBox, scale](int value) {
        const QSignalBlocker blocker(spinBox);
        spinBox->setValue(static_cast<double>(value) / scale);
        });
    connect(spinBox, &QDoubleSpinBox::valueChanged, this, [slider, scale](double value) {
        const QSignalBlocker blocker(slider);
        slider->setValue(static_cast<int>(value * scale));
        });
    connect(slider, &QSlider::valueChanged, this, &FlowVisualizerMenu::onSettingChanged);

    slider->setValue(static_cast<int>(spinBox->value() * scale));
}

// Overloaded version for QSpinBox.
void FlowVisualizerMenu::linkSliderAndSpinBox(QSlider* slider, QSpinBox* spinBox)
{
    connect(slider, &QSlider::valueChanged, spinBox, &QSpinBox::setValue);
    connect(spinBox, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);
    connect(slider, &QSlider::valueChanged, this, &FlowVisualizerMenu::onSettingChanged);

    slider->setValue(spinBox->value());
}

// Opens a color dialog and updates the button's style and the associated color variable.
void FlowVisualizerMenu::pickColorForButton(QPushButton* button, QColor& colorMember)
{
    QColor newColor = QColorDialog::getColor(colorMember, this, "Select Color");
    if (newColor.isValid()) {
        colorMember = newColor;
        button->setStyleSheet(QString("background-color: %1").arg(newColor.name()));
    }
}

// Updates a label with a two-color horizontal gradient.
void FlowVisualizerMenu::updateAxisGradientPreview(QLabel* label, const QColor& negColor, const QColor& posColor)
{
    if (!label || label->width() <= 0) return;
    QPixmap pixmap(label->size());
    pixmap.fill(Qt::transparent);
    QLinearGradient gradient(0, 0, label->width(), 0);
    gradient.setColorAt(0.0, negColor);
    gradient.setColorAt(1.0, posColor);
    QPainter painter(&pixmap);
    painter.fillRect(pixmap.rect(), gradient);
    label->setPixmap(pixmap);
}

// Updates a label with a multi-stop gradient based on data from a QTableWidget.
void FlowVisualizerMenu::updateGradientPreviewFromTable(QLabel* previewLabel, QTableWidget* table)
{
    if (!previewLabel || !table || previewLabel->width() <= 0) return;

    QLinearGradient gradient(0, 0, previewLabel->width(), 0);

    // TODO: Iterate through the table rows, get the position from the spinbox in column 0
    // and the color from the button in column 1. Add them to the QLinearGradient
    // using gradient.setColorAt(position, color).

    QPixmap pixmap(previewLabel->size());
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.fillRect(pixmap.rect(), gradient);
    previewLabel->setPixmap(pixmap);
}


// --- HELPER IMPLEMENTATIONS ---

glm::vec4 FlowVisualizerMenu::qColorToGlm(const QColor& color) const {
    return glm::vec4(color.redF(), color.greenF(), color.blueF(), color.alphaF());
}

std::vector<ColorStop> FlowVisualizerMenu::getGradientFromTable(QTableWidget* table) const
{
    std::vector<ColorStop> gradient;
    for (int row = 0; row < table->rowCount(); ++row) {
        ColorStop stop;

        auto* posWidget = qobject_cast<QDoubleSpinBox*>(table->cellWidget(row, 0));
        if (posWidget) {
            stop.position = static_cast<float>(posWidget->value());
        }

        auto* colorWidget = qobject_cast<QPushButton*>(table->cellWidget(row, 1));
        if (colorWidget) {
            QColor color = colorWidget->palette().button().color();
            stop.color = qColorToGlm(color);
        }
        gradient.push_back(stop);
    }
    // TODO: Sort the gradient by position if needed.
    return gradient;
}

// --- PUBLIC GETTER IMPLEMENTATIONS (Fixed) ---

bool FlowVisualizerMenu::isMasterVisible() const { return ui->masterVisibilityCheck->isChecked(); }
glm::vec3 FlowVisualizerMenu::getFieldPosition() const { return { static_cast<float>(ui->originInputX->value()), static_cast<float>(ui->originInputY->value()), static_cast<float>(ui->originInputZ->value()) }; }
glm::vec3 FlowVisualizerMenu::getFieldOrientation() const { return { static_cast<float>(ui->angleInputEulerX->value()), static_cast<float>(ui->angleInputEulerY->value()), static_cast<float>(ui->angleInputEulerZ->value()) }; }
AABB FlowVisualizerMenu::getBounds() const
{
    // Field centre comes from the “Origin” spin-boxes
    const glm::vec3 centre = getFieldPosition();

    // Which boundary widget is active?  0 = Box, 1 = Sphere  (see stackedWidget)
    if (ui->stackedWidget->currentIndex() == 0)          //  ☐ Box
    {
        const float lx = static_cast<float>(ui->boundaryLengthInput->value());
        const float ly = static_cast<float>(ui->boundaryWidthInput->value());
        const float lz = static_cast<float>(ui->boundaryHeightInput->value());
        const glm::vec3 half(lx * 0.5f, ly * 0.5f, lz * 0.5f);

        return { centre - half, centre + half };
    }
    else                                                 //  ◯ Sphere
    {
        const float r = static_cast<float>(ui->sphereBoundaryRadiusInput->value());
        const glm::vec3 extent(r);
        return { centre - extent, centre + extent };
    }
}

glm::vec3 FlowVisualizerMenu::getCentre() const
{
    return { static_cast<float>(ui->originInputX->value()),
             static_cast<float>(ui->originInputY->value()),
             static_cast<float>(ui->originInputZ->value()) };
}

glm::vec3 FlowVisualizerMenu::getEuler() const   // degrees
{
    return { static_cast<float>(ui->angleInputEulerX->value()),
             static_cast<float>(ui->angleInputEulerY->value()),
             static_cast<float>(ui->angleInputEulerZ->value()) };
}

FieldVisualizerComponent::DisplayMode FlowVisualizerMenu::getDisplayMode() const { return static_cast<FieldVisualizerComponent::DisplayMode>(ui->visualizationTypeInput->currentIndex()); }

// Static Arrow Getters
glm::ivec3 FlowVisualizerMenu::getArrowDensity() const { return { ui->densityControlRowsInput->value(), ui->densityControlColumnsInput->value(), ui->densityControlLayersInput->value() }; }
float FlowVisualizerMenu::getArrowBaseSize() const { return ui->baseSizeSpinBox->value(); }
float FlowVisualizerMenu::getArrowHeadScale() const { return ui->headScaleSpinBox->value(); }
float FlowVisualizerMenu::getArrowIntensityMultiplier() const { return ui->intensityMultiplierSpinBox->value(); }
float FlowVisualizerMenu::getArrowCullingThreshold() const { return ui->cullingThresholdSpinBox->value(); }
bool FlowVisualizerMenu::isArrowLengthScaled() const { return ui->lengthScaleCheckButton->isChecked(); }
float FlowVisualizerMenu::getArrowLengthScaleMultiplier() const { return ui->lengthScaleMultiplier->value(); }
bool FlowVisualizerMenu::isArrowThicknessScaled() const { return ui->thicknessScaleCheckButton->isChecked(); }
float FlowVisualizerMenu::getArrowThicknessScaleMultiplier() const { return ui->thicknessScaleMultiplier->value(); }
FieldVisualizerComponent::ColoringMode FlowVisualizerMenu::getArrowColoringMode() const { return static_cast<FieldVisualizerComponent::ColoringMode>(ui->coloringStyleComboBox->currentIndex()); }
glm::vec4 FlowVisualizerMenu::getArrowDirColor(int index) const {
    switch (index) {
    case 0: return qColorToGlm(m_staticXPos);
    case 1: return qColorToGlm(m_staticXNeg);
    case 2: return qColorToGlm(m_staticYPos);
    case 3: return qColorToGlm(m_staticYNeg);
    case 4: return qColorToGlm(m_staticZPos);
    case 5: return qColorToGlm(m_staticZNeg);
    default: return { 1,1,1,1 };
    }
}
std::vector<ColorStop> FlowVisualizerMenu::getArrowIntensityGradient() const { return getGradientFromTable(ui->table_colorStops); }

// Flow Getters
int FlowVisualizerMenu::getFlowParticleCount() const { return ui->dynamic_particleCountSpinBox->value(); }
float FlowVisualizerMenu::getFlowLifetime() const { return ui->dynamic_particleLifetimeSpinBox->value(); }
float FlowVisualizerMenu::getFlowBaseSpeed() const { return ui->dynamic_particleBaseSpeedSpinBox->value(); }
float FlowVisualizerMenu::getFlowSpeedIntensityMult() const { return ui->dynamic_particleSpeedMulitplierSpinBox->value(); }
float FlowVisualizerMenu::getFlowBaseSize() const { return ui->dynamic_particleBaseSizeSpinBox->value(); }
float FlowVisualizerMenu::getFlowHeadScale() const { return ui->dynamic_particleHeadScaleSpinBox->value(); }
float FlowVisualizerMenu::getFlowPeakSizeMult() const { return ui->dynamic_particlePeakSizeSpinBox->value(); }
float FlowVisualizerMenu::getFlowMinSize() const { return ui->dynamic_particleMinSizeSpinBox->value(); }
float FlowVisualizerMenu::getFlowGrowthPercent() const { return ui->dynamic_particleGrowthPercentageSpinBox->value(); }
float FlowVisualizerMenu::getFlowShrinkPercent() const { return ui->dynamic_particleShrinkPercentageSpinBox->value(); }
float FlowVisualizerMenu::getFlowRandomWalk() const { return ui->dynamic_particleRandomDriftSpinBox->value(); }
bool FlowVisualizerMenu::isFlowLengthScaled() const { return ui->dynamic_lengthScaleCheckButton->isChecked(); }
float FlowVisualizerMenu::getFlowLengthScaleMultiplier() const { return ui->dynamic_lengthScaleMultiplier->value(); }
bool FlowVisualizerMenu::isFlowThicknessScaled() const { return ui->dynamic_thicknessScaleCheckButton->isChecked(); }
float FlowVisualizerMenu::getFlowThicknessScaleMultiplier() const { return ui->dynamic_thicknessScaleMultiplier->value(); }
FieldVisualizerComponent::ColoringMode FlowVisualizerMenu::getFlowColoringMode() const { return static_cast<FieldVisualizerComponent::ColoringMode>(ui->dynamic_coloringStyleComboBox->currentIndex()); }

// Particle Getters
bool FlowVisualizerMenu::isParticleSolid() const { return ui->solid_particle_toggle_button->isChecked(); }
int FlowVisualizerMenu::getParticleCount() const { return ui->particle_particleCountSpinBox->value(); }
float FlowVisualizerMenu::getParticleLifetime() const { return ui->particle_particleLifetimeSpinBox->value(); }
float FlowVisualizerMenu::getParticleBaseSpeed() const { return ui->particle_particleBaseSpeedSpinBox->value(); }
float FlowVisualizerMenu::getParticleSpeedIntensityMult() const { return ui->particle_intensitySpeedMultSpinBox->value(); }
float FlowVisualizerMenu::getParticleBaseSize() const { return ui->particle_particleBaseSizeSpinBox->value(); }
float FlowVisualizerMenu::getParticlePeakSizeMult() const { return ui->particle_particlePeakIntensitySpinBox->value(); }
float FlowVisualizerMenu::getParticleMinSize() const { return ui->particle_particleMinSizeSpinBox->value(); }
float FlowVisualizerMenu::getParticleBaseGlow() const { return ui->particle_spriteBaseSizeSpinBox->value(); }
float FlowVisualizerMenu::getParticlePeakGlowMult() const { return ui->particle_spritePeakIntensitySpinBox->value(); }
float FlowVisualizerMenu::getParticleMinGlow() const { return ui->particle_spriteMinSizeSpinBox->value(); }
float FlowVisualizerMenu::getParticleRandomWalk() const { return ui->particle_randomWalkSpinBox->value(); }
FieldVisualizerComponent::ColoringMode FlowVisualizerMenu::getParticleColoringMode() const { return static_cast<FieldVisualizerComponent::ColoringMode>(ui->particle_coloringStyleComboBox->currentIndex()); }
glm::vec4 FlowVisualizerMenu::getParticleDirColor(int index) const { /* ... */ return {}; }
std::vector<ColorStop> FlowVisualizerMenu::getParticleIntensityGradient() const { return getGradientFromTable(ui->particle_table_colorStops); }
std::vector<ColorStop> FlowVisualizerMenu::getParticleLifetimeGradient() const { return getGradientFromTable(ui->particle_table_colorStopsAge); }

void FlowVisualizerMenu::updateControlsFromComponent(const FieldVisualizerComponent& component)
{
    Q_ASSERT(ui->originInputX->parent());
    // Block signals to prevent an infinite loop of updates
    const QSignalBlocker blocker(this);

    const glm::vec3 centre = 0.5f * (component.bounds.min + component.bounds.max);
    const glm::vec3 size = component.bounds.max - component.bounds.min;

    // Update General controls
    ui->masterVisibilityCheck->setChecked(component.isEnabled);
    ui->visualizationTypeInput->setCurrentIndex(static_cast<int>(component.displayMode));
	// Update Boundary Type (Box or Sphere)

	// Update Origin and Boundary Size Inputs
    ui->originInputX->setValue(centre.x);
    ui->originInputY->setValue(centre.y);
    ui->originInputZ->setValue(centre.z);

    ui->boundaryLengthInput->setValue(size.x);
    ui->boundaryWidthInput->setValue(size.y);
    ui->boundaryHeightInput->setValue(size.z);

    // Update Arrow Settings Tab
    const auto& arrowSettings = component.arrowSettings;
    ui->densityControlRowsInput->setValue(arrowSettings.density.x);
    ui->densityControlColumnsInput->setValue(arrowSettings.density.y);
    ui->densityControlLayersInput->setValue(arrowSettings.density.z);
    ui->baseSizeSpinBox->setValue(arrowSettings.vectorScale);
    ui->headScaleSpinBox->setValue(arrowSettings.headScale);
    ui->cullingThresholdSpinBox->setValue(arrowSettings.cullingThreshold);

    // TODO: Add similar blocks to update the Flow and Particle tabs

    // Manually trigger the slider updates after setting the spinboxes
    linkSliderAndSpinBox(ui->rowDensitySlider, ui->densityControlRowsInput);
    linkSliderAndSpinBox(ui->columnDensitySlider, ui->densityControlColumnsInput);
    linkSliderAndSpinBox(ui->layerDensitySlider, ui->densityControlLayersInput);
    linkSliderAndSpinBox(ui->baseSizeSlider, ui->baseSizeSpinBox);
    linkSliderAndSpinBox(ui->headScaleSlider, ui->headScaleSpinBox);
    linkSliderAndSpinBox(ui->cullingThresholdSlider, ui->cullingThresholdSpinBox);
}