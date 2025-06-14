#pragma once

#include "RobotDescription.hpp"
#include <QWidget>
#include <QPointer>

// Forward declare UI elements
class QLineEdit;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;

class LinkPropertiesWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LinkPropertiesWidget(QWidget* parent = nullptr);

    void setLink(LinkDescription* linkDesc);
    void updateLinkDescription() const; // Renamed to reflect it modifies the stored pointer

signals:
    void propertiesChanged();

private slots:
    void onBrowseMeshClicked();

private:
    void setupUI();

    // A pointer to the currently edited link description.
    // It's not const so we can modify it directly.
    LinkDescription* m_currentLink = nullptr;

    // --- UI Elements ---
    QPointer<QLineEdit> m_nameLineEdit;
    QPointer<QLineEdit> m_meshPathLineEdit;
    QPointer<QPushButton> m_browseMeshButton;
    QPointer<QDoubleSpinBox> m_massSpinBox;
    QPointer<QCheckBox> m_visibleCheckBox;
};
