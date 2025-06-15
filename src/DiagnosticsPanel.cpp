#include "DiagnosticsPanel.hpp"
#include <QVBoxLayout>
#include <QLabel>
#include <iomanip>
#include <sstream>

DiagnosticsPanel::DiagnosticsPanel(QWidget* parent)
    : QDockWidget("Diagnostics", parent)
{
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_mainWidget = new QWidget();
    m_layout = new QVBoxLayout(m_mainWidget);
    m_layout->setSpacing(5);
    m_layout->setAlignment(Qt::AlignTop);

    // Add a placeholder label. It will be updated once the robot is loaded.
    m_layout->addWidget(new QLabel("Waiting for robot data..."));

    setWidget(m_mainWidget);
}
