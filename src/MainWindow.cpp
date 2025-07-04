#include "MainWindow.hpp"
#include "StaticToolbar.hpp"
#include "PropertiesPanel.hpp"
#include "Scene.hpp"
#include "ViewportWidget.hpp"
#include "components.hpp" 
#include "Camera.hpp"
#include "SceneBuilder.hpp"
#include "GridLevel.hpp"
#include "KRobotParser.hpp"
#include "KRobotWriter.hpp"
#include "URDFParser.hpp"
#include "SDFParser.hpp"
#include "RobotEnrichmentDialog.hpp"
#include "Mesh.hpp" // Required for the test cube's mesh data.
#include "IntersectionSystem.hpp" 
#include "RenderingSystem.hpp"
#include "FlowVisualizerMenu.hpp"
#include "Helpers.hpp"   

#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QWidget>
#include <QMenuBar>
#include <QStatusBar>
#include <QTimer>
#include <QDebug>
#include <DockManager.h>
#include <DockWidget.h>
#include <DockAreaWidget.h>
#include <QApplication>
#include <QButtonGroup>
#include <QSplitter>
#include "DockSplitter.h" 

const QString sidePanelStyle = R"(
    /* General Window and Text Styling */
    QWidget {
        background-color: #2c313a; /* Dark gray background */
        color: #d5d5d5;            /* Light gray text */
        font-family: "Segoe UI";    /* Or another clean, modern font */
        font-size: 9pt;
    }

    /* GroupBox Styling for a modern, clean look */
    QGroupBox {
        background-color: #353b46; /* Slightly lighter background for groups */
        border: 1px solid #4a5260;
        border-radius: 4px;
        margin-top: 10px; /* Provides space for the title */
    }

    QGroupBox::title {
        subcontrol-origin: margin;
        subcontrol-position: top center;
        padding: 0 5px;
        background-color: #353b46;
        border: none;
    }

    /* Apply a shared style to BOTH QToolButton and QPushButton */
    QToolButton, QPushButton {
        background-color: transparent;
        border: 1px solid #4a5260;
        border-radius: 4px;
        padding: 5px;
        min-width: 65px;
        min-height: 20px;
        color: #d5d5d5; /* Make sure text color is set */
    }

    QToolButton:hover, QPushButton:hover {
        background-color: #4a5260;
        border: 1px solid #5a6474;
    }

    QToolButton:pressed, QPushButton:pressed {
        background-color: #5a6474;
    }

    QToolButton:checked { /* This is for toggle buttons like your tools */
        background-color: #0078d7;
        color: white;
        border: 1px solid #0078d7;
    }

    QComboBox {
        background-color: #2c313a;
        border: 1px solid #4a5260;
        border-radius: 4px;
        padding: 5px;
        min-height: 20px;
    }

    QComboBox:hover {
        border: 1px solid #5a6474;
    }

    QComboBox::drop-down {
        border: none;
    }

    QComboBox::down-arrow {
        image: url(:/icons/chevron-down.png);
        width: 12px;
        height: 12px;
    }
    
    QSlider::groove:horizontal {
        border: 1px solid #4a5260;
        height: 4px; 
        background: #353b46;
        margin: 2px 0;
        border-radius: 2px;
    }

    QSlider::handle:horizontal {
        background: #0078d7;
        border: 1px solid #0078d7;
        width: 14px;
        margin: -5px 0; 
        border-radius: 7px;
    }

    QFrame[frameShape="VLine"] {
        border: 1px solid #4a5260;
    }
    QFrame[frameShape="HLine"] {
        border: 1px solid #4a5260;
    }
)";

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // --- 1. Create the Scene and Initial Entities ---
    m_scene = std::make_unique<Scene>();
    auto& registry = m_scene->getRegistry();

    // --- Set up scene-wide properties like fog ---
    auto& sceneProps = registry.ctx().emplace<SceneProperties>();
    sceneProps.fogEnabled = true;
    sceneProps.fogColor = glm::vec3(0.1f, 0.1f, 0.15f);
    sceneProps.fogStartDistance = 15.0f;
    sceneProps.fogEndDistance = 100.0f;

    // --- Create a default grid ---
    {
        auto gridEntity = registry.create();
        registry.emplace<TagComponent>(gridEntity, "Primary Grid");
        registry.emplace<TransformComponent>(gridEntity);
        auto& gridComp = registry.emplace<GridComponent>(gridEntity);
        gridComp.levels.emplace_back(0.001f, glm::vec3(1.0f, 0.56f, 1.0f), 0.0f, 2.0f);
        gridComp.levels.emplace_back(0.01f, glm::vec3(0.66f, 0.66f, 0.5f), 0.0f, 5.0f);
        gridComp.levels.emplace_back(0.1f, glm::vec3(0.35f, 0.35f, 0.35f), 0.0f, 25.0f);
        gridComp.levels.emplace_back(1.0f, glm::vec3(1.0f, 0.6f, 0.0f), 5.0f, 50.0f);
        gridComp.levels.emplace_back(10.0f, glm::vec3(0.28f, 0.56f, 0.86f), 25.0f, 200.0f);
    }

    // --- Create a single test cube in the center of the scene ---
    {
        auto cubeEntity = registry.create();
        registry.emplace<TagComponent>(cubeEntity, "Test Cube");
        registry.emplace<TransformComponent>(cubeEntity).translation = glm::vec3(0.0f, 0.5f, 0.0f);
        registry.emplace<BoundingBoxComponent>(cubeEntity);
        registry.emplace<FieldSourceTag>(cubeEntity);

        auto& pointEffector = registry.emplace<MeshEffectorComponent>(cubeEntity);
        pointEffector.strength = -1.0f;
        pointEffector.distance = 3.0f;

        auto& mesh = registry.emplace<RenderableMeshComponent>(cubeEntity);
        const std::vector<float>& raw = Mesh::getLitCubeVertices();
        constexpr std::size_t stride = 6;
        mesh.vertices.reserve(raw.size() / stride);
        for (std::size_t i = 0; i < raw.size(); i += stride)
        {
            glm::vec3 pos{ raw[i],     raw[i + 1], raw[i + 2] };
            glm::vec3 normal{ raw[i + 3],   raw[i + 4], raw[i + 5] };
            mesh.vertices.emplace_back(pos, normal);
        }
        mesh.indices = Mesh::getLitCubeIndices();
    }

    // --- Create Splines ---
    {
        using SB = SceneBuilder;
        auto& reg = m_scene->getRegistry();
        auto CREntity = SB::makeCR(reg, { {-2,0,-1}, {-2,0, 1}, { 2,0, 1}, { 2,0,-1} }, { 0.9f,0.9f,0.9f,1 }, { 0.9f,0.3f,0.3f,1 }, 18.0f);
        reg.emplace<PulsingSplineTag>(CREntity);
        auto ParamEntity = SB::makeParam(reg, [](float t) { return glm::vec3(2 * std::cos(6.28f * 3 * t), 6 * t, 2 * std::sin(6.28f * 3 * t)); }, { 0.9f,0.9f,0.9f,1 }, { 0.2f,0.6f,1.0f,1 }, 12.0f);
        reg.emplace<PulsingSplineTag>(ParamEntity);
        auto LinearEntity = SB::makeLinear(reg, { {-5, 0.1, -5}, {-5, 2, -5}, {0, 2, -5}, {0, 0.1, -5} }, { 0.9f,0.9f,0.9f,1 }, { 0.1f, 1.0f, 0.2f, 1 }, 18.0f);
        reg.emplace<PulsingSplineTag>(LinearEntity);
        auto bezierEntity = SB::makeBezier(reg, { {5, 0.1, -5}, {5, 4, -5}, {2, 4, -5}, {2, 0.1, -5} }, { 0.9f,0.9f,0.9f,1 }, { 1.0f, 0.9f, 0.2f, 1 }, 18.0f);
        reg.emplace<PulsingSplineTag>(bezierEntity);
       // auto& splineEffector = reg.emplace<SplineEffectorComponent>(bezierEntity);
       // splineEffector.strength = -1.0f; // Example: make it an attractor
       // splineEffector.radius = 02.00f;
    }

    // --- Create Field Visualizers and Effectors ---
    {
        auto& reg = m_scene->getRegistry();
        auto visualizerEntity = reg.create();
        reg.emplace<TagComponent>(visualizerEntity, "Field Visualizer");
        reg.emplace<TransformComponent>(visualizerEntity);
        auto& visualizer = reg.emplace<FieldVisualizerComponent>(visualizerEntity);
        visualizer.bounds = { glm::vec3(-5.0f, -2.5f, -5.0f), glm::vec3(10.0f, 2.5f, 5.0f) };
        visualizer.displayMode = FieldVisualizerComponent::DisplayMode::Arrows;
        auto& registry = m_scene->getRegistry();
        QTimer::singleShot(0, this, [this, &registry] {
            auto view = registry.view<FieldVisualizerComponent>();
            if (!view.empty())
                m_flowVisualizerMenu->updateControlsFromComponent(
                    firstComponent<FieldVisualizerComponent>(registry));
            });
        // Set properties on the correct sub-struct

        visualizer.arrowSettings.density = { 15, 5, 15 };
        visualizer.arrowSettings.vectorScale = 0.5f;
        visualizer.arrowSettings.headScale = 0.4f;
        visualizer.arrowSettings.intensityMultiplier = 1.0f;
        visualizer.arrowSettings.cullingThreshold = 0.01f;
        visualizer.arrowSettings.coloringMode = FieldVisualizerComponent::ColoringMode::Intensity;
        visualizer.bounds.max = { 4, 2, 4 };
		visualizer.bounds.min = { -4, -2, -4 };
        visualizer.flowSettings.particleCount = 5000;
        visualizer.flowSettings.baseSpeed = 0.15f;
        visualizer.flowSettings.baseSize = 0.30f; // Was flowScale
        visualizer.flowSettings.randomWalkStrength = 0.1f; // Was flowRandomWalk
        visualizer.flowSettings.lifetime = 7.0f;

        auto windSource = reg.create();
        reg.emplace<TagComponent>(windSource, "Wind Source");
        reg.emplace<TransformComponent>(windSource);
        reg.emplace<FieldSourceTag>(windSource);
        auto& directional = reg.emplace<DirectionalEffectorComponent>(windSource);
        directional.direction = { 1.0f, 0.0f, 0.50f };
        directional.strength = 0.60f;
        

        auto repulsorSource = reg.create();
        reg.emplace<TagComponent>(repulsorSource, "Repulsor");
        auto& repulsorTransform = reg.emplace<TransformComponent>(repulsorSource);
        repulsorTransform.translation = { 5.0f, 1.0f, 0.0f };
        reg.emplace<FieldSourceTag>(repulsorSource);
        auto& point = reg.emplace<PointEffectorComponent>(repulsorSource);
        point.strength = 10.0f;
        point.radius = 4.0f;
        point.falloff = PointEffectorComponent::FalloffType::Linear;
    }

    // --- Create initial cameras for the viewports ---
    auto cameraEntity1 = SceneBuilder::createCamera(registry, { 0, 2, 5 }, { 1, 0.3f, 0 });
    auto cameraEntity2 = SceneBuilder::createCamera(registry, { 10, 5, 10 }, { 0, 0.6f, 1 });

    // --- 2. Create the SINGLE Shared Rendering System ---
    m_renderingSystem = std::make_unique<RenderingSystem>(nullptr);

    // --- 3. Setup the Core UI Layout (No changes here) ---
    m_centralContainer = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_centralContainer);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    m_fixedTopToolbar = new StaticToolbar(this);
    mainLayout->addWidget(m_fixedTopToolbar, 0);
    m_dockManager = new ads::CDockManager();
    mainLayout->addWidget(m_dockManager, 1);
    this->setCentralWidget(m_centralContainer);

    ViewportWidget* viewport1 = new ViewportWidget(m_scene.get(), m_renderingSystem.get(), cameraEntity1, this); // Creates the first viewport widget.
    m_viewports.push_back(viewport1); // Adds the viewport to a list for management.
    m_renderingSystem->setViewportWidget(viewport1); // Associates the renderer with its primary viewport.
    ads::CDockWidget* viewportDock1 = new ads::CDockWidget("3D Viewport 1 (Camera 1)"); // Creates the first dockable viewport.
    viewportDock1->setWidget(viewport1); // Sets the viewport as the content of the dock widget.
    ads::CDockAreaWidget* viewportArea1 = m_dockManager->addDockWidget(ads::LeftDockWidgetArea, viewportDock1); // Docks the first viewport, creating our first column.

    // Create the SECONDARY viewport and dock it to the RIGHT of the FIRST one.
    ViewportWidget* viewport2 = new ViewportWidget(m_scene.get(), m_renderingSystem.get(), cameraEntity2, this); // Creates the second viewport widget.
    m_viewports.push_back(viewport2); // Adds the viewport to the list.
    ads::CDockWidget* viewportDock2 = new ads::CDockWidget("3D Viewport 2 (Camera 2)"); // Creates the second dockable viewport.
    viewportDock2->setWidget(viewport2); // Sets the viewport as the content of the dock widget.
    ads::CDockAreaWidget* viewportArea2 = m_dockManager->addDockWidget(ads::RightDockWidgetArea, viewportDock2, viewportArea1); // This is key: docks viewport 2 to the right OF viewport 1, creating a horizontal split.

    // Create the properties panel and dock it to the RIGHT of the SECOND viewport.
    PropertiesPanel* propertiesPanel = new PropertiesPanel(m_scene.get(), this); // Creates the properties panel widget.
    propertiesPanel->setMinimumWidth(700); // Sets the minimum width of the properties panel. The dock widget will respect this.
    ads::CDockWidget* propertiesDock = new ads::CDockWidget("Grid(s)"); // Creates the properties dock widget.
    propertiesDock->setWidget(propertiesPanel); // Sets the properties panel as the content of the dock widget.
    propertiesDock->setStyleSheet(sidePanelStyle); // Applies your custom style.
    ads::CDockAreaWidget* propertiesArea = m_dockManager->addDockWidget(ads::RightDockWidgetArea, propertiesDock, viewportArea2); // Docks the properties panel to the right OF viewport 2, creating our third column.

    // --- Set Proportions for the three main dock areas ---

    ads::CDockSplitter* mainSplitter = viewportArea1->parentSplitter(); // Gets the custom splitter containing our dock areas.
    if (mainSplitter) // Checks if the splitter is valid.
    {
        // We give it a list of integers representing the initial sizes for a 25:50:25 ratio.
        mainSplitter->setSizes({ 1, 2, 1 });
    }

    // Create the flow visualizer menu and add it as a TAB to the properties area.
    m_flowVisualizerMenu = new FlowVisualizerMenu(this); // Creates the flow visualizer menu widget.
    ads::CDockWidget* flowMenuDock = new ads::CDockWidget("Field Visualizer"); // Creates the flow visualizer dock widget.
    m_flowVisualizerMenu->setMinimumWidth(650); // Sets the minimum width for this widget as well.
    flowMenuDock->setWidget(m_flowVisualizerMenu); // Sets the menu as the content of the dock widget.
    flowMenuDock->setStyleSheet(sidePanelStyle); // Applies your custom style.
    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, flowMenuDock, propertiesArea); // Adds the flow menu as a tab in the properties dock area.
    
    connect(m_flowVisualizerMenu, &FlowVisualizerMenu::settingsChanged,
        this, &MainWindow::onFlowVisualizerSettingsChanged);

    connect(m_flowVisualizerMenu, &FlowVisualizerMenu::transformChanged,
        this, &MainWindow::onFlowVisualizerTransformChanged);

    // --- 5. FINAL, ROBUST INITIALIZATION AND RENDER LOOP START ---

    // 1. Create the timer, but DO NOT START IT YET.
    m_masterRenderTimer = new QTimer(this);
    connect(m_masterRenderTimer, &QTimer::timeout, this, &MainWindow::onMasterRender);

    // Connect to the primary viewport's signal. When its GL context is ready,
    // we will initialize our shared RenderingSystem.
    connect(viewport1, &ViewportWidget::glContextReady, this, [this, viewport1]() {
        // Only initialize the system ONCE.
        if (!m_renderingSystem->isInitialized()) {
            qDebug() << "[LIFECYCLE] Primary viewport context is ready. Initializing RenderingSystem.";

            // To initialize the renderer, we must borrow the primary viewport's context.
            viewport1->makeCurrent();
            m_renderingSystem->initialize(viewport1->width(), viewport1->height());
            viewport1->doneCurrent();

            // Now that the renderer is ready, we can safely start the main render loop.
            qDebug() << "[LIFECYCLE] RenderingSystem is initialized. Starting master render timer.";
            m_masterRenderTimer->start(16); // Target ~60 FPS
        }
        });

    // --- 6. Other Signal/Slot Connections ---
    connect(m_fixedTopToolbar, &StaticToolbar::loadRobotClicked, this, &MainWindow::onLoadRobotClicked);
    connect(viewportDock1, &ads::CDockWidget::topLevelChanged, this, [viewport1](bool isFloating) { /* ... */ });
    connect(viewportDock2, &ads::CDockWidget::topLevelChanged, this, [viewport2](bool isFloating) { /* ... */ });
    connect(m_flowVisualizerMenu, &FlowVisualizerMenu::settingsChanged, this, &MainWindow::onFlowVisualizerSettingsChanged);

    updateVisualizerUI();
    onFlowVisualizerSettingsChanged();

    // --- 7. Final Window Setup ---
    if (menuBar()) {
        menuBar()->setVisible(false);
    }
    resize(1600, 900);
    setWindowTitle("KR Studio ALPHA V0.602");
    setWindowIcon(QIcon(":/icons/kRLogoSquare.png"));
    statusBar()->showMessage("Ready.");
}

void MainWindow::onMasterRender()
{
    // --- 1. LOGIC UPDATES ---
    // This section should run at a fixed rate to ensure smooth and
    // predictable animation and physics, regardless of frame rate.
    if (m_renderingSystem && m_renderingSystem->isInitialized())
    {
        // Use the timer's interval to get a consistent delta time.
        const float deltaTime = static_cast<float>(m_masterRenderTimer->interval()) / 1000.0f;

        // Update all time-based systems
        m_renderingSystem->updateAnimations(m_scene->getRegistry(), deltaTime);
        m_renderingSystem->updateSceneLogic(m_scene->getRegistry(), deltaTime); // Pass deltaTime here

        // Update transforms based on camera state
        m_renderingSystem->updateCameraTransforms(m_scene->getRegistry());
        ViewportWidget::propagateTransforms(m_scene->getRegistry());
    }

    // --- 2. SCHEDULE REPAINT ---
    // Asynchronously schedule a repaint for each viewport. Qt will handle
    // calling paintGL efficiently without blocking the main thread.
    for (ViewportWidget* vp : m_viewports)
    {
        if (vp) {
            vp->update();
        }
    }
}

// The destructor orchestrates a clean shutdown.
MainWindow::~MainWindow()
{
    // Stop the timer to prevent any more render calls during shutdown.
    m_masterRenderTimer->stop();

    if (!m_viewports.empty() && m_viewports[0]) {
        m_viewports[0]->makeCurrent();
        if (m_renderingSystem) {
            // Pass the registry to the shutdown method
            m_renderingSystem->shutdown(m_scene->getRegistry());
            m_renderingSystem.reset();
        }
        m_viewports[0]->doneCurrent();
    }
}

// This slot handles loading robot files from the toolbar button.
void MainWindow::onLoadRobotClicked()
{
    const char* fileFilter =
        "All Supported Robot Files (*.krobot *.urdf *.sdf);;"
        "KStudio Robot (*.krobot);;"
        "URDF Robot (*.urdf);;"
        "SDF Robot (*.sdf);;"
        "All Files (*)";

    QString filePath = QFileDialog::getOpenFileName(this, "Load Robot Model", "", fileFilter);
    if (filePath.isEmpty()) {
        return;
    }

    RobotDescription description;
    bool loadSuccess = false;

    if (filePath.endsWith(".urdf", Qt::CaseInsensitive)) {
        description = URDFParser::parse(filePath.toStdString());
        loadSuccess = true;
    }
    else if (filePath.endsWith(".sdf", Qt::CaseInsensitive)) {
        description = SDFParser::parse(filePath.toStdString());
        loadSuccess = true;
    }
    else if (filePath.endsWith(".krobot", Qt::CaseInsensitive)) {
        description = KRobotParser::parse(filePath.toStdString());
        loadSuccess = true;
    }

    if (!loadSuccess) {
        QMessageBox::critical(this, "File Load Error", "Could not parse the selected robot file.");
        return;
    }

    if (description.needsEnrichment) {
        RobotEnrichmentDialog dialog(description, this);
        if (dialog.exec() == QDialog::Accepted) {
            const RobotDescription& finalDescription = dialog.getFinalDescription();
            QString krobotSavePath = QFileDialog::getSaveFileName(this, "Save Enriched KRobot File", "", "KRobot Files (*.krobot)");
            if (krobotSavePath.isEmpty()) return;

            if (KRobotWriter::save(finalDescription, krobotSavePath.toStdString())) {
                statusBar()->showMessage(QString("Successfully imported and enriched '%1'").arg(QFileInfo(filePath).fileName()));
                SceneBuilder::spawnRobot(*m_scene, finalDescription);
            }
            else {
                QMessageBox::critical(this, "File Save Error", "Could not save the new .krobot file.");
            }
        }
    }
    else {
        statusBar()->showMessage(QString("Successfully loaded robot '%1'").arg(QString::fromStdString(description.name)));
        SceneBuilder::spawnRobot(*m_scene, description);
    }
}

void MainWindow::onFlowVisualizerSettingsChanged()
{
    auto& registry = m_scene->getRegistry();
    auto view = registry.view<FieldVisualizerComponent, TransformComponent>();

    // --- FIX: Use .size_hint() to check if the view is empty ---
    if (view.size_hint() == 0) {
        qWarning() << "onFlowVisualizerSettingsChanged called, but no FieldVisualizerComponent found in scene.";
        return;
    }

    auto visualizerEntity = view.front();
    auto& visualizer = view.get<FieldVisualizerComponent>(visualizerEntity);
    auto& transform = view.get<TransformComponent>(visualizerEntity);

    // --- Update Transform and General Settings ---
    transform.translation = m_flowVisualizerMenu->getFieldPosition();
    transform.rotation = glm::quat(glm::radians(m_flowVisualizerMenu->getFieldOrientation()));
    visualizer.isEnabled = m_flowVisualizerMenu->isMasterVisible();
    visualizer.displayMode = m_flowVisualizerMenu->getDisplayMode();
    visualizer.bounds = m_flowVisualizerMenu->getBounds();

    // --- FIX: Update the correct nested struct based on the display mode ---
    switch (visualizer.displayMode)
    {
    case FieldVisualizerComponent::DisplayMode::Arrows:
    {
        auto& settings = visualizer.arrowSettings;
        settings.density = m_flowVisualizerMenu->getArrowDensity();
        settings.vectorScale = m_flowVisualizerMenu->getArrowBaseSize();
        settings.headScale = m_flowVisualizerMenu->getArrowHeadScale();
        settings.intensityMultiplier = m_flowVisualizerMenu->getArrowIntensityMultiplier();
        settings.cullingThreshold = m_flowVisualizerMenu->getArrowCullingThreshold();
        settings.scaleByLength = m_flowVisualizerMenu->isArrowLengthScaled();
        settings.lengthScaleMultiplier = m_flowVisualizerMenu->getArrowLengthScaleMultiplier();
        settings.scaleByThickness = m_flowVisualizerMenu->isArrowThicknessScaled();
        settings.thicknessScaleMultiplier = m_flowVisualizerMenu->getArrowThicknessScaleMultiplier();
        settings.coloringMode = m_flowVisualizerMenu->getArrowColoringMode();
        settings.xPosColor = m_flowVisualizerMenu->getArrowDirColor(0);
        settings.xNegColor = m_flowVisualizerMenu->getArrowDirColor(1);
        settings.yPosColor = m_flowVisualizerMenu->getArrowDirColor(2);
        settings.yNegColor = m_flowVisualizerMenu->getArrowDirColor(3);
        settings.zPosColor = m_flowVisualizerMenu->getArrowDirColor(4);
        settings.zNegColor = m_flowVisualizerMenu->getArrowDirColor(5);
        settings.intensityGradient = m_flowVisualizerMenu->getArrowIntensityGradient();
        break;
    }
    case FieldVisualizerComponent::DisplayMode::Particles:
    {
        auto& settings = visualizer.particleSettings;
        settings.isSolid = m_flowVisualizerMenu->isParticleSolid();
        settings.particleCount = m_flowVisualizerMenu->getParticleCount();
        settings.lifetime = m_flowVisualizerMenu->getParticleLifetime();
        settings.baseSpeed = m_flowVisualizerMenu->getParticleBaseSpeed();
        settings.speedIntensityMultiplier = m_flowVisualizerMenu->getParticleSpeedIntensityMult();
        settings.baseSize = m_flowVisualizerMenu->getParticleBaseSize();
        settings.peakSizeMultiplier = m_flowVisualizerMenu->getParticlePeakSizeMult();
        settings.minSize = m_flowVisualizerMenu->getParticleMinSize();
        settings.baseGlowSize = m_flowVisualizerMenu->getParticleBaseGlow();
        settings.peakGlowMultiplier = m_flowVisualizerMenu->getParticlePeakGlowMult();
        settings.minGlowSize = m_flowVisualizerMenu->getParticleMinGlow();
        settings.randomWalkStrength = m_flowVisualizerMenu->getParticleRandomWalk();

        settings.coloringMode = m_flowVisualizerMenu->getParticleColoringMode();
        settings.xPosColor = m_flowVisualizerMenu->getParticleDirColor(0);
        settings.xNegColor = m_flowVisualizerMenu->getParticleDirColor(1);
        // ... etc for other particle direction colors
        settings.intensityGradient = m_flowVisualizerMenu->getParticleIntensityGradient();
        settings.lifetimeGradient = m_flowVisualizerMenu->getParticleLifetimeGradient();
        break;
    }
    case FieldVisualizerComponent::DisplayMode::Flow:
    {
        auto& settings = visualizer.flowSettings;
        settings.particleCount = m_flowVisualizerMenu->getFlowParticleCount();
        settings.lifetime = m_flowVisualizerMenu->getFlowLifetime();
        settings.baseSpeed = m_flowVisualizerMenu->getFlowBaseSpeed();
        settings.speedIntensityMultiplier = m_flowVisualizerMenu->getFlowSpeedIntensityMult();
        settings.baseSize = m_flowVisualizerMenu->getFlowBaseSize();
        settings.headScale = m_flowVisualizerMenu->getFlowHeadScale();
        settings.peakSizeMultiplier = m_flowVisualizerMenu->getFlowPeakSizeMult();
        settings.minSize = m_flowVisualizerMenu->getFlowMinSize();
        settings.growthPercentage = m_flowVisualizerMenu->getFlowGrowthPercent();
        settings.shrinkPercentage = m_flowVisualizerMenu->getFlowShrinkPercent();
        settings.randomWalkStrength = m_flowVisualizerMenu->getFlowRandomWalk();
        settings.scaleByLength = m_flowVisualizerMenu->isFlowLengthScaled();
        settings.lengthScaleMultiplier = m_flowVisualizerMenu->getFlowLengthScaleMultiplier();
        settings.scaleByThickness = m_flowVisualizerMenu->isFlowThicknessScaled();
        settings.thicknessScaleMultiplier = m_flowVisualizerMenu->getFlowThicknessScaleMultiplier();

        settings.coloringMode = m_flowVisualizerMenu->getFlowColoringMode();
        // ... etc for flow direction colors and gradients
        break;
    }
    }

    if (visualizer.displayMode == FieldVisualizerComponent::DisplayMode::Arrows) {
        const auto& settings = visualizer.arrowSettings;
        qDebug() << "[SETTINGS APPLIED] Mode: Arrows, Density:"
            << settings.density.x << "x" << settings.density.y << "x" << settings.density.z
            << "Scale:" << settings.vectorScale;
    }

    // Mark the component as dirty so the rendering system knows to update its GPU buffers.
    visualizer.isGpuDataDirty = true;
    qDebug() << "Flow visualizer settings successfully applied to component.";
}

void MainWindow::updateVisualizerUI()
{
    auto& registry = m_scene->getRegistry();
    auto view = registry.view<const FieldVisualizerComponent>(); // View as const
    if (view.size() == 0) return;

    // Get the current state from the component
    const auto& visualizer = view.get<const FieldVisualizerComponent>(view.front());

    // Pass the component to the menu's new public setter function
    m_flowVisualizerMenu->updateControlsFromComponent(visualizer);
}

void MainWindow::onFlowVisualizerTransformChanged()
{
    auto& reg = m_scene->getRegistry();
    auto view = reg.view<TransformComponent, FieldVisualizerComponent>();
    if (view.size_hint() == 0) return;

    auto e = view.front();
    auto [xf, viz] = view.get<TransformComponent, FieldVisualizerComponent>(e);

    xf.translation = m_flowVisualizerMenu->getCentre();
    xf.rotation = glm::quat(glm::radians(m_flowVisualizerMenu->getEuler()));
    viz.isGpuDataDirty = true;          // in case centre moved
}