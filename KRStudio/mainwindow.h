#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

// Forward declarations
namespace Ui { class MainWindow; }
namespace ads { class CDockManager; } // For ADS

// Forward declare your custom panels
class PropertiesPanel;
class ViewportPanel;
class GraphPanel;
class MonitorPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow *ui;
    ads::CDockManager* m_dockManager; // ADS Dock Manager

    // Optional: Keep pointers if you need to access panels later
    // PropertiesPanel* m_propertiesPanel;
    // ViewportPanel* m_viewportPanel;
    // GraphPanel* m_graphPanel;
    // MonitorPanel* m_monitorPanel;
};

#endif // MAINWINDOW_H
