#include "MainWindow.hpp"
#include "ViewportWidget.hpp"
#include "StaticToolbar.hpp" // Include your custom toolbar header

#include <QMenuBar>
#include <QMenu>
#include <QAction> // Required for QAction
#include <QStatusBar>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    if (menuBar()) // Check if a menu bar exists (QMainWindow creates one by default)
    {
        menuBar()->setVisible(false);
    }

    // 1. Setup the Viewport as the central widget (ADS will dock around it)
    m_viewport = new ViewportWidget(this);
    setCentralWidget(m_viewport);

    // 2. Initialize the Qt Advanced Docking System Manager
    m_dockManager = new ads::CDockManager(this);

    // 3. Create an instance of your StaticToolbar
    m_staticToolbar = new StaticToolbar(this); // Parent can be this or nullptr

    // 4. Create a CDockWidget to hold your toolbar
    ads::CDockWidget* toolbarDockWidget = new ads::CDockWidget("Main Toolbar");
    toolbarDockWidget->setWidget(m_staticToolbar);

    // Optional: Customize dock widget features (closable, floatable, movable)
    // By default, they are usually all enabled.
    toolbarDockWidget->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    toolbarDockWidget->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    toolbarDockWidget->setFeature(ads::CDockWidget::DockWidgetMovable, false);
    
    // Set an icon for the dock widget tab (optional)
    // toolbarDockWidget->setIcon(QIcon(":/icons/your_toolbar_icon.png"));

    // 5. Add the dock widget to the dock manager
    // You can choose TopDockWidgetArea, LeftDockWidgetArea, RightDockWidgetArea, BottomDockWidgetArea
    m_dockManager->addDockWidget(ads::TopDockWidgetArea, toolbarDockWidget);

    // --- Standard Menu and Status Bar Setup ---
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    QAction* exitAction = new QAction(tr("E&xit"), this);
    connect(exitAction, &QAction::triggered, this, &MainWindow::close);
    fileMenu->addAction(exitAction);

    // Add other menus (View menu for managing dock widgets is often useful with ADS)
    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    // Add actions to show/hide dock widgets (ADS provides ways to do this)
    // For example, ADS manager might have a method to create a menu for all dock widgets
    // or you can add actions manually: viewMenu->addAction(toolbarDockWidget->toggleViewAction());


    // --- Window Setup ---
    resize(1600, 900); // Increased size to better accommodate toolbar
    setWindowTitle("Robotics Software (Qt ADS Integration)");
    statusBar()->showMessage("Ready.");
}

MainWindow::~MainWindow()
{
    // Qt's parent-child ownership should handle deletion of m_dockManager,
    // which in turn owns the dock widgets and their contents.
    // If m_staticToolbar was not given a parent or given to the dock widget,
    // and m_toolbarDock was a member, ensure proper cleanup if necessary.
    // However, setWidget() for CDockWidget usually takes ownership.
}