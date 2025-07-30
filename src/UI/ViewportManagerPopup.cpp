#include "ViewportManagerPopup.hpp"
#include "MainWindow.hpp" // For ViewportWidget
#include "ViewportWidget.hpp"
#include "Scene.hpp"
#include "components.hpp" // For CameraComponent
#include "DockWidget.h"
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>

ViewportManagerPopup::ViewportManagerPopup(QWidget* parent) : QWidget(parent) {
    // Make this widget a popup that closes when you click away.
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_DeleteOnClose);
    setStyleSheet("background-color: #353b46; border: 1px solid #4a5260;");

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(10, 10, 10, 10);
    m_mainLayout->setSpacing(10);

    // Counter Label (+ ----- [N] ----- -)
    auto* counterLayout = new QHBoxLayout();
    m_addViewportButton = new QPushButton("+");
    m_removeViewportButton = new QPushButton("-");
    m_counterLabel = new QLabel("[0]");
    m_counterLabel->setAlignment(Qt::AlignCenter);

    counterLayout->addWidget(m_addViewportButton);
    counterLayout->addWidget(m_counterLabel, 1); // Stretch the label
    counterLayout->addWidget(m_removeViewportButton);
    m_mainLayout->addLayout(counterLayout);

    // Grid for the "Show" buttons
    m_buttonGrid = new QGridLayout();
    m_buttonGrid->setSpacing(5);
    m_mainLayout->addLayout(m_buttonGrid);

    m_mainLayout->addStretch();

    // Reset Button
    m_resetButton = new QPushButton("Reset Viewports");
    m_mainLayout->addWidget(m_resetButton);

    // Connect internal buttons to the public signals
    connect(m_addViewportButton, &QPushButton::clicked, this, &ViewportManagerPopup::addViewportRequested);
    connect(m_removeViewportButton, &QPushButton::clicked, this, &ViewportManagerPopup::removeViewportRequested);
    connect(m_resetButton, &QPushButton::clicked, this, &ViewportManagerPopup::resetViewportsRequested);
}

void ViewportManagerPopup::updateUi(const QList<ads::CDockWidget*>& viewportDocks, Scene* scene) {
    // Update the counter
    m_counterLabel->setText(QString("[ %1 ]").arg(viewportDocks.size()));

    // Clear the old buttons
    clearLayout(m_buttonGrid);

    // Re-create buttons for the current viewports
    int row = 0, col = 0;
    for (int i = 0; i < viewportDocks.size(); ++i) {
        ads::CDockWidget* dock = viewportDocks.at(i);
        auto* vp = qobject_cast<ViewportWidget*>(dock->widget());
        if (!vp || !scene) continue;

        auto* button = new QPushButton(QString("Show %1").arg(i + 1));

        // Color-code the button using the camera's tint
        auto& registry = scene->getRegistry();
        const auto& cam = registry.get<CameraComponent>(vp->getCameraEntity());
        QColor tintColor;
        tintColor.setRgbF(cam.tint.r, cam.tint.g, cam.tint.b);
        button->setStyleSheet(QString("background-color: %1; color: %2;")
            .arg(tintColor.name())
            .arg(tintColor.lightnessF() > 0.5 ? "black" : "white"));

        // When clicked, emit a signal with the specific dock widget to show
        connect(button, &QPushButton::clicked, this, [this, dock]() {
            emit showViewportRequested(dock);
            close(); // Close the popup after clicking
            });

        m_buttonGrid->addWidget(button, row, col);
        col++;
        if (col >= 3) { // 3 buttons per row
            col = 0;
            row++;
        }
    }
}

// Helper to safely clear all widgets from a layout
void ViewportManagerPopup::clearLayout(QLayout* layout) {
    if (!layout) return;
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}