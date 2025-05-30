#pragma once
#include <QMainWindow>

class ViewportWidget;
class DiagnosticsPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots: // <-- Add a private slots section
    void updateUI();

private:
    ViewportWidget* m_viewport;
    DiagnosticsPanel* m_diagnosticsPanel;
};