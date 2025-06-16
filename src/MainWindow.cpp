/**
 * @file MainWindow.cpp
 * @brief Implementation of the main application window.
 */

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
    m_scene = std::make_unique<Scene>(); // The MainWindow creates and owns the single Scene.
    auto& registry = m_scene->getRegistry();
    registry.ctx().emplace<SceneProperties>(); // Set up scene-wide properties like fog.

    // --- Create a default grid ---
    {
        auto gridEntity = registry.create();
        registry.emplace<TagComponent>(gridEntity, "Primary Grid");
        registry.emplace<TransformComponent>(gridEntity);
        auto& gridComp = registry.emplace<GridComponent>(gridEntity);
        gridComp.levels.emplace_back(0.001f, glm::vec3(0.6f, 0.6f, 0.4f), 0.80f, .4f);
        gridComp.levels.emplace_back(0.01f, glm::vec3(0.25f, 0.3f, 0.4f), 2.0f, 1.0f);
        gridComp.levels.emplace_back(0.1f, glm::vec3(0.9f, 0.85f, 0.6f), 10.0f, 5.0f);
        gridComp.levels.emplace_back(1.0f, glm::vec3(0.7f, 0.5f, 0.2f), 200.0f, 7.0f);
        gridComp.levels.emplace_back(10.0f, glm::vec3(0.2f, 0.7f, 0.9f), 200.0f, 20.0f);
    }

    // --- Create initial cameras for the viewports ---
    auto cameraEntity1 = SceneBuilder::createCamera(registry, { 0.0f, 2.0f, 5.0f }); // Creates a camera entity using the helper.
    auto cameraEntity2 = SceneBuilder::createCamera(registry, { 10.0f, 5.0f, 10.0f }); // Creates a second camera entity.


    // --- 3. Setup the Core UI Layout ---
    m_centralContainer = new QWidget(this); // This container holds the toolbar and the docking area.
    QVBoxLayout* mainLayout = new QVBoxLayout(m_centralContainer);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_fixedTopToolbar = new StaticToolbar(this); // The static toolbar at the top.
    mainLayout->addWidget(m_fixedTopToolbar, 0);

    m_dockManager = new ads::CDockManager(); // The advanced docking system that manages all panels.
    mainLayout->addWidget(m_dockManager, 1);

    this->setCentralWidget(m_centralContainer); // Set the container as the main window's central widget.


    // --- 4. Setup Dockable Widgets (Viewports & Panels) ---
    ViewportWidget* viewport1 = new ViewportWidget(m_scene.get(), cameraEntity1, this);
    viewport1->setMinimumSize(400, 300);
    ads::CDockWidget* viewportDock1 = new ads::CDockWidget("3D Viewport 1 (Perspective)");
    viewportDock1->setWidget(viewport1);
    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, viewportDock1);
    m_viewports.push_back(viewport1); // FINAL FIX: Add the viewport to our list.

    ViewportWidget* viewport2 = new ViewportWidget(m_scene.get(), cameraEntity2, this);
    viewport2->setMinimumSize(400, 300);
    ads::CDockWidget* viewportDock2 = new ads::CDockWidget("3D Viewport 2 (Top Down)");
    viewportDock2->setWidget(viewport2);
    m_dockManager->addDockWidget(ads::RightDockWidgetArea, viewportDock2);
    m_viewports.push_back(viewport2); // FINAL FIX: Add the second viewport to our list.


    PropertiesPanel* propertiesPanel = new PropertiesPanel(m_scene.get(), this);
    propertiesPanel->setMinimumWidth(700);
    ads::CDockWidget* propertiesDock = new ads::CDockWidget("Properties");
    propertiesDock->setWidget(propertiesPanel);
    propertiesDock->setMinimumSizeHintMode(ads::CDockWidget::MinimumSizeHintFromContentMinimumSize);
    m_dockManager->addDockWidget(ads::RightDockWidgetArea, propertiesDock);


    // --- 5. Setup Signal/Slot Connections ---
    connect(m_fixedTopToolbar, &StaticToolbar::loadRobotClicked, this, &MainWindow::onLoadRobotClicked);

    connect(viewportDock1, &ads::CDockWidget::topLevelChanged, this, [viewport1](bool isFloating) {
        if (isFloating && viewport1) {
            QTimer::singleShot(0, viewport1, [=]() {
                viewport1->getCamera().setToKnownGoodView(); // Resets the camera to a good default view.
                viewport1->update();
                });
        }
        });

    connect(viewportDock2, &ads::CDockWidget::topLevelChanged, this, [viewport2](bool isFloating) {
        if (isFloating && viewport2) {
            QTimer::singleShot(0, viewport2, [=]() {
                viewport2->getCamera().forceRecalculateView(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f), 0.0f); // Sets a top-down view.
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

// FIX: Added the missing destructor definition.
MainWindow::~MainWindow()
{
    //qDebug() << "=====================================================";
    //qDebug() << "[LIFETIME] MainWindow Destructor ~MainWindow() STARTING.";

    // STEP 1: Explicitly tell all viewports to clean up their GPU resources.
    // This happens FIRST, while the Scene and OpenGL context are still fully valid.
    //qDebug() << "[LIFETIME] MainWindow: Commanding all viewports to shut down...";
    for (ViewportWidget* viewport : m_viewports) {
        if (viewport) {
            viewport->shutdown();
        }
    }
    //qDebug() << "[LIFETIME] MainWindow: All viewports have been shut down.";

    // STEP 2: The unique_ptr for m_scene is automatically destroyed here,
    // which is now safe because no one is using it anymore.
    //qDebug() << "[LIFETIME] MainWindow: About to destroy Scene...";
}

// FIX: Added the missing slot implementation.
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
    bool requiresEnrichment = false;

    try {
        if (filePath.endsWith(".urdf", Qt::CaseInsensitive)) {
            description = URDFParser::parse(filePath.toStdString());
            requiresEnrichment = true;
        }
        else if (filePath.endsWith(".sdf", Qt::CaseInsensitive)) {
            description = SDFParser::parse(filePath.toStdString());
            requiresEnrichment = true;
        }
        else if (filePath.endsWith(".krobot", Qt::CaseInsensitive)) {
            description = KRobotParser::parse(filePath.toStdString());
            requiresEnrichment = false;
        }
        else {
            QMessageBox::warning(this, "Unsupported File", "The selected file type is not supported.");
            return;
        }
    }
    catch (const std::exception& e) {
        QMessageBox::critical(this, "Parse Error", e.what());
        return;
    }

    if (requiresEnrichment) {
        RobotEnrichmentDialog dialog(description, this);
        if (dialog.exec() == QDialog::Accepted) {
            const RobotDescription& finalDescription = dialog.getFinalDescription();
            QString krobotSavePath = QFileDialog::getSaveFileName(this, "Save Enriched KRobot File", "", "KRobot Files (*.krobot)");
            if (krobotSavePath.isEmpty()) return;

            if (KRobotWriter::save(finalDescription, krobotSavePath.toStdString())) {
                statusBar()->showMessage(QString("Successfully imported and enriched '%1'").arg(QFileInfo(filePath).fileName()));
                SceneBuilder::spawnRobot(*m_scene, finalDescription); // Spawns the robot into our main scene.
            }
            else {
                QMessageBox::critical(this, "File Save Error", "Could not save the new .krobot file.");
            }
        }
    }
    else {
        statusBar()->showMessage(QString("Successfully loaded robot '%1'").arg(QString::fromStdString(description.name)));
        SceneBuilder::spawnRobot(*m_scene, description); // Spawns the robot into our main scene.
    }
}
