#include "ExecutionControlWidget.hpp"
#include <QComboBox>
#include <QVBoxLayout>
#include <QLabel>

ExecutionControlWidget::ExecutionControlWidget(QWidget* parent)
    : QWidget(parent)
{
    // A vertical layout is perfect for stacking future controls.
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    layout->setSpacing(5);

    // **FIX FOR SIZING**: Set a specific, fixed size for the widget.
    // This provides a stable size hint and prevents the node from expanding.
    // The node's geometry will be calculated based on this size.
    setFixedSize(180, 120);

    m_policyComboBox = new QComboBox(this);
    m_policyComboBox->addItem("Continuous", QVariant::fromValue(Node::UpdatePolicy::Asynchronous));
    m_policyComboBox->addItem("Synchronous", QVariant::fromValue(Node::UpdatePolicy::Synchronous));
    m_policyComboBox->addItem("Triggered", QVariant::fromValue(Node::UpdatePolicy::Triggered));

    m_edgeLabel = new QLabel("Trigger On:", this);

    m_edgeComboBox = new QComboBox(this);
    m_edgeComboBox->addItem("Rising Edge", QVariant::fromValue(Node::TriggerEdge::Rising));
    m_edgeComboBox->addItem("Falling Edge", QVariant::fromValue(Node::TriggerEdge::Falling));
    m_edgeComboBox->addItem("Both Edges", QVariant::fromValue(Node::TriggerEdge::Both));

    layout->addWidget(new QLabel("Execution Mode:", this));
    layout->addWidget(m_policyComboBox);
    layout->addWidget(m_edgeLabel);
    layout->addWidget(m_edgeComboBox);

    // Pushes all current and future widgets to the top of the layout.
    layout->addStretch();

    connect(m_policyComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &ExecutionControlWidget::onPolicyComboBoxChanged);
    connect(m_edgeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &ExecutionControlWidget::onEdgeComboBoxChanged);

    // Set initial visibility
    onPolicyComboBoxChanged(m_policyComboBox->currentIndex());
}

void ExecutionControlWidget::setInitialState(Node::UpdatePolicy policy, Node::TriggerEdge edge)
{
    // Block signals to prevent them from firing during this setup
    m_policyComboBox->blockSignals(true);
    m_edgeComboBox->blockSignals(true);

    m_policyComboBox->setCurrentIndex(static_cast<int>(policy));
    m_edgeComboBox->setCurrentIndex(static_cast<int>(edge));

    // Manually call this once to set the initial visibility correctly
    onPolicyComboBoxChanged(static_cast<int>(policy));

    // Unblock signals
    m_policyComboBox->blockSignals(false);
    m_edgeComboBox->blockSignals(false);
}

void ExecutionControlWidget::onPolicyComboBoxChanged(int index)
{
    Node::UpdatePolicy newPolicy = m_policyComboBox->itemData(index).value<Node::UpdatePolicy>();
    bool isTriggered = (newPolicy == Node::UpdatePolicy::Triggered);

    // Use the member variable to reliably toggle visibility.
    m_edgeLabel->setVisible(isTriggered);
    m_edgeComboBox->setVisible(isTriggered);

    Q_EMIT updatePolicyChanged(newPolicy);
}

void ExecutionControlWidget::onEdgeComboBoxChanged(int index)
{
    Node::TriggerEdge newEdge = m_edgeComboBox->itemData(index).value<Node::TriggerEdge>();
    Q_EMIT triggerEdgeChanged(newEdge);
}