#include "NodeDelegate.hpp"
#include "Node.hpp"
#include "NodeFactory.hpp"
#include "ExecutionControlWidget.hpp"

#include <QtNodes/NodeData>
#include <any>
#include <QVariant>
#include <QtNodes/NodeStyle>
#include <QDebug>

// AnyNodeData class remains the same...

class AnyNodeData : public QtNodes::NodeData
{
public:
    explicit AnyNodeData(const PortDataPacket& packet) : m_packet(packet) {}

    QtNodes::NodeDataType type() const override
    {
        QString t = QString::fromStdString(m_packet.type.name);
        return { t, t };
    }

    const PortDataPacket& packet() const { return m_packet; }

private:
    PortDataPacket m_packet;
};

NodeDelegate::NodeDelegate(std::string typeId)
    : m_backendNode(nullptr)
    , m_typeId(std::move(typeId))
{
}
NodeDelegate::~NodeDelegate() = default;

// ... name(), caption(), portCaptionVisible(), portCaption() remain the same ...
QString NodeDelegate::name() const
{
    if (m_backendNode) {
        return QString::fromStdString(m_backendNode->getId());
    }
    return QString::fromStdString(m_typeId);
}

bool NodeDelegate::portCaptionVisible(QtNodes::PortType, QtNodes::PortIndex) const
{
    return true;
}

QString NodeDelegate::portCaption(QtNodes::PortType portType, QtNodes::PortIndex portIndex) const
{
    const Port* p = getBackendPort(portType, portIndex);
    if (!p) return QStringLiteral("Invalid Port");

    if (p->name == "Trigger") {
        if (m_backendNode) {
            auto policy = m_backendNode->getUpdatePolicy();
            if (policy == Node::UpdatePolicy::Asynchronous || policy == Node::UpdatePolicy::Synchronous) {
                return QString("Hold");
            }
        }
        return QString("Trigger");
    }

    return QString::fromStdString(p->name);
}
QString NodeDelegate::caption() const
{
    std::string currentTypeId = m_backendNode ? m_backendNode->getId() : m_typeId;
    if (currentTypeId.empty()) {
        qWarning() << "Caption requested for delegate with no Type ID set.";
        return QStringLiteral("Error (No ID)");
    }
    const auto& descMap = NodeFactory::instance().getRegisteredNodeTypes();
    auto it = descMap.find(currentTypeId);
    if (it != descMap.end()) {
        return QString::fromStdString(it->second.aui_name);
    }
    qWarning() << "Caption lookup failed for Type ID:" << QString::fromStdString(currentTypeId);
    return QStringLiteral("Error (Not Found)");
}


unsigned int NodeDelegate::nPorts(QtNodes::PortType portType) const
{
    if (!m_backendNode) return 0;
    auto desiredDirection = (portType == QtNodes::PortType::In)
        ? Port::Direction::Input
        : Port::Direction::Output;
    unsigned int count = 0;
    for (const auto& port : m_backendNode->getPorts()) {
        if (port.direction == desiredDirection) {
            ++count;
        }
    }
    return count;
}

QtNodes::NodeDataType NodeDelegate::dataType(QtNodes::PortType portType, QtNodes::PortIndex portIndex) const
{
    const Port* p = getBackendPort(portType, portIndex);
    if (!p) return { "", "" };

    QString typeName = QString::fromStdString(p->type.name);
    return { typeName, typeName };
}

std::shared_ptr<QtNodes::NodeData> NodeDelegate::outData(QtNodes::PortIndex portIndex)
{
    if (!m_backendNode) return nullptr;
    const Port* p = getBackendPort(QtNodes::PortType::Out, portIndex);
    if (!p || !p->packet.has_value()) return nullptr;
    return std::make_shared<AnyNodeData>(p->packet.value());
}

void NodeDelegate::setInData(std::shared_ptr<QtNodes::NodeData> data, QtNodes::PortIndex portIndex)
{
    if (!m_backendNode || !data) {
        return;
    }
    const Port* inPort = getBackendPort(QtNodes::PortType::In, portIndex);
    if (!inPort) return;
    auto anyData = std::dynamic_pointer_cast<AnyNodeData>(data);
    if (anyData)
    {
        m_backendNode->setInput(inPort->name, anyData->packet());
        m_backendNode->process();

        for (unsigned int i = 0; i < nPorts(QtNodes::PortType::Out); ++i) {
            Q_EMIT dataUpdated(i);
        }
    }
}

void NodeDelegate::setBackendNode(std::unique_ptr<Node> backendNode)
{
    m_backendNode = std::move(backendNode);
    m_backendNode->process();
}

QWidget* NodeDelegate::embeddedWidget()
{
    // If the widget has not been created yet AND the backend is ready...
    if (!m_embeddedWidget && m_backendNode && m_backendNode->needsExecutionControls())
    {
        // ...create the widget now.
        auto* controlWidget = new ExecutionControlWidget();
        m_embeddedWidget = controlWidget; // Store the pointer immediately.

        // Set its initial state from the backend node.
        controlWidget->setInitialState(m_backendNode->getUpdatePolicy(), m_backendNode->getTriggerEdge());

        // Connect the widget's signals to update the backend and the UI.
        connect(controlWidget, &ExecutionControlWidget::updatePolicyChanged,
            this, [this](Node::UpdatePolicy policy) {
                if (m_backendNode) {
                    m_backendNode->setUpdatePolicy(policy);
                    // We must emit a signal that forces a full node repaint to update the port caption.
                    // We ask the MainWindow to do this.
                    Q_EMIT embeddedWidgetSizeUpdated(); // This is a general "something changed" signal
                }
            });

        connect(controlWidget, &ExecutionControlWidget::triggerEdgeChanged,
            this, [this](Node::TriggerEdge edge) {
                if (m_backendNode) {
                    m_backendNode->setTriggerEdge(edge);
                }
            });
    }

    // Return the widget.
    return m_embeddedWidget;
}


const Port* NodeDelegate::getBackendPort(QtNodes::PortType portType, QtNodes::PortIndex portIndex) const
{
    if (!m_backendNode) return nullptr;
    auto desiredDirection = (portType == QtNodes::PortType::In)
        ? Port::Direction::Input
        : Port::Direction::Output;
    unsigned int currentIndex = 0;
    for (const auto& port : m_backendNode->getPorts()) {
        if (port.direction == desiredDirection) {
            if (currentIndex == static_cast<unsigned int>(portIndex)) {
                return &port;
            }
            ++currentIndex;
        }
    }
    return nullptr;
}