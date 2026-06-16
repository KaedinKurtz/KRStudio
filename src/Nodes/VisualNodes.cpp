#include "VisualNodes.hpp"
#include <iostream>
#include <algorithm> // Required for std::clamp
#include <memory>    // Required for std::make_unique
#include <cmath>
#include <QLCDNumber>
#include <QPainter>
#include <QString>

namespace NodeLibrary {

namespace {
constexpr double kGaugePi = 3.14159265358979323846;

// A display-only arc gauge: paints a background arc, a coloured fill to the normalized position, and a
// needle. NO Q_OBJECT (no signals/slots) so it needs no moc. The current value + normalized position are
// stored as dynamic properties (krs_gauge_value / krs_gauge_norm) so a headless gate can read what the
// gauge is showing without depending on this file-local type.
class GaugeWidget : public QWidget {
public:
    GaugeWidget() {
        setMinimumSize(130, 96);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setProperty("krs_gauge_norm", 0.0);
        setProperty("krs_gauge_value", 0.0);
    }
    void setReading(double norm, double value) {
        m_norm = std::clamp(norm, 0.0, 1.0); m_value = value;
        setProperty("krs_gauge_norm", m_norm);
        setProperty("krs_gauge_value", m_value);
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(10, 10, -10, -10);
        const double startDeg = 210.0, spanDeg = -240.0;  // open-bottom sweep, clockwise
        p.setPen(QPen(QColor(70, 70, 82), 8)); p.drawArc(r, int(startDeg * 16), int(spanDeg * 16));
        p.setPen(QPen(QColor(80, 200, 120), 8)); p.drawArc(r, int(startDeg * 16), int(spanDeg * m_norm * 16));
        const double ang = (startDeg + spanDeg * m_norm) * kGaugePi / 180.0;
        const QPointF c = r.center();
        const double rad = std::min(r.width(), r.height()) / 2.0 - 8.0;
        p.setPen(QPen(QColor(232, 92, 92), 3));
        p.drawLine(c, c + QPointF(std::cos(ang) * rad, -std::sin(ang) * rad));
    }
private:
    double m_norm = 0.0, m_value = 0.0;
};
} // namespace

    // --- Visualization Node Implementations ---

    // ConditionalLightNode
    ConditionalLightNode::ConditionalLightNode() {
	m_id = "viz_conditional_light";
        // FIX: Use nested initializer for DataType
        m_ports.push_back({ "Input", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Color", {"glm::vec4", "rgba"}, Port::Direction::Output, this });
    }
    void ConditionalLightNode::compute() {
        auto input = getInput<float>("Input");
        if (input) {
            for (const auto& rule : conditions) {
                if (rule.condition && rule.condition(*input)) {
                    setOutput("Color", rule.color);
                    return; // First matching rule wins
                }
            }
        }
        setOutput("Color", defaultColor); // No rule matched
    }
    namespace {
        // FIX: Added semicolon after struct definition
        struct ConditionalLightRegistrar {
            ConditionalLightRegistrar() {
                NodeDescriptor desc = { "Conditional Light", "Visualization", "Displays a color based on input conditions." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("viz_conditional_light", desc, []() { return std::make_unique<ConditionalLightNode>(); });
            }
        };
    } // namespace
    // FIX: Declared static instance on its own line
    static ConditionalLightRegistrar g_conditionalLightRegistrar;


    // DialGaugeNode
    DialGaugeNode::DialGaugeNode() {
	m_id = "viz_dial_gauge";
        // FIX: Use nested initializer for DataType
        m_ports.push_back({ "Value", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Min", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Max", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Normalized", {"float", "normalized"}, Port::Direction::Output, this });
    }
    void DialGaugeNode::compute() {
        auto val = getInput<float>("Value");
        auto min = getInput<float>("Min");
        auto max = getInput<float>("Max");
        if (val && min && max && (*max > *min)) {                 // disconnected/invalid range -> NO update
            float normalized = std::clamp((*val - *min) / (*max - *min), 0.0f, 1.0f);
            setOutput("Normalized", normalized);
            m_norm = normalized; m_value = *val; m_have = true;   // value only -- the WIDGET is pushed in refreshUi()
        }
    }
    bool DialGaugeNode::refreshUi() {
        if (!m_have || !m_gauge) return false;
        if (m_pushed && m_norm == m_lastNorm && m_value == m_lastValue) return false;   // unchanged -> no repaint
        m_lastNorm = m_norm; m_lastValue = m_value; m_pushed = true;
        static_cast<GaugeWidget*>(m_gauge.data())->setReading(m_norm, m_value);
        return true;
    }
    namespace {
        // FIX: Added semicolon after struct definition
        struct DialGaugeRegistrar {
            DialGaugeRegistrar() {
                NodeDescriptor desc = { "Dial Gauge", "Visualization", "Visualizes a value in a min/max range." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("viz_dial_gauge", desc, []() { return std::make_unique<DialGaugeNode>(); });
            }
        };
    } // namespace
    // FIX: Declared static instance on its own line
    static DialGaugeRegistrar g_dialGaugeRegistrar;


    // ValuePlotterNode
    ValuePlotterNode::ValuePlotterNode() {
	m_id = "viz_plotter";
        // FIX: Use nested initializer for DataType
        m_ports.push_back({ "Time", {"float", "seconds"}, Port::Direction::Input, this });
        m_ports.push_back({ "Value", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "History", {"std::deque<std::pair<float, float>>", "timeseries"}, Port::Direction::Output, this });
    }
    void ValuePlotterNode::compute() {
        auto time = getInput<float>("Time");
        auto value = getInput<float>("Value");
        if (time && value) {
            m_history.push_back({ *time, *value });
            if (m_history.size() > MAX_HISTORY) {
                m_history.pop_front();
            }
            setOutput("History", m_history);
        }
    }
    namespace {
        // FIX: Added semicolon after struct definition
        struct ValuePlotterRegistrar {
            ValuePlotterRegistrar() {
                NodeDescriptor desc = { "Value Plotter", "Visualization", "Plots a value over time." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("viz_plotter", desc, []() { return std::make_unique<ValuePlotterNode>(); });
            }
        };
    } // namespace
    // FIX: Declared static instance on its own line
    static ValuePlotterRegistrar g_valuePlotterRegistrar;


    // --- Monitoring & Logging Node Implementations ---

    // DataMonitorNode
    DataMonitorNode::DataMonitorNode() {
	m_id = "mon_data_monitor";
        // FIX: Use nested initializer for DataType
        m_ports.push_back({ "Data In", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Data Out", {"float", "unitless"}, Port::Direction::Output, this });
    }
    void DataMonitorNode::compute() {
        auto input = getInput<float>("Data In");
        if (input) {
            if (errorCondition && errorCondition(*input)) {
                // In a real engine, this would push to a global ErrorLogManager service.
                std::cerr << "[ERROR LOG] Node '" << getId() << "': " << errorMessage
                    << " (Value: " << *input << ")\n";
            }
            // Always pass the data through, regardless of the error state.
            setOutput("Data Out", *input);
        }
    }
    namespace {
        // FIX: Added semicolon after struct definition
        struct DataMonitorRegistrar {
            DataMonitorRegistrar() {
                NodeDescriptor desc = { "Data Monitor", "Monitoring", "Logs an error if a condition is met." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("mon_data_monitor", desc, []() { return std::make_unique<DataMonitorNode>(); });
            }
        };
    } // namespace
    // FIX: Declared static instance on its own line
    static DataMonitorRegistrar g_dataMonitorRegistrar;



QWidget* DataMonitorNode::createCustomWidget()
{
    // TODO: Implement custom widget for "DataMonitorNode"
    return nullptr;
}


QWidget* ValuePlotterNode::createCustomWidget()
{
    // TODO: Implement custom widget for "ValuePlotterNode"
    return nullptr;
}


QWidget* DialGaugeNode::createCustomWidget()
{
    auto* g = new GaugeWidget();
    m_gauge = g;
    return g;
}

// --- NumericReadoutNode: a 7-segment LCD readout (Value + Digits + Decimals) ---
NumericReadoutNode::NumericReadoutNode() {
    m_id = "viz_numeric_readout";
    m_ports.push_back({ "Value",    {"double", "unitless"}, Port::Direction::Input,  this });
    m_ports.push_back({ "Digits",   {"int", "count"},       Port::Direction::Input,  this });
    m_ports.push_back({ "Decimals", {"int", "count"},       Port::Direction::Input,  this });
    m_ports.push_back({ "Display",  {"std::string", "text"},Port::Direction::Output, this });
}
void NumericReadoutNode::compute() {
    auto v = getInput<double>("Value");
    if (!v) return;                                    // disconnected -> no update (NEG-CTRL)
    int digits   = std::clamp(getInput<int>("Digits").value_or(6), 1, 16);
    int decimals = std::clamp(getInput<int>("Decimals").value_or(2), 0, 10);
    const QString text = QString::number(*v, 'f', decimals);
    setOutput<std::string>("Display", text.toStdString());
    m_text = text.toStdString(); m_digits = digits; m_have = true;   // value only -- the LCD is pushed in refreshUi()
}
bool NumericReadoutNode::refreshUi() {
    if (!m_have || !m_lcd) return false;
    if (m_pushed && m_text == m_lastText && m_digits == m_lastDigits) return false;   // unchanged -> no repaint
    m_lastText = m_text; m_lastDigits = m_digits; m_pushed = true;
    m_lcd->setDigitCount(m_digits);
    m_lcd->display(QString::fromStdString(m_text));
    return true;
}
QWidget* NumericReadoutNode::createCustomWidget()
{
    auto* lcd = new QLCDNumber();
    lcd->setSegmentStyle(QLCDNumber::Flat);
    lcd->setDigitCount(6);
    lcd->setMinimumSize(130, 48);
    m_lcd = lcd;
    return lcd;
}
namespace {
    struct NumericReadoutRegistrar {
        NumericReadoutRegistrar() {
            NodeDescriptor desc = { "Numeric Readout", "Visualization",
                "7-segment readout of a value; Digits sets segment count, Decimals sets fractional places." };
            NodeFactory::instance().registerNodeType("viz_numeric_readout", desc,
                []() { return std::make_unique<NumericReadoutNode>(); });
        }
    };
    static NumericReadoutRegistrar g_numericReadoutRegistrar;
} // namespace


QWidget* ConditionalLightNode::createCustomWidget()
{
    // TODO: Implement custom widget for "ConditionalLightNode"
    return nullptr;
}
} // namespace NodeLibrary