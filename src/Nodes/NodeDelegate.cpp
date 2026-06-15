#include "NodeDelegate.hpp"
#include "Node.hpp"
#include "NodeFactory.hpp"
#include "ExecutionControlWidget.hpp"

#include <QtNodes/NodeData>
#include <any>
#include <QVariant>
#include <QtNodes/NodeStyle>
#include <QDebug>
#include <QVBoxLayout>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <glm/glm.hpp>
#include <memory>

// AnyNodeData class remains the same...

void addCommonNodeControls(QVBoxLayout* layout, Node* backendNode) {
    if (!backendNode || !backendNode->needsExecutionControls()) {
        return;
    }

    auto* controlWidget = new ExecutionControlWidget();
    controlWidget->setInitialState(backendNode->getUpdatePolicy(), backendNode->getTriggerEdge());

    // Connect signals from the widget to the backend node
    QObject::connect(controlWidget, &ExecutionControlWidget::updatePolicyChanged,
        [backendNode](Node::UpdatePolicy policy) {
            backendNode->setUpdatePolicy(policy);
        });

    QObject::connect(controlWidget, &ExecutionControlWidget::triggerEdgeChanged,
        [backendNode](Node::TriggerEdge edge) {
            backendNode->setTriggerEdge(edge);
        });

    layout->addWidget(controlWidget);
}

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

// MOUNT FIX: lazily create the backend node + its widget the first time QtNodes touches this delegate
// (nPorts/dataType/embeddedWidget during NodeGraphicsObject construction), so embeddedWidget() is non-null
// when embedQWidget() mounts it. Idempotent.
void NodeDelegate::ensureBackend() const
{
    if (m_backendNode || m_typeId.empty()) return;
    m_backendNode = NodeFactory::instance().createNode(m_typeId);
    if (m_backendNode) {
        populateEmbeddedWidget();
        m_backendNode->process();
    }
}

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
    ensureBackend();
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

    // QtNodes connects an out->in pair only when their NodeDataType.id MATCHES. Map the library's numeric
    // types to ONE id "number" (double/float/int/bool all carry numbers) so signal/math/logic nodes
    // interconnect; vectors -> "vec3"; everything else keeps its own id. (.name is the display label.)
    const std::string& tn = p->type.name;
    QString id = QString::fromStdString(tn);
    if (tn == "double" || tn == "float" || tn == "int" || tn == "bool") id = "number";
    else if (tn == "glm::vec3") id = "vec3";
    return { id, QString::fromStdString(tn) };
}

void NodeDelegate::recomputeAndPropagate()
{
    if (!m_backendNode) return;
    m_backendNode->process();
    for (unsigned int i = 0; i < nPorts(QtNodes::PortType::Out); ++i) Q_EMIT dataUpdated(i);
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
    if (!m_backendNode) return;

    // The backend is now attached. It's safe to create the widget.
    populateEmbeddedWidget();

    // Initial processing to populate output ports.
    m_backendNode->process();
}

QWidget* NodeDelegate::embeddedWidget()
{
    ensureBackend();                 // MOUNT FIX: build the widget now if QtNodes asks before the handler ran
    return m_embeddedWidget;
}


const Port* NodeDelegate::getBackendPort(QtNodes::PortType portType, QtNodes::PortIndex portIndex) const
{
    ensureBackend();
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

void NodeDelegate::populateEmbeddedWidget() const
{
    if (!m_backendNode || m_embeddedWidget) {
        // Don't do anything if there's no backend or the widget already exists.
        return;
    }

    // 1. Create a container widget. This will be the main embedded widget.
    auto* container = new QWidget();
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2); // Keep it tight

    // 2. Call the "global" function to add common controls.
    addCommonNodeControls(layout, m_backendNode.get());

    // 3. Call a "node-specific" function on the backend to get custom controls.
    if (QWidget* customWidget = m_backendNode->createCustomWidget()) {
        layout->addWidget(customWidget);
    }

    // 4. PER-INPUT-PORT WIDGETS (the root fix): for every input port the user can set, mount a typed,
    //    labelled control bound to the port's LITERAL value -> editing it sets the input the node reads
    //    when that port is unconnected (a connection still overrides). Each control is tagged
    //    "krs_input_port" so the binding can be verified through the node body.
    NodeDelegate* self = const_cast<NodeDelegate*>(this);
    Node* node = m_backendNode.get();
    for (const auto& port : node->getPorts()) {
        if (port.direction != Port::Direction::Input || port.name == "Trigger") continue;
        const std::string tn = port.type.name;
        const std::string portName = port.name;
        const QString tag = QString::fromStdString(portName);
        auto* row = new QWidget();
        auto* rl = new QHBoxLayout(row); rl->setContentsMargins(0, 0, 0, 0); rl->setSpacing(4);
        auto* lab = new QLabel(QString::fromStdString(portName)); lab->setMinimumWidth(40); rl->addWidget(lab);

        if (tn == "double" || tn == "float") {
            auto* sb = new QDoubleSpinBox(); sb->setRange(-1.0e6, 1.0e6); sb->setDecimals(4); sb->setSingleStep(0.1);
            sb->setProperty("krs_input_port", tag);
            QObject::connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                [self, portName](double v) { if (self->m_backendNode) self->m_backendNode->setPortLiteral<float>(portName, float(v)); self->recomputeAndPropagate(); });
            rl->addWidget(sb);
        } else if (tn == "int") {
            auto* sb = new QSpinBox(); sb->setRange(-1000000, 1000000);
            sb->setProperty("krs_input_port", tag);
            QObject::connect(sb, QOverload<int>::of(&QSpinBox::valueChanged),
                [self, portName](int v) { if (self->m_backendNode) self->m_backendNode->setPortLiteral<int>(portName, v); self->recomputeAndPropagate(); });
            rl->addWidget(sb);
        } else if (tn == "bool") {
            auto* cb = new QCheckBox();
            cb->setProperty("krs_input_port", tag);
            QObject::connect(cb, &QCheckBox::toggled,
                [self, portName](bool v) { if (self->m_backendNode) self->m_backendNode->setPortLiteral<bool>(portName, v); self->recomputeAndPropagate(); });
            rl->addWidget(cb);
        } else if (tn == "glm::vec3") {
            auto vec = std::make_shared<glm::vec3>(0.0f);
            for (int k = 0; k < 3; ++k) {
                auto* sb = new QDoubleSpinBox(); sb->setRange(-1.0e6, 1.0e6); sb->setDecimals(3);
                if (k == 0) sb->setProperty("krs_input_port", tag);
                QObject::connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    [self, portName, vec, k](double v) { (*vec)[k] = float(v);
                        if (self->m_backendNode) self->m_backendNode->setPortLiteral<glm::vec3>(portName, *vec); self->recomputeAndPropagate(); });
                rl->addWidget(sb);
            }
        } else {
            delete row; continue;   // handle/data ports (registry*, ProfiledData, ...) -> set via connection only
        }
        layout->addWidget(row);
    }

    container->setLayout(layout);
    m_embeddedWidget = container;
}