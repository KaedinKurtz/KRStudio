#include "JointPropertiesWidget.hpp"
#include <QVBoxLayout>
#include <QLabel>

// This is the correct, minimal implementation for the placeholder widget.

JointPropertiesWidget::JointPropertiesWidget(QWidget* parent)
    : QWidget(parent)
{
    // It should have its own layout and its own placeholder label.
    auto* layout = new QVBoxLayout(this);

    auto* placeholderLabel = new QLabel("Joint Properties Editor (WIP)", this);
    placeholderLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(placeholderLabel);

    setLayout(layout);
}

// The setData function is defined but currently empty, waiting for you to implement it.
void JointPropertiesWidget::setData(const JointDescription& jointDesc)
{
    // TODO: Use the data from jointDesc to populate the real editor fields.
    // This function intentionally left blank for now.
}
