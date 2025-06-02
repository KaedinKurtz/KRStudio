// include/MainWindow.hpp
#pragma once

#include <QMainWindow>

// Qt Advanced Docking System includes
#include <DockManager.h>
#include <DockWidget.h>

class ViewportWidget;
class StaticToolbar;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private:
    ViewportWidget* m_viewport;             // Instance of your viewport
    ads::CDockManager* m_dockManager;
    StaticToolbar* m_staticToolbar;         // Your static toolbar instance

    // DockWidgets
    ads::CDockWidget* m_toolbarDock;        // Dock widget for the static toolbar
    ads::CDockWidget* m_viewportDock;       // Dock widget for the viewport
};