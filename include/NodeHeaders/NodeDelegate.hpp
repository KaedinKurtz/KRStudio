#pragma once

#include <QWidget>

#include <QtNodes/NodeDelegateModel>
#include "Node.hpp"
#include <memory>

class NodeDelegate : public QtNodes::NodeDelegateModel
{
    Q_OBJECT

public:
    explicit NodeDelegate(std::string typeId);
    ~NodeDelegate() override;

    QString name() const override;
    QString caption() const override;
    QString portCaption(QtNodes::PortType portType, QtNodes::PortIndex portIndex) const override;
    // This was the missing declaration
    bool portCaptionVisible(QtNodes::PortType portType, QtNodes::PortIndex portIndex) const override;

    unsigned int nPorts(QtNodes::PortType portType) const override;
    QtNodes::NodeDataType dataType(QtNodes::PortType portType, QtNodes::PortIndex portIndex) const override;
    std::shared_ptr<QtNodes::NodeData> outData(QtNodes::PortIndex portIndex) override;
    void setInData(std::shared_ptr<QtNodes::NodeData> data, QtNodes::PortIndex portIndex) override;
    QWidget* embeddedWidget() override;

    void setBackendNode(std::unique_ptr<Node> backendNode);
    void populateEmbeddedWidget() const;

    // Create the backend node + its embedded widget on first access (the MOUNT FIX): QtNodes calls
    // nPorts()/dataType()/embeddedWidget() while constructing the NodeGraphicsObject, BEFORE MainWindow's
    // nodeCreated handler runs -- so the widget must already exist then, or it is never embedded.
    void ensureBackend() const;
    Node* backendNode() const { ensureBackend(); return m_backendNode.get(); }
    // re-run the backend node + notify QtNodes downstream (called when an in-node input widget is edited).
    void recomputeAndPropagate();


Q_SIGNALS:
    void portCaptionChanged();

private:
    const Port* getBackendPort(QtNodes::PortType portType, QtNodes::PortIndex portIndex) const;

    mutable std::unique_ptr<Node> m_backendNode;   // mutable: lazily created from m_typeId on first access
    std::string m_typeId;
    mutable QWidget* m_embeddedWidget = nullptr;
};