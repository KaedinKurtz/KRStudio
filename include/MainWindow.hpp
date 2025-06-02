// include/MainWindow.hpp
#pragma once

#include <QMainWindow>
#include <QTimer>

// Add the include for QResizeEvent
#include <QResizeEvent>

// Forward declarations
class QWidget; // Add QWidget forward declaration
class StaticToolbar;
class ViewportWidget;
namespace ads {
    class CDockManager;
    class CDockWidget;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:


private:
    // --- Member variables updated for the new layout ---
    QWidget* m_centralContainer; // The main container is now a member
    StaticToolbar* m_fixedTopToolbar;
    QWidget* m_adsHostWidget;
    ads::CDockManager* m_dockManager;
    ViewportWidget* m_viewport;
    ads::CDockWidget* m_viewportDock;
};