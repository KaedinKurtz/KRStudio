// In a new file, e.g., include/ViewportManagerPopup.hpp
#pragma once

#include <QWidget>
#include <QList>

// Forward declarations
class QVBoxLayout;
class QGridLayout;
class QLabel;
class QPushButton;
class Scene;
namespace ads {
    class CDockWidget;
}

class ViewportManagerPopup : public QWidget {
    Q_OBJECT

public:
    explicit ViewportManagerPopup(QWidget* parent = nullptr);

    // This is the main function to update the popup's content.
    void updateUi(const QList<ads::CDockWidget*>& viewportDocks, Scene* scene);

signals:
    // Signal to tell MainWindow to bring a specific viewport to the front.
    void showViewportRequested(ads::CDockWidget* dock);
    // Signal to tell MainWindow to reset the layout to a single viewport.
    void resetViewportsRequested();
    // Signals for adding/removing viewports
    void addViewportRequested();
    void removeViewportRequested();

private:
    void clearLayout(QLayout* layout);

    // UI elements
    QVBoxLayout* m_mainLayout;
    QLabel* m_counterLabel;
    QGridLayout* m_buttonGrid;
    QPushButton* m_addViewportButton;
    QPushButton* m_removeViewportButton;
    QPushButton* m_resetButton;
};