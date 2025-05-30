#include "MainWindow.hpp"
#include "ViewportWidget.hpp"
#include "DiagnosticsPanel.hpp" // <-- Include the new panel
#include "Robot.hpp"            // <-- Include Robot for type safety

#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // Viewport setup (unchanged)
    m_viewport = new ViewportWidget(this);
    setCentralWidget(m_viewport);

    // --- NEW: Create and add the diagnostics panel ---
    m_diagnosticsPanel = new DiagnosticsPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_diagnosticsPanel);
    // ------------------------------------------------

    // Menu setup (unchanged)
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    QAction* exitAction = new QAction(tr("E&xit"), this);
    connect(exitAction, &QAction::triggered, this, &MainWindow::close);
    fileMenu->addAction(exitAction);

    // Window setup (unchanged)
    resize(1280, 720);
    setWindowTitle("Robotics Software (Qt Version)");
    statusBar()->showMessage("Ready.");

    // --- NEW: Connect the viewport's timer to our UI update slot ---
    connect(&m_viewport->getTimer(), &QTimer::timeout, this, &MainWindow::updateUI);
}

MainWindow::~MainWindow() {}

// --- NEW: Implement the UI update slot ---
void MainWindow::updateUI()
{
    const Robot* robot = m_viewport->getRobot();
    if (robot) {
        m_diagnosticsPanel->updateData(*robot);
    }
}