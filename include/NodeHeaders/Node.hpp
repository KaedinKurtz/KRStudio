#pragma once

#include <string>
#include <vector>
#include <any>
#include <map>
#include <optional>
#include <chrono>
#include "components.hpp"
#include <QWidget>
#include <glm/glm.hpp> // For glm::vec3, etc.


// Forward declaration
class Node;

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
    std::optional<PortDataPacket> packet;
};

class Node {
public:
    enum class UpdatePolicy { Asynchronous, Synchronous, Triggered };
    enum class TriggerEdge { Rising, Falling, Both };

    virtual ~Node() = default;

    virtual QWidget* createCustomWidget() { return nullptr; }

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
            if (port.direction == Port::Direction::Input && port.name == portName && port.packet.has_value()) {
                try {
                    return std::any_cast<T>(port.packet->data);
                }
                catch (const std::bad_any_cast&) { return std::nullopt; }
            }
        }
        return std::nullopt;
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

    const std::string& getId() const { return m_id; }
    const std::vector<Port>& getPorts() const { return m_ports; }
    virtual void configureForType(const std::type_info& typeInfo) {}
    virtual QWidget* createEmbeddedWidget() { return nullptr; }

protected:
    Node() {
        m_ports.push_back({ "Trigger", {"bool", "unitless"}, Port::Direction::Input, this });
    }
    virtual void compute() = 0;

    std::string m_id;
    std::vector<Port> m_ports;
    UpdatePolicy m_updatePolicy = UpdatePolicy::Asynchronous;
    TriggerEdge m_triggerEdge = TriggerEdge::Rising;
    bool m_lastTriggerState = false;
    float m_lastExecutionTimeMs = 0.0f;
};
