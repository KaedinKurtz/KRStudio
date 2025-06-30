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

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // --- 1. Create the Scene and Initial Entities ---
    m_scene = std::make_unique<Scene>();

    m_renderingSystem = std::make_unique<RenderingSystem>(nullptr);

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

        // NOTE: The IntersectionComponent has been removed and is no longer needed here.
        registry.emplace<FieldSourceTag>(cubeEntity);

        // 2. Add the specific effector component. Let's make it a repulsor.
        auto& pointEffector = registry.emplace<MeshEffectorComponent>(cubeEntity);
        pointEffector.strength = -2.0f; // A strong positive value for repulsion
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

    {
        using SB = SceneBuilder;
        auto& reg = m_scene->getRegistry();

        // Catmull–Rom (big red swoop) - This one should already be there
        auto CREntity = SB::makeCR(reg,
            { {-2,0,-1}, {-2,0, 1}, { 2,0, 1}, { 2,0,-1} },
            { 0.9f,0.9f,0.9f,1 }, { 0.9f,0.3f,0.3f,1 }, 18.0f);
        reg.emplace<PulsingSplineTag>(CREntity);

        // Parametric (blue helix) - This one should already be there
        auto ParamEntity = SB::makeParam(reg,
            [](float t) { return glm::vec3(2 * std::cos(6.28f * 3 * t), 6 * t, 2 * std::sin(6.28f * 3 * t)); },
            { 0.9f,0.9f,0.9f,1 }, { 0.2f,0.6f,1.0f,1 }, 12.0f);
        reg.emplace<PulsingSplineTag>(ParamEntity);

        // --- Add these new examples ---

        // Linear (sharp green line)
        auto LinearEntity = SB::makeLinear(reg,
            { {-5, 0.1, -5}, {-5, 2, -5}, {0, 2, -5}, {0, 0.1, -5} },
            { 0.9f,0.9f,0.9f,1 }, { 0.1f, 1.0f, 0.2f, 1 }, 18.0f);
        reg.emplace<PulsingSplineTag>(LinearEntity);

        // Bezier (smooth yellow curve)
        auto bezierEntity = SB::makeBezier(reg,
            { {5, 0.1, -5}, {5, 4, -5}, {2, 4, -5}, {2, 0.1, -5} },
            { 0.9f,0.9f,0.9f,1 }, { 1.0f, 0.9f, 0.2f, 1 }, 18.0f);
        reg.emplace<PulsingSplineTag>(bezierEntity);
    }

    {
        auto& registry = m_scene->getRegistry();

        // 1. The Visualizer Entity
        // This entity defines the volume in which the field will be rendered.
        auto visualizerEntity = registry.create();
        registry.emplace<TagComponent>(visualizerEntity, "Field Visualizer");
        registry.emplace<TransformComponent>(visualizerEntity); // Positioned at the world origin
        auto& visualizer = registry.emplace<FieldVisualizerComponent>(visualizerEntity);
        visualizer.bounds = { 20.0f, 5.0f, 20.0f };
        visualizer.density = { 20, 5, 20 };
        visualizer.maxMagnitude = 5.0f; // Expecting forces up to this strength
        visualizer.vectorScale = 0.5f; // Make arrows a bit smaller
        // Create a default blue-to-red color gradient
        visualizer.colorGradient.push_back({ 0.0f, glm::vec4(0.2f, 0.5f, 1.0f, 1.0f) }); // Blue for low magnitude
        visualizer.colorGradient.push_back({ 1.0f, glm::vec4(1.0f, 0.3f, 0.3f, 1.0f) }); // Red for high magnitude


        // 2. A Directional Effector (like wind)
        auto windSource = registry.create();
        registry.emplace<TagComponent>(windSource, "Wind Source");
        registry.emplace<TransformComponent>(windSource);
        registry.emplace<FieldSourceTag>(windSource); // REQUIRED tag
        auto& directional = registry.emplace<DirectionalEffectorComponent>(windSource);
        directional.direction = { 1.0f, 0.0f, 0.5f }; // Points somewhat diagonally
        directional.strength = 1.5f;

        // 3. A Point Effector (a repulsor)
        auto repulsorSource = registry.create();
        registry.emplace<TagComponent>(repulsorSource, "Repulsor");
        auto& repulsorTransform = registry.emplace<TransformComponent>(repulsorSource);
        repulsorTransform.translation = { 5.0f, 1.0f, 0.0f }; // Position it off to the side
        registry.emplace<FieldSourceTag>(repulsorSource); // REQUIRED tag
        auto& point = registry.emplace<PointEffectorComponent>(repulsorSource);
        point.strength = 10.0f; // Positive strength = repulsion
        point.radius = 4.0f;
        point.falloff = PointEffectorComponent::FalloffType::Linear;
    }

    // --- Create initial cameras for the viewports ---
    auto cameraEntity1 = SceneBuilder::createCamera(registry, { 0, 2, 5 }, { 1, 0.3f, 0 });
    auto cameraEntity2 = SceneBuilder::createCamera(registry, { 10, 5, 10 }, { 0, 0.6f, 1 });


    // --- 3. Setup the Core UI Layout ---
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
    // Create the viewports and pass them a pointer to the shared (but still uninitialized) rendering system.
    ViewportWidget* viewport1 = new ViewportWidget(
        m_scene.get(),
        m_renderingSystem.get(),      // ← shared renderer
        cameraEntity1,
        this);

    m_viewports.push_back(viewport1);
    ads::CDockWidget* viewportDock1 = new ads::CDockWidget("3D Viewport 1 (Perspective)");
    viewportDock1->setWidget(viewport1);
    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, viewportDock1);

    // ---------------------------------------------------------------------
    ViewportWidget* viewport2 = new ViewportWidget(
        m_scene.get(),
        m_renderingSystem.get(),      // ← same renderer
        cameraEntity2,
        this);

    m_viewports.push_back(viewport2);
    ads::CDockWidget* viewportDock2 = new ads::CDockWidget("3D Viewport 2 (Top Down)");
    viewportDock2->setWidget(viewport2);
    m_dockManager->addDockWidget(ads::RightDockWidgetArea, viewportDock2);

    PropertiesPanel* propertiesPanel = new PropertiesPanel(m_scene.get(), this);
    ads::CDockWidget* propertiesDock = new ads::CDockWidget("Properties");
    propertiesDock->setWidget(propertiesPanel);
    m_dockManager->addDockWidget(ads::RightDockWidgetArea, propertiesDock);

    // --- 5. Setup Signal/Slot Connections ---
    connect(m_fixedTopToolbar, &StaticToolbar::loadRobotClicked, this, &MainWindow::onLoadRobotClicked);

    connect(viewportDock1, &ads::CDockWidget::topLevelChanged, this, [viewport1](bool isFloating) {
        if (isFloating && viewport1) {
            QTimer::singleShot(0, viewport1, [=]() {
                viewport1->getCamera().setToKnownGoodView();
                viewport1->update();
                });
        }
        });

    connect(viewportDock2, &ads::CDockWidget::topLevelChanged, this, [viewport2](bool isFloating) {
        if (isFloating && viewport2) {
            QTimer::singleShot(0, viewport2, [=]() {
                viewport2->getCamera().forceRecalculateView(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f), 0.0f);
                viewport2->update();
                });
        }
        });


    // --- 6. Final Window Setup ---
    if (menuBar()) {
        menuBar()->setVisible(false);
    }
    resize(1600, 900);
    setWindowTitle("KR Studio ALPHA V0.602");
    setWindowIcon(QIcon(":/icons/kRLogoSquare.png"));
    statusBar()->showMessage("Ready.");
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
