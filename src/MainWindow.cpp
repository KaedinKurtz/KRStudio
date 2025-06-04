/**
 * @file MainWindow.cpp
 * @brief Implementation of the main application window.
 *
 * This file defines the main window of the application, which is responsible for
 * setting up the user interface layout, including a static toolbar and a
 * flexible docking system for hosting multiple 3D viewports.
 */

#include "MainWindow.hpp"
#include "ViewportWidget.hpp"
#include "StaticToolbar.hpp"

#include <QVBoxLayout>
#include <QWidget>
#include <DockManager.h>  // From the Advanced Docking System library
#include <DockWidget.h>   // From the Advanced Docking System library
#include <QMenuBar>
#include <QStatusBar>
#include <algorithm>

 /**
  * @brief Constructs the main window and initializes its UI components.
  * @param parent The parent widget, typically nullptr for the main window.
  */
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // --- 1. Create the main container and its vertical layout ---
    // This central container will hold all other UI elements.
    m_centralContainer = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_centralContainer);
    // Remove any margins or spacing to have the contents fill the entire area.
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- 2. Add the static toolbar to the TOP of the layout ---
    // This toolbar will remain fixed at the top of the window.
    m_fixedTopToolbar = new StaticToolbar(this);
    // The '0' stretch factor means it takes up only its required height.
    mainLayout->addWidget(m_fixedTopToolbar, 0);

    // --- 3. Initialize the Dock Manager ---
    // The dock manager will control the layout of all dockable widgets.
    // It is created without a parent here and will be added to the layout.
    m_dockManager = new ads::CDockManager();
    // Add the dock manager to the layout, giving it a stretch factor of '1'.
    // This makes it expand to fill all available vertical space below the toolbar.
    mainLayout->addWidget(m_dockManager, 1);

    // --- 4. Set the container as the central widget of the QMainWindow ---
    this->setCentralWidget(m_centralContainer);


    // --- 5. Setup the FIRST ViewportWidget and its Dock ---
    m_viewport = new ViewportWidget(this); // This will be your first 3D view

    // ---> NEW SIZING HINTS FOR THE CONTENT AND ITS DOCK <---
    // a) Tell the viewport (QOpenGLWidget) that it wants to expand.
    // This is a hint to the layout system that it should grow to fill available space.
    m_viewport->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // b) Give the viewport a substantial minimum size. This influences its initial
    // sizeHint() and prevents it from starting too small.
    m_viewport->setMinimumSize(600, 400);

    // Create a dock widget to contain the viewport. This is the handle that
    // the user can drag, float, and dock.
    m_viewportDock = new ads::CDockWidget("3D Viewport 1");
    m_viewportDock->setWidget(m_viewport); // Place the viewport inside the dock widget.

    // c) Also tell the dock widget itself that it wants to expand.
    m_viewportDock->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Configure the features of the dock widget.
    m_viewportDock->setFeature(ads::CDockWidget::DockWidgetClosable, false); // Cannot be closed.
    m_viewportDock->setFeature(ads::CDockWidget::DockWidgetMovable, true);   // Can be moved.
    m_viewportDock->setFeature(ads::CDockWidget::DockWidgetFloatable, true); // Can be undocked into a floating window.

    // Add the first viewport to the central area of the docking manager.
    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, m_viewportDock);


    // --- 6. Setup the SECOND ViewportWidget and its Dock ---
    ViewportWidget* m_viewport2 = new ViewportWidget(this);
    m_viewport2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_viewport2->setMinimumSize(600, 400);

    ads::CDockWidget* m_viewportDock2 = new ads::CDockWidget("3D Viewport 2");
    m_viewportDock2->setWidget(m_viewport2);

    m_viewportDock2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_viewportDock2->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    m_viewportDock2->setFeature(ads::CDockWidget::DockWidgetMovable, true);
    m_viewportDock2->setFeature(ads::CDockWidget::DockWidgetFloatable, true);

    // Add the second viewport to the right side of the docking area,
    // creating a split view with the first viewport.
    m_dockManager->addDockWidget(ads::RightDockWidgetArea, m_viewportDock2);


    // --- 7. Setup Signal/Slot connections to reset viewports on state change ---

    // Connection for the FIRST viewport.
    // The `topLevelChanged` signal is emitted when a dock widget is floated or docked.
    connect(m_viewportDock, &ads::CDockWidget::topLevelChanged, this, [this](bool isFloating) {
        Q_UNUSED(isFloating); // We don't need to use the 'isFloating' flag.
        if (m_viewport) {
            // Use QTimer::singleShot with a 0ms delay to defer the execution.
            // This ensures the operation runs after the current event (docking) has
            // been fully processed, preventing potential rendering glitches.
            QTimer::singleShot(0, m_viewport, [viewport_ptr = m_viewport]() {
                if (viewport_ptr) {
                    // Reset the camera to its default position and schedule a repaint.
                    viewport_ptr->getCamera().setToKnownGoodView();
                    viewport_ptr->update();
                }
                });
        }
        });

    // Connection for the SECOND viewport.
    // The logic is identical to the first viewport's connection.
    connect(m_viewportDock2, &ads::CDockWidget::topLevelChanged, this, [m_viewport2](bool isFloating) {
        Q_UNUSED(isFloating);
        if (m_viewport2) {
            // We capture m_viewport2 directly in the lambda.
            QTimer::singleShot(0, m_viewport2, [viewport_ptr = m_viewport2]() {
                if (viewport_ptr) {
                    viewport_ptr->getCamera().setToKnownGoodView();
                    viewport_ptr->update();
                }
                });
        }
        });

    // --- 8. Final Window Setup ---
    // Hide the default QMainWindow menu bar as we are using a custom toolbar.
    if (menuBar()) {
        menuBar()->setVisible(false);
    }
    // Set a good default size for the main window.
    resize(1600, 900);
    // Set the window title and icon.
    setWindowTitle("KR Studio V0.1");
    setWindowIcon(QIcon(":/icons/kRLogoSquare.png")); // Icon loaded from Qt resources.
    // Set an initial message in the status bar.
    statusBar()->showMessage("Ready.");
}

/**
 * @brief Destroys the MainWindow.
 *
 * Qt's parent-child ownership system will handle the deletion of all child
 * widgets, so no explicit cleanup is needed here for the dynamically allocated UI elements.
 */
MainWindow::~MainWindow()
{
}