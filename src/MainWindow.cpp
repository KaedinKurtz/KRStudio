/**
 * @file MainWindow.cpp
 * @brief Implementation of the main application window.
 *
 * This file defines the main window of the application. It is responsible for
 * creating and owning the central Scene, initializing the UI layout (toolbar,
 * docking system), and creating the ViewportWidgets that render the scene.
 */

#include "MainWindow.hpp"
#include "StaticToolbar.hpp"
#include "PropertiesPanel.hpp"
#include "Scene.hpp"
#include "ViewportWidget.hpp"
#include "components.hpp" 
#include "Camera.hpp"
#include "KRobotParser.hpp"
#include "KRobotWriter.hpp"
#include "URDFParser.hpp"
#include "URDFImporterDialog.hpp"
#include "SDFParser.hpp"
#include "RobotEnrichmentDialog.hpp"
#include "SceneBuilder.hpp"

#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QWidget>
#include <QMenuBar>
#include <QStatusBar>
#include <QTimer>
#include <DockManager.h>
#include <DockWidget.h>
#include <DockAreaWidget.h>


MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // --- 1. Create the Scene ---
    m_scene = std::make_unique<Scene>();
    auto& registry = m_scene->getRegistry();
    registry.ctx().emplace<SceneProperties>();

    // --- 2. Create Entities in the Scene ---
    // (Grid creation is unchanged)
    auto gridEntity = registry.create();
    registry.emplace<TagComponent>(gridEntity, "Primary Grid");
    registry.emplace<TransformComponent>(gridEntity);
    auto& gridComp = registry.emplace<GridComponent>(gridEntity);
    gridComp.levels.emplace_back(0.001f, glm::vec3(0.6f, 0.6f, 0.4f), 0.80f, .4f);
    gridComp.levels.emplace_back(0.01f, glm::vec3(0.25f, 0.3f, 0.4f), 2.0f, 1.0f);
    gridComp.levels.emplace_back(0.1f, glm::vec3(0.9f, 0.85f, 0.6f), 10.0f, 5.0f);
    gridComp.levels.emplace_back(1.0f, glm::vec3(0.7f, 0.5f, 0.2f), 200.0f, 7.0f);
    gridComp.levels.emplace_back(10.0f, glm::vec3(0.2f, 0.7f, 0.9f), 200.0f, 20.0f);

    // (Camera creation is unchanged)
    auto cameraEntity1 = registry.create();
    auto& camComp1 = registry.emplace<CameraComponent>(cameraEntity1);
    camComp1.camera.setToKnownGoodView();

    auto cameraEntity2 = registry.create();
    auto& camComp2 = registry.emplace<CameraComponent>(cameraEntity2);
    camComp2.camera.forceRecalculateView(glm::vec3(10.0f, 5.0f, 10.0f), glm::vec3(0.0f), 0.0f);

    // --- Create the Cube Entity with Full Component Data ---
    //auto cubeEntity = registry.create();
    //registry.emplace<TagComponent>(cubeEntity, "Robot Base");
   // registry.emplace<TransformComponent>(cubeEntity);
   // registry.emplace<RenderableMeshComponent>(cubeEntity); // The simple tag
  //  registry.emplace<IntersectionComponent>(cubeEntity);   // The empty container for results



    // --- 3. Setup the UI Layout ---
    m_centralContainer = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_centralContainer);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_fixedTopToolbar = new StaticToolbar(this);
    mainLayout->addWidget(m_fixedTopToolbar, 0);

    m_dockManager = new ads::CDockManager();
    mainLayout->addWidget(m_dockManager, 1);

    this->setCentralWidget(m_centralContainer);

    // --- 4. Setup Viewports ---
    m_viewport1 = new ViewportWidget(m_scene.get(), cameraEntity1, this);
    m_viewport1->setMinimumSize(400, 300);
    ads::CDockWidget* viewportDock1 = new ads::CDockWidget("3D Viewport 1 (Perspective)");
    viewportDock1->setWidget(m_viewport1);
    viewportDock1->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, viewportDock1);

    m_viewport2 = new ViewportWidget(m_scene.get(), cameraEntity2, this);
    m_viewport2->setMinimumSize(400, 300);
    ads::CDockWidget* viewportDock2 = new ads::CDockWidget("3D Viewport 2 (Top Down)");
    viewportDock2->setWidget(m_viewport2);
    viewportDock2->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    m_dockManager->addDockWidget(ads::RightDockWidgetArea, viewportDock2);


    // --- 8. Setup the Properties Panel ---
    PropertiesPanel* propertiesPanel = new PropertiesPanel(m_scene.get(), this);
    // FIX: Give the panel content itself a minimum width.
    propertiesPanel->setMinimumWidth(700);

    ads::CDockWidget* propertiesDock = new ads::CDockWidget("Properties");
    propertiesDock->setWidget(propertiesPanel);
    // FIX: Tell the dock widget to respect the minimum size of its content (the PropertiesPanel).
    propertiesDock->setMinimumSizeHintMode(ads::CDockWidget::MinimumSizeHintFromContentMinimumSize);
    // Dock it into the same area as the second viewport. The splitter will handle it.
    m_dockManager->addDockWidget(ads::RightDockWidgetArea, propertiesDock);

    // --- 6. Setup Signal/Slot connections ---
    connect(viewportDock1, &ads::CDockWidget::topLevelChanged, this, [this](bool isFloating) {
        Q_UNUSED(isFloating);
        if (m_viewport1) {
            QTimer::singleShot(0, m_viewport1, [=]() {
                m_viewport1->getCamera().setToKnownGoodView();
                m_viewport1->update();
            });
        }
    });

    connect(viewportDock2, &ads::CDockWidget::topLevelChanged, this, [this](bool isFloating) {
        Q_UNUSED(isFloating);
        if (m_viewport2) {
            QTimer::singleShot(0, m_viewport2, [=]() {
                m_viewport2->getCamera().setToKnownGoodView();
                m_viewport2->update();
            });
        }
    });

    connect(m_fixedTopToolbar, &StaticToolbar::loadRobotClicked, this, &MainWindow::onLoadRobotClicked);


    // --- 7. Final Window Setup ---
    if (menuBar()) {
        menuBar()->setVisible(false);
    }
    resize(1600, 900);
    setWindowTitle("KR Studio - ECS Refactor");
    setWindowIcon(QIcon(":/icons/kRLogoSquare.png"));
    statusBar()->showMessage("Ready.");

}

MainWindow::~MainWindow()
{
    delete m_viewport1;
    delete m_viewport2;
}

void MainWindow::onLoadRobotClicked()
{
    // --- Step 1: Create a unified file dialog filter ---
    // This filter string allows the user to see all supported files at once,
    // or filter by a specific type. The format is "Description(*.ext1 *.ext2);;".
    const char* fileFilter =
        "All Supported Robot Files (*.krobot *.urdf *.sdf);;"
        "KStudio Robot (*.krobot);;"
        "URDF Robot (*.urdf);;"
        "SDF Robot (*.sdf);;"
        "All Files (*)";

    // --- Step 2: Open the dialog and get the selected file path ---
    QString filePath = QFileDialog::getOpenFileName(this, "Load Robot Model", "", fileFilter); // Opens the file dialog with our filter.
    if (filePath.isEmpty()) { // Checks if the user clicked "Cancel".
        return; // after a line, explain what it does
    }

    // --- Step 3: The Dispatcher - Call the correct parser based on file extension ---
    RobotDescription description; // This will hold the data from whichever parser succeeds.
    bool requiresEnrichment = false; // A flag to track if we need to open the Importer Dialog.

    try {
        if (filePath.endsWith(".krobot", Qt::CaseInsensitive)) {
            description = KRobotParser::parse(filePath.toStdString()); // Use the KRobot parser.
            requiresEnrichment = false; // .krobot files are complete, no enrichment needed.

        }
        else if (filePath.endsWith(".urdf", Qt::CaseInsensitive)) {
            description = URDFParser::parse(filePath.toStdString()); // Use the URDF parser.
            requiresEnrichment = true; // URDF is a partial format, requires user enrichment.

        }
        else if (filePath.endsWith(".sdf", Qt::CaseInsensitive)) {
            description = SDFParser::parse(filePath.toStdString()); // Use the SDF parser.
            requiresEnrichment = true; // SDF is also partial, requires user enrichment.

        }
        else {
            QMessageBox::warning(this, "Unsupported File", "The selected file type is not supported."); // Warn the user if the extension is unknown.
            return; // Stop the process.
        }
    }
    catch (const std::exception& e) {
        QMessageBox::critical(this, "Parse Error", e.what()); // Catches any errors thrown by the parsers.
        return; // Stop the process.
    }

    // --- Step 4: The Workflow Fork - Decide what to do with the parsed data ---
    if (requiresEnrichment) {
        RobotEnrichmentDialog dialog(description, this);
        if (dialog.exec() == QDialog::Accepted) {
            const RobotDescription& finalDescription = dialog.getFinalDescription();
            QString krobotSavePath = QFileDialog::getSaveFileName(this, "Save Enriched KRobot File", "", "KRobot Files (*.krobot)");
            if (krobotSavePath.isEmpty()) return;

            if (KRobotWriter::save(finalDescription, krobotSavePath.toStdString())) {
                statusBar()->showMessage(QString("Successfully imported and enriched '%1'").arg(QFileInfo(filePath).fileName()));

                qDebug() << "[MainWindow] Spawning robot into MAIN scene.";
                // --- THIS IS THE FIX ---
                // This call now adds the new robot entities to our main scene,
                // which already contains the grid and cameras.
                SceneBuilder::spawnRobot(*m_scene, finalDescription);
            }
            else {
                QMessageBox::critical(this, "File Save Error", "Could not save the new .krobot file.");
            }
        }
    }
    else { // This handles loading a .krobot file directly
        statusBar()->showMessage(QString("Successfully loaded robot '%1'").arg(QString::fromStdString(description.name)));

        qDebug() << "[MainWindow] Spawning robot into MAIN scene.";
        // --- THIS IS THE FIX ---
        // Spawn the robot from the .krobot file directly into the MAIN scene.
        SceneBuilder::spawnRobot(*m_scene, description);
    }
}