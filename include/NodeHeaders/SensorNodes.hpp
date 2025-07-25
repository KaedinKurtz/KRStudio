#pragma once

#include <QWidget>

#include "Node.hpp"
#include "NodeFactory.hpp"
#include "ObservableDataManager.hpp" // Crucial include
#include "components.hpp" // For ImuDataPoint, Pose6D, etc.
#include <glm/glm.hpp>
#include <string>

namespace NodeLibrary {

    /**
     * @brief A generic, configurable node to get any data source from the manager.
     * This is the base for most sensor nodes that poll data.
     */
    template<typename T>
    class SensorNode : public Node {
    public:
        QWidget* createCustomWidget() override {
            // TODO: Implement a custom widget for SensorNode if needed.
            // This widget could have a dropdown to select the m_dataSourceID.
            return nullptr;
        }

        // Constructor takes the string name of the data type for the port.
        SensorNode(const std::string& dataTypeName) {
            // FIX: The port's type must be initialized as a DataType struct {name, unit}.
            m_ports.push_back({ "Value", {"ProfiledData<" + dataTypeName + ">", "data_packet"}, Port::Direction::Output, this });
        }

        // This ID is set by the IDE when the user selects a data stream from a dropdown.
        void setDataSourceID(const std::string& id) {
            m_dataSourceID = id;
        }

        // The compute method fetches the data from the manager.
        void compute() override {
            if (!m_dataSourceID.empty()) {
                // Get the data from the central manager singleton.
                auto data = ObservableDataManager::instance().getData<T>(m_dataSourceID);
                if (data) {
                    // Pass the full profiled data packet through the output port.
                    setOutput("Value", *data);
                }
            }
        }
    protected:
        // The unique string identifier for the data stream (e.g., "robot1/imu/raw").
        std::string m_dataSourceID;
    };


    // --- Specific Sensor Node Implementations ---

    /**
     * @brief A utility node that unpacks a full ImuDataPoint packet into its components.
     * This is more convenient than accessing struct members directly in the graph.
     */
    class ImuUnpackerNode : public Node {
    public:
        QWidget* createCustomWidget() override;
        ImuUnpackerNode();
        void compute() override;
    };


    // --- Concrete, Registered Sensor Nodes ---
    // We use 'using' to create specific, named types from the generic template.

    using PoseSensorNode = SensorNode<Pose6D>;
    using FloatSensorNode = SensorNode<float>;

} // namespace NodeLibrary