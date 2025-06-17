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


        // Catmull–Rom (big swoop)
        SB::makeCR(reg,
            { {-2,0,-1}, {-2,0, 1}, { 2,0, 1}, { 2,0,-1} },
            { 0.9f,0.3f,0.3f,1 });

        // blue 3-turn helix, 6 m tall
        SB::makeParam(reg,
            [](float t) { return glm::vec3(2 * std::cos(6.28f * t),
                6 * t,
                2 * std::sin(6.28f * t)); },
            { 0.2f,0.6f,1.0f,1 });
    }

    // --- Create initial cameras for the viewports ---
    auto cameraEntity1 = SceneBuilder::createCamera(registry,
        { 0, 2, 5 }, { 1,0.3f,0 });
    auto cameraEntity2 = SceneBuilder::createCamera(registry,
        { 10, 5,10 }, { 0,0.6f,1 });

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
    ViewportWidget* viewport1 = new ViewportWidget(m_scene.get(), cameraEntity1, this);
    m_viewports.push_back(viewport1);
    ads::CDockWidget* viewportDock1 = new ads::CDockWidget("3D Viewport 1 (Perspective)");
    viewportDock1->setWidget(viewport1);
    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, viewportDock1);

    ViewportWidget* viewport2 = new ViewportWidget(m_scene.get(), cameraEntity2, this);
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
    setWindowTitle("KR Studio - ECS Refactor");
    setWindowIcon(QIcon(":/icons/kRLogoSquare.png"));
    statusBar()->showMessage("Ready.");
}

// The destructor orchestrates a clean shutdown.
MainWindow::~MainWindow()
{
    //qDebug() << "=====================================================";
    //qDebug() << "[LIFETIME] MainWindow Destructor ~MainWindow() STARTING.";
    //qDebug() << "[LIFETIME] MainWindow: Commanding all viewports to shut down...";
    for (ViewportWidget* viewport : m_viewports) {
        if (viewport) {
            viewport->shutdown();
        }
    }
    //qDebug() << "[LIFETIME] MainWindow: All viewports have been shut down.";
    //qDebug() << "[LIFETIME] MainWindow: About to destroy Scene...";
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
