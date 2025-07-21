#pragma once

#include <QWidget>
#include "Node.hpp" // Include for the enums
#include <QLabel>

class QComboBox;

class ExecutionControlWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ExecutionControlWidget(QWidget* parent = nullptr);

    // Sets the initial state of the controls without emitting signals
    void setInitialState(Node::UpdatePolicy policy, Node::TriggerEdge edge);

Q_SIGNALS:
    // These signals will notify the backend node of changes
    void updatePolicyChanged(Node::UpdatePolicy newPolicy);
    void triggerEdgeChanged(Node::TriggerEdge newEdge);

private Q_SLOTS:
    void onPolicyComboBoxChanged(int index);
    void onEdgeComboBoxChanged(int index);

private:
    QComboBox* m_policyComboBox;
    QLabel* m_edgeLabel;
    QComboBox* m_edgeComboBox;
};