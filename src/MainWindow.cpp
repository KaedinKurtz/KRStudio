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

#include <QVBoxLayout>
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

    // --- NEW: Set the global SceneProperties in the context ---
    registry.ctx().emplace<SceneProperties>();


    // --- 2. Create Entities in the Scene ---
    // A. Create the Grid Entity (now without fog properties)
    auto gridEntity = registry.create();
    registry.emplace<TagComponent>(gridEntity, "Primary Grid");
    registry.emplace<TransformComponent>(gridEntity);
    auto& gridComp = registry.emplace<GridComponent>(gridEntity);
    gridComp.levels.emplace_back(0.001f, glm::vec3(0.6f, 0.6f, 0.4f), 0.80f, .4f);
    gridComp.levels.emplace_back(0.01f, glm::vec3(0.25f, 0.3f, 0.4f), 2.0f, 1.0f);
    gridComp.levels.emplace_back(0.1f, glm::vec3(0.9f, 0.85f, 0.6f), 10.0f, 5.0f);
    gridComp.levels.emplace_back(1.0f, glm::vec3(0.7f, 0.5f, 0.2f), 200.0f, 7.0f);
    gridComp.levels.emplace_back(10.0f, glm::vec3(0.2f, 0.7f, 0.9f), 200.0f, 20.0f);

    // B. Create two Camera Entities for our two viewports
    auto cameraEntity1 = registry.create();
    auto& camComp1 = registry.emplace<CameraComponent>(cameraEntity1);
    camComp1.camera.setToKnownGoodView();

    auto cameraEntity2 = registry.create();
    auto& camComp2 = registry.emplace<CameraComponent>(cameraEntity2);
    camComp2.camera.forceRecalculateView(glm::vec3(10.0f, 5.0f, 10.0f), glm::vec3(0.0f), 0.0f);

    // C. Create a Cube Entity to represent the robot base for now
    auto cubeEntity = registry.create();
    registry.emplace<TagComponent>(cubeEntity, "Robot Base");
    // Place it at the origin
    registry.emplace<TransformComponent>(cubeEntity);
    // Mark it as renderable
    registry.emplace<RenderableMeshComponent>(cubeEntity);


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

    // --- 4. Setup the FIRST ViewportWidget and its Dock ---
    ViewportWidget* viewport1 = new ViewportWidget(m_scene.get(), cameraEntity1, this);
    viewport1->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewport1->setMinimumSize(400, 300);

    ads::CDockWidget* viewportDock1 = new ads::CDockWidget("3D Viewport 1 (Perspective)");
    viewportDock1->setWidget(viewport1);
    viewportDock1->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, viewportDock1);

    // --- 5. Setup the SECOND ViewportWidget and its Dock ---
    ViewportWidget* viewport2 = new ViewportWidget(m_scene.get(), cameraEntity2, this);
    viewport2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewport2->setMinimumSize(400, 300);

    ads::CDockWidget* viewportDock2 = new ads::CDockWidget("3D Viewport 2 (Top Down)");
    viewportDock2->setWidget(viewport2);
    viewportDock2->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    m_dockManager->addDockWidget(ads::RightDockWidgetArea, viewportDock2);


    // --- 8. Setup the Properties Panel ---
    PropertiesPanel* propertiesPanel = new PropertiesPanel(m_scene.get(), this);

    ads::CDockWidget* propertiesDock = new ads::CDockWidget("Properties");
    propertiesDock->setWidget(propertiesPanel);

    // Dock it to the left of the main viewport area
    m_dockManager->addDockWidget(ads::LeftDockWidgetArea, propertiesDock);


    // --- 6. Setup Signal/Slot connections ---
    connect(viewportDock1, &ads::CDockWidget::topLevelChanged, this, [viewport1](bool isFloating) {
        Q_UNUSED(isFloating);
        if (viewport1) {
            QTimer::singleShot(0, viewport1, [=]() {
                viewport1->getCamera().setToKnownGoodView();
                viewport1->update();
                });
        }
        });

    connect(viewportDock2, &ads::CDockWidget::topLevelChanged, this, [viewport2](bool isFloating) {
        Q_UNUSED(isFloating);
        if (viewport2) {
            QTimer::singleShot(0, viewport2, [=]() {
                viewport2->getCamera().setToKnownGoodView();
                viewport2->update();
                });
        }
        });


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
}
