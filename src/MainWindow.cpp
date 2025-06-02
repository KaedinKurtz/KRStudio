// src/MainWindow.cpp
#include "MainWindow.hpp"
#include "ViewportWidget.hpp"
#include "StaticToolbar.hpp" // Your custom toolbar header

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // 1. Initialize the Qt Advanced Docking System Manager
    // This should be one of the first things for ADS to take over the window.
    m_dockManager = new ads::CDockManager(this);

    // 2. Setup the Static Toolbar
    m_staticToolbar = new StaticToolbar(this);
    m_toolbarDock = new ads::CDockWidget("Main Toolbar"); // Assign to member
    m_toolbarDock->setWidget(m_staticToolbar);

    // --- Configure the static toolbar dock widget ---
    m_toolbarDock->setFeature(ads::CDockWidget::DockWidgetMovable, false);
    m_toolbarDock->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    m_toolbarDock->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    // else {
    m_toolbarDock->setFeature(ads::CDockWidget::NoTab, true); // Fallback if titleBar() hiding isn't perfect
    // }
    m_dockManager->addDockWidget(ads::TopDockWidgetArea, m_toolbarDock);

    // 3. Setup the ViewportWidget as a dockable widget
    m_viewport = new ViewportWidget(this);
    m_viewportDock = new ads::CDockWidget("3D Viewport");
    m_viewportDock->setWidget(m_viewport);

    // --- Configure the viewport dock widget features ---
    m_viewportDock->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    m_viewportDock->setFeature(ads::CDockWidget::DockWidgetMovable, true);
    m_viewportDock->setFeature(ads::CDockWidget::DockWidgetFloatable, true);

    connect(m_viewportDock, &ads::CDockWidget::topLevelChanged, this, [this](bool isFloating) {
        Q_UNUSED(isFloating);
        if (m_viewport) {
            QTimer::singleShot(0, m_viewport, [viewport_ptr = m_viewport]() {
                if (viewport_ptr) {
                    int w = viewport_ptr->width();
                    int h = viewport_ptr->height();
                    float aspectRatio = static_cast<float>(w) / std::max(1, h);
                    qDebug() << "Timer fired: Attempting to set known good view for viewport:" << viewport_ptr
                        << "with current size:" << w << "x" << h << "Aspect:" << aspectRatio;

                    viewport_ptr->getCamera().setToKnownGoodView(aspectRatio);
                    viewport_ptr->update();
                }
                });
        }
        });

    m_dockManager->addDockWidget(ads::BottomDockWidgetArea, m_viewportDock);

    // --- Main Menu Bar (hide it as before) ---
    if (menuBar())
    {
        menuBar()->setVisible(false);
    }
    // Or, if you want some basic menus for ADS (like listing dock widgets):
    // QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    // m_dockManager->addDockWidgetActionsToMenu(viewMenu); // Example, API may vary

    // --- Window Setup ---
    resize(1600, 900);
    setWindowTitle("Robotics Software - Dockable Viewport");
    statusBar()->showMessage("Ready.");
}

MainWindow::~MainWindow()
{
    // Qt's parent-child mechanism handles m_dockManager.
    // m_viewport and m_staticToolbar are widgets set into CDockWidgets,
    // which are managed by the CDockManager.
}