#pragma once

#include <QDockWidget>
#include <map>
#include <string>

// Forward declarations
class QLabel;
class QVBoxLayout;
class Robot;

class DiagnosticsPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit DiagnosticsPanel(QWidget* parent = nullptr);
    void updateData(const Robot& robot);

private:
    QWidget* m_mainWidget;
    QVBoxLayout* m_layout;
    std::map<std::string, QLabel*> m_jointLabels;
};