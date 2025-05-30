#include "MainWindow.hpp"
#include "ViewportWidget.hpp"
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    m_viewport = new ViewportWidget(this);
    setCentralWidget(m_viewport);

    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    QAction* exitAction = new QAction(tr("E&xit"), this);
    connect(exitAction, &QAction::triggered, this, &MainWindow::close);
    fileMenu->addAction(exitAction);

    resize(1280, 720);
    setWindowTitle("Robotics Software (Qt Version)");
    statusBar()->showMessage("Ready.");
}

MainWindow::~MainWindow() {}