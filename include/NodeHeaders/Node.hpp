#pragma once

#include <string>
#include <vector>
#include <any>
#include <type_traits>
#include <map>
#include <optional>
#include <chrono>
#include <functional>
#include "components.hpp"
#include <QWidget>
#include <glm/glm.hpp> // For glm::vec3, etc.


// Forward declaration
class Node;
class Scene;   // Phase 5: the live ECS scene a node may read/write (injected, not owned)

/**
 * @brief A rich descriptor for a port's data type, including physical units.
 */
struct DataType {
    std::string name; // The C++ type name, e.g., "float", "glm::vec3"
    std::string unit; // The physical unit, e.g., "meters", "radians/s", "unitless"

    bool operator==(const DataType& other) const {
        return name == other.name && unit == other.unit;
    }
};

/** @brief Performance metadata that travels with every piece of data. */
struct PerformanceData {
    float self_ms = 0.0f;
    float upstream_ms = 0.0f;
};

/** @brief The complete data packet that flows through ports. */
struct PortDataPacket {
    std::any data;
    DataType type;
    PerformanceData perf;
};

// A basic port descriptor, now with a rich DataType
struct Port {
    std::string name;
    DataType type; // <-- UPGRADED
    enum class Direction { Input, Output };
    Direction direction;
    Node* parentNode = nullptr;
    bool isFresh = false;
    std::optional<PortDataPacket> packet;        // value delivered by an upstream CONNECTION
    std::optional<PortDataPacket> literalValue;  // value typed into the in-node input widget (used when unconnected)
    std::vector<std::string> enumOptions;        // for an "enum" input: the combo labels (selection = int index)
};

class Node {
public:
    enum class UpdatePolicy { Asynchronous, Synchronous, Triggered };
    enum class TriggerEdge { Rising, Falling, Both };

    virtual ~Node() = default;

    virtual QWidget* createCustomWidget() { return nullptr; }

    // Push this node's freshly-computed value into its DISPLAY widget, IF it changed since the last push.
    // Called at a capped UI rate (refreshGraphUi), NEVER per eval -- so widget repaints are decoupled from
    // (and far below) the evaluation rate. Returns true if it actually repainted (the value changed).
    // Default: nodes with no display widget do nothing. Display nodes (readout/gauge) override this and
    // must NOT touch their widget inside compute().
    virtual bool refreshUi() { return false; }

    // --- Core Execution Logic (remains the same) ---
    void process() {
        auto start_time = std::chrono::high_resolution_clock::now();

        // --- REPLACE THE EXISTING 'process' LOGIC WITH THIS ---
        bool shouldFire = false;
        if (m_updatePolicy == UpdatePolicy::Triggered) {
            auto triggerOpt = getInput<bool>("Trigger");
            if (triggerOpt) {
                bool triggerVal = *triggerOpt;
                if ((m_triggerEdge == TriggerEdge::Rising && triggerVal && !m_lastTriggerState) ||
                    (m_triggerEdge == TriggerEdge::Falling && !triggerVal && m_lastTriggerState) ||
                    (m_triggerEdge == TriggerEdge::Both && triggerVal != m_lastTriggerState))
                {
                    shouldFire = true;
                }
                m_lastTriggerState = triggerVal;
            }
        }
        else if (m_updatePolicy == UpdatePolicy::Synchronous) {
            auto holdOpt = getInput<bool>("Trigger"); // Now used as "Hold"
            if (holdOpt && *holdOpt) return; // If Hold is high, do nothing.

            bool allFresh = true;
            for (auto& p : m_ports) {
                if (p.direction == Port::Direction::Input && p.name != "Trigger" && !p.isFresh) {
                    allFresh = false;
                    break;
                }
            }
            if (allFresh) {
                shouldFire = true;
                // Mark all inputs as no longer fresh
                for (auto& p : m_ports) {
                    if (p.direction == Port::Direction::Input && p.name != "Trigger") p.isFresh = false;
                }
            }
        }
        else { // Asynchronous (Continuous)
            auto holdOpt = getInput<bool>("Trigger"); // Now used as "Hold"
            if (holdOpt && *holdOpt) return; // If Hold is high, do nothing.

            shouldFire = true; // Always try to compute
        }

        if (shouldFire) {
            compute();
        }
        else {
            return;
        }
        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<float, std::milli> duration = end_time - start_time;
        m_lastExecutionTimeMs = duration.count();
    }

    // --- Configuration ---
    void setUpdatePolicy(UpdatePolicy policy) { m_updatePolicy = policy; }
    UpdatePolicy getUpdatePolicy() const { return m_updatePolicy; }
    void setTriggerEdge(TriggerEdge edge) { m_triggerEdge = edge; }
    TriggerEdge getTriggerEdge() const { return m_triggerEdge; }

    virtual bool needsExecutionControls() const { return true; }

    // True if the node's output is a PURE function of its editable inputs (drive an input -> output changes).
    // Event/stateful sources (a Button whose output is a momentary pulse gated by a press, an IK node that
    // samples only on a trigger edge) override to false so the INPUT-BIND gate does not require driving an
    // input to change the output -- they still must MOUNT their input widgets.
    virtual bool isPureInputFunction() const { return true; }

    // --- Port Data Management ---

    void setInput(const std::string& portName, const PortDataPacket& newPacket) {
        for (auto& port : m_ports) {
            if (port.direction == Port::Direction::Input && port.name == portName) {
                // The graph runner MUST ensure port.type == newPacket.type before calling this.
                port.packet = newPacket;
                port.isFresh = true;
                return;
            }
        }
    }

    template<typename T>
    std::optional<T> getInput(const std::string& portName) {
        for (const auto& port : m_ports) {
            if (port.direction == Port::Direction::Input && port.name == portName) {
                // a live CONNECTION (packet) wins; otherwise fall back to the in-node widget's literal.
                const std::optional<PortDataPacket>& src = port.packet.has_value() ? port.packet : port.literalValue;
                if (!src.has_value()) return std::nullopt;
                try { return std::any_cast<T>(src->data); } catch (const std::bad_any_cast&) {}
                // NUMERIC COERCION: a "number" port carries double/float/int/bool interchangeably (the
                // unified type id). So a float-reading node accepts a double-carrying wire, etc.
                if constexpr (std::is_arithmetic_v<T>) {
                    try { return T(std::any_cast<double>(src->data)); } catch (...) {}
                    try { return T(std::any_cast<float>(src->data)); } catch (...) {}
                    try { return T(std::any_cast<int>(src->data)); } catch (...) {}
                    try { return T(std::any_cast<bool>(src->data) ? 1 : 0); } catch (...) {}
                }
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    // Set the literal value of an INPUT port (what the in-node input widget writes). Read by getInput
    // when the port has no live connection.
    template<typename T>
    void setPortLiteral(const std::string& portName, const T& value) {
        for (auto& port : m_ports) {
            if (port.direction == Port::Direction::Input && port.name == portName) {
                PortDataPacket pk; pk.data = value; pk.type = port.type; port.literalValue = pk; return;
            }
        }
    }

    // Clear an input port's CONNECTION packet (used to model a severed wire). The literal (if any) remains.
    void clearInputPacket(const std::string& portName) {
        for (auto& port : m_ports)
            if (port.direction == Port::Direction::Input && port.name == portName) { port.packet.reset(); port.isFresh = false; return; }
    }

    // Numeric convenience: read a port as a double, accepting double/float/bool/int (the library nodes
    // pass numbers around as doubles; bool ports carry 1.0/0.0). Returns def if unset / non-numeric.
    double getInputD(const std::string& portName, double def = 0.0) {
        if (auto d = getInput<double>(portName)) return *d;
        if (auto f = getInput<float>(portName))  return double(*f);
        if (auto b = getInput<bool>(portName))   return *b ? 1.0 : 0.0;
        if (auto i = getInput<int>(portName))    return double(*i);
        return def;
    }

    // Read an input port's LITERAL value (the in-node widget's stored value) as a double -- used to
    // INITIALIZE that widget so it shows the node's default instead of 0. Ignores live connections.
    double literalD(const std::string& portName, double def = 0.0) const {
        for (const auto& port : m_ports) {
            if (port.direction == Port::Direction::Input && port.name == portName && port.literalValue.has_value()) {
                const std::any& d = port.literalValue->data;
                try { return std::any_cast<double>(d); } catch (...) {}
                try { return double(std::any_cast<float>(d)); } catch (...) {}
                try { return double(std::any_cast<int>(d)); } catch (...) {}
                try { return std::any_cast<bool>(d) ? 1.0 : 0.0; } catch (...) {}
            }
        }
        return def;
    }

    template<typename T>
    void setOutput(const std::string& portName, const T& value) {
        for (auto& port : m_ports) {
            if (port.direction == Port::Direction::Output && port.name == portName) {
                float max_upstream_latency = 0.0f;
                for (const auto& inPort : m_ports) {
                    if (inPort.direction == Port::Direction::Input && inPort.packet.has_value()) {
                        float total_latency = inPort.packet->perf.self_ms + inPort.packet->perf.upstream_ms;
                        if (total_latency > max_upstream_latency) {
                            max_upstream_latency = total_latency;
                        }
                    }
                }
                PortDataPacket newPacket;
                newPacket.data = value;
                newPacket.type = port.type; // Use the port's predefined type
                newPacket.perf.self_ms = m_lastExecutionTimeMs;
                newPacket.perf.upstream_ms = max_upstream_latency;
                port.packet = newPacket;
                return;
            }
        }
    }

    // Declare an ENUM input port: an in-node combo of `options`; the selection is stored as the port's
    // int-index literal and read by compute via getInput<int>(name). A wire still overrides the combo.
    void addEnumInputPort(const std::string& name, const std::vector<std::string>& options) {
        Port p;
        p.name = name;
        p.type = { "enum", "unitless" };
        p.direction = Port::Direction::Input;
        p.parentNode = this;
        p.enumOptions = options;
        m_ports.push_back(std::move(p));
    }

    const std::string& getId() const { return m_id; }
    const std::vector<Port>& getPorts() const { return m_ports; }
    virtual void configureForType(const std::type_info& typeInfo) {}
    virtual QWidget* createEmbeddedWidget() { return nullptr; }

    // --- Phase 5 live-backend bridge: the graph runner injects the active Scene so a node's
    //     compute() can read/write the real ECS registry (e.g. a SceneContext source node emits
    //     the registry pointer the Physics nodes consume). Injected, never owned. ---
    void setScene(Scene* s) { m_scene = s; }
    Scene* scene() const { return m_scene; }

    // --- Runtime PORT reconfiguration. A node whose port SET depends on runtime state (e.g. the Property
    //     node, whose outputs are X/Y/Z for a vector property, Roll/Pitch/Yaw for orientation, or a single
    //     Value for mass) changes its ports by passing the mutation to changePorts(). The NodeDelegate
    //     installs reconfigurePorts so the change is bracketed by the QtNodes portsAboutToBeDeleted/
    //     portsDeleted/portsAboutToBeInserted/portsInserted signals (which clean up stale connections and
    //     rebuild the visual node + geometry). Headless (gates) it is null -> the mutation applies directly.
    std::function<void(const std::function<void()>& applyMutation)> reconfigurePorts;
    void changePorts(const std::function<void()>& applyMutation) {
        if (reconfigurePorts) reconfigurePorts(applyMutation);
        else if (applyMutation) applyMutation();
    }

    // Drive the node's primary named selection (e.g. the Property node's property) -- the same path the
    // in-node combo uses, so it may reconfigure ports. Default no-op; lets tests + a restored param re-apply.
    virtual bool selectNamedOption(const std::string& /*option*/) { return false; }

    // --- Phase 1 node PARAMETERS: tunable internal state that is NOT a wired input port. An in-node
    //     widget (slider/dial/spinbox) binds to a param by name and writes it via setParam(); compute()
    //     reads it via getParam(). This is the behaviorally-meaningful, headless-gateable layer of the
    //     in-node UI -- the gate drives a param and asserts the OUTPUT changes (the widget is a thin
    //     binding over this). Params survive across process() calls (unlike port packets). ---
    template<typename T> void setParam(const std::string& name, const T& v) { m_params[name] = v; }
    template<typename T> T getParam(const std::string& name, const T& def) const {
        auto it = m_params.find(name);
        if (it == m_params.end()) return def;
        try { return std::any_cast<T>(it->second); } catch (const std::bad_any_cast&) { return def; }
    }
    bool hasParam(const std::string& name) const { return m_params.find(name) != m_params.end(); }
    const std::map<std::string, std::any>& params() const { return m_params; }

protected:
    Node() {
        m_ports.push_back({ "Trigger", {"bool", "unitless"}, Port::Direction::Input, this });
    }
    virtual void compute() = 0;

    std::string m_id;
    Scene* m_scene = nullptr;   // live backend, injected by the graph runner (Phase 5)
    std::vector<Port> m_ports;
    std::map<std::string, std::any> m_params;   // tunable internal state driven by in-node widgets (Phase 1)
    UpdatePolicy m_updatePolicy = UpdatePolicy::Asynchronous;
    TriggerEdge m_triggerEdge = TriggerEdge::Rising;
    bool m_lastTriggerState = false;
    float m_lastExecutionTimeMs = 0.0f;
};
