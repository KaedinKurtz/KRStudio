#pragma once

#include "RobotDescription.hpp"
#include <QWidget>

// A widget for displaying and editing the properties of a single JointDescription.
// For now, it's just a placeholder.
class JointPropertiesWidget : public QWidget
{
    Q_OBJECT

public:
    explicit JointPropertiesWidget(QWidget* parent = nullptr);

    // A public method to populate the editor with data from a specific joint.
    void setData(const JointDescription& jointDesc);

signals:
    // A signal to notify the parent dialog that data has changed.
    void propertiesChanged();

private:
    // In the future, you will add UI elements for all the rich joint data.
};
