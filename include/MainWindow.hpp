#pragma once

#include <QMainWindow>

// Qt Advanced Docking System includes
// Adjust paths if your installation structure is different (e.g., "ads/DockManager.h")
#include <DockManager.h>
#include <DockWidget.h>

class ViewportWidget;
class StaticToolbar; // Forward declare our custom toolbar widget

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private:
    ViewportWidget* m_viewport;
    ads::CDockManager* m_dockManager;
    StaticToolbar* m_staticToolbar; // Instance of your toolbar UI widget
    // ads::CDockWidget* m_toolbarDock; // The ADS wrapper for the toolbar
                                     // Not strictly needed as a member if only added once
};