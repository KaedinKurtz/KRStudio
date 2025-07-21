#include "JointPropertiesWidget.hpp"
#include <QVBoxLayout>
#include <QLabel>

// This is the correct, minimal implementation for the placeholder widget.
// It defines the constructor and setData function that the rest of the code expects.

JointPropertiesWidget::JointPropertiesWidget(QWidget* parent)
    : QWidget(parent)
{
    // It should have its own layout...
    auto* layout = new QVBoxLayout(this);

    // ...and its own placeholder label.
    auto* placeholderLabel = new QLabel("Joint Properties Editor (Work In Progress)", this);
    placeholderLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(placeholderLabel);

    setLayout(layout);
}

// The setData function is defined but currently empty. The linker just needs to know it exists.
void JointPropertiesWidget::setData(const JointDescription& jointDesc)
{
    // TODO: In the future, use the data from jointDesc to populate the real editor fields.
    // This function intentionally left blank for now to satisfy the linker.
}
