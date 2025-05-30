#pragma once
#include <QMainWindow>

class ViewportWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private:
    ViewportWidget* m_viewport;
};