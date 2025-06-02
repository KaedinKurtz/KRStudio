#include "mainwindow.h"
#include "ui_mainwindow.h" // Your mainwindow.ui

// Include ADS headers
#include "DockManager.h"
#include "DockWidget.h"

// Include your custom panel headers
#include "propertiespanel.h"
#include "viewportpanel.h"   // Create these files like PropertiesPanel
#include "graphpanel.h"      // Create these files like PropertiesPanel
#include "monitorpanel.h"    // Create these files like PropertiesPanel

#include <QLabel> // For placeholder content if needed

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    // 1. Setup the main window frame (toolbars, menubar from mainwindow.ui)
    ui->setupUi(this);

    // --- Initialize the ADS Docking System ---
    m_dockManager = new ads::CDockManager(this);
    setCentralWidget(m_dockManager); // ADS takes over the central area

    // --- Create instances of your custom panels ---
    PropertiesPanel* propertiesPanel = new PropertiesPanel(this);
    ViewportPanel* viewportPanel = new ViewportPanel(this); // This will contain your GL view
    GraphPanel* graphPanel = new GraphPanel(this);
    MonitorPanel* monitorPanel = new MonitorPanel(this);

    // --- Wrap panels in ADS DockWidgets ---
    ads::CDockWidget* propertiesDock = new ads::CDockWidget("Properties");
    propertiesDock->setWidget(propertiesPanel);
    // Optional: Set minimum/maximum sizes for the dock widget itself if needed
    // propertiesDock->setMinimumWidth(200);
    // propertiesDock->setMaximumWidth(300);


    ads::CDockWidget* viewportDock = new ads::CDockWidget("Viewport");
    viewportDock->setWidget(viewportPanel);
    // The viewport should ideally be the one that expands most
    // You can also set features like preventing it from closing
    viewportDock->setFeatures(viewportDock->features() | ads::DockWidgetDeleteOnClose);


    ads::CDockWidget* graphDock = new ads::CDockWidget("Graphs");
    graphDock->setWidget(graphPanel);
    // graphDock->setMinimumHeight(100);
    // graphDock->setMaximumHeight(300);


    ads::CDockWidget* monitorDock = new ads::CDockWidget("Monitor");
    monitorDock->setWidget(monitorPanel);
    // monitorDock->setMinimumWidth(400);
    // monitorDock->setMaximumWidth(600);


    // --- Arrange panels in the initial 5-panel layout ---
    // Your mockup had:
    // - Top: Static QTabWidget in QToolBar (done in mainwindow.ui)
    // - Left: Properties
    // - Center: Viewport
    // - Right: Monitor
    // - Bottom: Graphs (spanning under Properties, Viewport, Monitor)

    // Add the side panels first
    ads::CDockAreaWidget* leftArea = m_dockManager->addDockWidget(ads::LeftDockWidgetArea, propertiesDock);
    ads::CDockAreaWidget* rightArea = m_dockManager->addDockWidget(ads::RightDockWidgetArea, monitorDock);

    // Add the bottom panel. It will span the full width available at the bottom.
    ads::CDockAreaWidget* bottomArea = m_dockManager->addDockWidget(ads::BottomDockWidgetArea, graphDock);

    // Add the viewport to the center. It will fill the remaining space.
    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, viewportDock);


    // Optional: Restore a saved layout if you implement that later
    // m_dockManager->restoreStateFromFile("layout.bin");

    // Optional: Set initial sizes of dock areas (requires more advanced ADS usage)
    // For example, to make the left properties panel take 25% of the width:
    // This is a bit more involved and might require getting the top-level splitter.
    // ads::CDockAreaWidget* centerArea = viewportDock->dockAreaWidget();
    // if (centerArea && centerArea->parentSplitter()) {
    //    QList<int> sizes;
    //    sizes << width() * 0.25 << width() * 0.75; // Example for a two-pane splitter
    //    centerArea->parentSplitter()->setSizes(sizes);
    // }
}

MainWindow::~MainWindow()
{
    // Optional: Save layout on exit
    // if (m_dockManager) {
    //     m_dockManager->saveStateToFile("layout.bin");
    // }
    delete ui;
    // m_dockManager is a child of MainWindow, so it will be deleted automatically.
}
```

This structure gives you:
* A static top toolbar defined in `mainwindow.ui`.
* A dynamic central area managed by ADS.
* Four custom panels (`PropertiesPanel`, `ViewportPanel`, `GraphPanel`, `MonitorPanel`), each with its own designable `.ui` file, arranged by ADS into your desired initial layout.
* The panels will be resizable via the splitters ADS creates and dockable/floatable according to ADS's default behavior (which you can customize further if needed).

Remember to add all the new `.h` and `.cpp` files for your custom panels to your `CMakeLists.txt` file so they get compil
