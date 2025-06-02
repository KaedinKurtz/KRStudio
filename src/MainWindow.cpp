// src/MainWindow.cpp
#include "MainWindow.hpp"
#include "ViewportWidget.hpp"
#include "StaticToolbar.hpp"

#include <QVBoxLayout>
#include <QWidget>
#include <DockManager.h>
#include <DockWidget.h>
#include <QMenuBar>
#include <QStatusBar>
#include <algorithm>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // 1. Create the main container and its vertical layout
    m_centralContainer = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_centralContainer);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 2. Add the static toolbar to the TOP
    m_fixedTopToolbar = new StaticToolbar(this);
    mainLayout->addWidget(m_fixedTopToolbar, 0);

    // 3. Create the host widget for the docking system
    // Remove any internal layout from m_adsHostWidget; it's just a container.

    // 4. Set our container as the central widget
    this->setCentralWidget(m_centralContainer);

    // 5. Initialize the Dock Manager and parent it to our host widget
    m_dockManager = new ads::CDockManager();

    mainLayout->addWidget(m_dockManager, 1);

    // 6. Setup the FIRST ViewportWidget and its Dock
    m_viewport = new ViewportWidget(this); // This will be your first viewport

    // ---> NEW SIZING HINTS FOR THE CONTENT AND ITS DOCK <---
    // a) Tell the m_viewport (QOpenGLWidget) that it wants to expand
    m_viewport->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // b) Give m_viewport a substantial minimum size to influence its sizeHint
    m_viewport->setMinimumSize(600, 400); // Adjust if needed

    m_viewportDock = new ads::CDockWidget("3D Viewport 1"); // Renamed for clarity
    m_viewportDock->setWidget(m_viewport);

    // c) Also tell the m_viewportDock that it wants to expand
    m_viewportDock->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_viewportDock->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    m_viewportDock->setFeature(ads::CDockWidget::DockWidgetMovable, true);
    m_viewportDock->setFeature(ads::CDockWidget::DockWidgetFloatable, true);

    // Use CenterDockWidgetArea for the first viewport
    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, m_viewportDock);

    // 7. Setup the SECOND ViewportWidget and its Dock
    ViewportWidget* m_viewport2 = new ViewportWidget(this); // Create a second ViewportWidget
    m_viewport2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_viewport2->setMinimumSize(600, 400); // Adjust if needed

    ads::CDockWidget* m_viewportDock2 = new ads::CDockWidget("3D Viewport 2"); // Create a second CDockWidget
    m_viewportDock2->setWidget(m_viewport2);

    m_viewportDock2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_viewportDock2->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    m_viewportDock2->setFeature(ads::CDockWidget::DockWidgetMovable, true);
    m_viewportDock2->setFeature(ads::CDockWidget::DockWidgetFloatable, true);

    // Add the second viewport dock. You can choose a different area, or let the dock manager arrange it.
    // For example, to place it next to the first one (e.g., to the right):
    m_dockManager->addDockWidget(ads::RightDockWidgetArea, m_viewportDock2);

    // Viewport reset connection for the first viewport
    connect(m_viewportDock, &ads::CDockWidget::topLevelChanged, this, [this](bool isFloating) {
        Q_UNUSED(isFloating);
        if (m_viewport) {
            QTimer::singleShot(0, m_viewport, [viewport_ptr = m_viewport]() {
                if (viewport_ptr) {
                    viewport_ptr->getCamera().setToKnownGoodView();
                    viewport_ptr->update();
                }
                });
        }
        });

    // Viewport reset connection for the second viewport
    connect(m_viewportDock2, &ads::CDockWidget::topLevelChanged, this, [m_viewport2](bool isFloating) {
        Q_UNUSED(isFloating);
        if (m_viewport2) {
            QTimer::singleShot(0, m_viewport2, [viewport_ptr = m_viewport2]() {
                if (viewport_ptr) {
                    viewport_ptr->getCamera().setToKnownGoodView();
                    viewport_ptr->update();
                }
                });
        }
        });

    // Window Setup
    if (menuBar()) {
        menuBar()->setVisible(false);
    }
    resize(1600, 900);
    setWindowTitle("KR Studio V0.1");
    setWindowIcon(QIcon(":/icons/kRLogoSquare.png"));
    statusBar()->showMessage("Ready.");
}

MainWindow::~MainWindow()
{
}