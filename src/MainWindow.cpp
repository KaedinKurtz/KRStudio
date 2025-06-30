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
        pointEffector.strength = -2.0f;
        pointEffector.distance = 7.0f;
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
    }

    // --- Create Field Visualizers and Effectors ---
    {
        auto& reg = m_scene->getRegistry();
        auto visualizerEntity = reg.create();
        reg.emplace<TagComponent>(visualizerEntity, "Field Visualizer");
        reg.emplace<TransformComponent>(visualizerEntity);
        auto& visualizer = reg.emplace<FieldVisualizerComponent>(visualizerEntity);
        visualizer.bounds = { 20.0f, 5.0f, 20.0f };
        visualizer.density = { 20, 5, 20 };
        visualizer.maxMagnitude = 5.0f;
        visualizer.vectorScale = 0.5f;
        visualizer.colorGradient.push_back({ 0.0f, glm::vec4(0.2f, 0.5f, 1.0f, 1.0f) });
        visualizer.colorGradient.push_back({ 1.0f, glm::vec4(1.0f, 0.3f, 0.3f, 1.0f) });
        auto windSource = reg.create();
        reg.emplace<TagComponent>(windSource, "Wind Source");
        reg.emplace<TransformComponent>(windSource);
        reg.emplace<FieldSourceTag>(windSource);
        auto& directional = reg.emplace<DirectionalEffectorComponent>(windSource);
        directional.direction = { 1.0f, 0.0f, 0.5f };
        directional.strength = 1.5f;
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

    // --- 4. Setup Dockable Widgets (Viewports & Panels) ---
    ViewportWidget* viewport1 = new ViewportWidget(m_scene.get(), m_renderingSystem.get(), cameraEntity1, this);
    m_viewports.push_back(viewport1);
    ads::CDockWidget* viewportDock1 = new ads::CDockWidget("3D Viewport 1 (Perspective)");
    viewportDock1->setWidget(viewport1);
    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, viewportDock1);

    ViewportWidget* viewport2 = new ViewportWidget(m_scene.get(), m_renderingSystem.get(), cameraEntity2, this);
    m_viewports.push_back(viewport2);
    ads::CDockWidget* viewportDock2 = new ads::CDockWidget("3D Viewport 2 (Top Down)");
    viewportDock2->setWidget(viewport2);
    m_dockManager->addDockWidget(ads::RightDockWidgetArea, viewportDock2);

    PropertiesPanel* propertiesPanel = new PropertiesPanel(m_scene.get(), this);
    ads::CDockWidget* propertiesDock = new ads::CDockWidget("Properties");
    propertiesDock->setWidget(propertiesPanel);
    m_dockManager->addDockWidget(ads::RightDockWidgetArea, propertiesDock);

    // --- 5. FINAL, ROBUST INITIALIZATION AND RENDER LOOP START ---

    // 1. Create the timer, but DO NOT START IT YET.
    m_masterRenderTimer = new QTimer(this);
    connect(m_masterRenderTimer, &QTimer::timeout, this, &MainWindow::onMasterRender);

    // 2. Connect the timer's start to the signal from the viewport.
    //    This creates the dependency: "WHEN the renderer is ready, THEN start the render loop."
    connect(viewport1, &ViewportWidget::renderingSystemInitialized, this, [this]() {
        if (!m_masterRenderTimer->isActive()) {
            qDebug() << "[LIFECYCLE] Received renderingSystemInitialized signal. Starting master render timer.";
            m_masterRenderTimer->start(16);
        }
        });

    // --- 6. Other Signal/Slot Connections ---
    connect(m_fixedTopToolbar, &StaticToolbar::loadRobotClicked, this, &MainWindow::onLoadRobotClicked);
    connect(viewportDock1, &ads::CDockWidget::topLevelChanged, this, [viewport1](bool isFloating) { /* ... */ });
    connect(viewportDock2, &ads::CDockWidget::topLevelChanged, this, [viewport2](bool isFloating) { /* ... */ });

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
    // This guard clause will now pass once the timer is started correctly.
    if (!m_renderingSystem || !m_renderingSystem->isInitialized() || m_viewports.empty()) {
        qWarning() << "onMasterRender called but system not ready. Skipping frame.";
        return;
    }

    // "Borrow" the context from the first viewport to run the expensive scene render.
    m_viewports[0]->makeCurrent();
    m_renderingSystem->renderSceneToFBOs(m_scene->getRegistry(), m_viewports[0]->getCamera());
    m_viewports[0]->doneCurrent();

    // Schedule a repaint for all viewports. They will just run their cheap composite pass.
    for (ViewportWidget* vp : m_viewports) {
        vp->update();
    }
}

// The destructor orchestrates a clean shutdown.
MainWindow::~MainWindow()
{
    // On shutdown, make a context current before deleting the renderer's resources
    if (!m_viewports.empty() && m_viewports[0]) {
        m_viewports[0]->makeCurrent();
        if (m_renderingSystem) {
            m_renderingSystem->shutdown();
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
