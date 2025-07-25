#include "SensorNodes.hpp"
#include <vector>
#include <memory> // Required for std::make_unique

namespace NodeLibrary {

    // --- ImuUnpackerNode Implementation ---

    ImuUnpackerNode::ImuUnpackerNode() {
	m_id = "sensor_imu_unpacker";
        // FIX: Use nested initializer {name, unit} for each DataType.
        m_ports.push_back({ "IMU Data In", {"ProfiledData<ImuDataPoint>", "data_packet"}, Port::Direction::Input, this });
        m_ports.push_back({ "Angular Velocity", {"glm::vec3", "rad/s"}, Port::Direction::Output, this });
        m_ports.push_back({ "Linear Acceleration", {"glm::vec3", "m/s^2"}, Port::Direction::Output, this });
        m_ports.push_back({ "Timestamp", {"double", "seconds"}, Port::Direction::Output, this });
    }

    void ImuUnpackerNode::compute() {
        // Get the optional containing the shared_ptr to the const ProfiledData packet.
        auto opt_data_ptr = getInput<std::shared_ptr<const ProfiledData<ImuDataPoint>>>("IMU Data In");

        if (opt_data_ptr) {
            // Dereference the optional to get the actual shared_ptr.
            const auto& profiled_data_ptr = *opt_data_ptr;

            // LOGIC FIX: Use the arrow operator '->' to access members of the object the smart pointer points to.
            const ImuDataPoint& imu_value = profiled_data_ptr->value;

            // Output the individual fields.
            setOutput("Angular Velocity", imu_value.angular_velocity);
            setOutput("Linear Acceleration", imu_value.linear_acceleration);
            setOutput("Timestamp", profiled_data_ptr->publication_timestamp);
        }
    }

    namespace {
        struct ImuUnpackerNodeRegistrar {
            ImuUnpackerNodeRegistrar() {
                NodeDescriptor desc = { "Unpack IMU Data", "Inputs/Sensors", "Unpacks an IMU data stream into its components." };
                // FIX: Use std::make_unique to return a std::unique_ptr<Node>.
                NodeFactory::instance().registerNodeType("sensor_imu_unpacker", desc, []() { return std::make_unique<ImuUnpackerNode>(); });
            }
        } g_imuUnpackerRegistrar;
    }


    // --- Concrete Sensor Node Registrations ---

    // PoseSensorNode
    namespace {
        struct PoseSensorNodeRegistrar {
            PoseSensorNodeRegistrar() {
                NodeDescriptor desc = { "Pose Sensor", "Inputs/Sensors", "Provides a Pose6D data stream from the data manager." };
                // FIX: Use std::make_unique and pass constructor arguments.
                NodeFactory::instance().registerNodeType("sensor_pose", desc, []() { return std::make_unique<PoseSensorNode>("Pose6D"); });
            }
        } g_poseSensorRegistrar;
    }

    // FloatSensorNode
    namespace {
        struct FloatSensorNodeRegistrar {
            FloatSensorNodeRegistrar() {
                NodeDescriptor desc = { "Float Sensor", "Inputs/Sensors", "Provides a float data stream from the data manager." };
                // FIX: Use std::make_unique and pass constructor arguments.
                NodeFactory::instance().registerNodeType("sensor_float", desc, []() { return std::make_unique<FloatSensorNode>("float"); });
            }
        } g_floatSensorRegistrar;
    }




QWidget* ImuUnpackerNode::createCustomWidget()
{
    // TODO: Implement custom widget for "ImuUnpackerNode"
    return nullptr;
}
} // namespace NodeLibrary