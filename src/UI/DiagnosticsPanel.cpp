#include "DiagnosticsPanel.hpp"
#include "Robot.hpp" // Include Robot to use its data
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

void DiagnosticsPanel::updateData(const Robot& robot)
{
    auto jointStates = robot.getJointStates();

    // On first run, create labels for each joint
    if (m_jointLabels.empty() && !jointStates.empty()) {
        // Clear the placeholder label
        delete m_layout->takeAt(0)->widget();
        for (const auto& [name, angle] : jointStates) {
            m_jointLabels[name] = new QLabel(m_mainWidget);
            m_layout->addWidget(m_jointLabels[name]);
        }
    }

    // Update the text of each label every frame
    for (const auto& [name, angle] : jointStates) {
        if (m_jointLabels.count(name)) {
            std::stringstream stream;
            stream << std::fixed << std::setprecision(2) << glm::degrees(angle);
            std::string label_text = name + ": " + stream.str() + " deg";
            m_jointLabels[name]->setText(QString::fromStdString(label_text));
        }
    }
}