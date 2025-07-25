#include "PerceptionNodes.hpp"
#include <iostream> 
#include <numeric>  
#include <memory>   // Required for std::make_unique

namespace NodeLibrary {

    // --- Free Function Implementations (Placeholders) ---
    // (Implementations remain the same)
    std::vector<glm::vec3> downsamplePointCloud(const std::vector<glm::vec3>& cloud, float leaf_size) {
        std::cout << "PERCEPTION: Downsampling point cloud with leaf size " << leaf_size << "...\n";
        std::vector<glm::vec3> downsampled;
        for (size_t i = 0; i < cloud.size(); i += 10) {
            downsampled.push_back(cloud[i]);
        }
        return downsampled;
    }

    std::vector<glm::vec3> removeStatisticalOutliers(const std::vector<glm::vec3>& cloud, int num_neighbors, float std_dev_multiplier) {
        std::cout << "PERCEPTION: Removing outliers with " << num_neighbors << " neighbors and std dev mult " << std_dev_multiplier << "...\n";
        return cloud;
    }

    std::optional<Plane> segmentPlaneRANSAC(const std::vector<glm::vec3>& cloud, float distance_threshold, int max_iterations) {
        std::cout << "PERCEPTION: Segmenting plane with RANSAC...\n";
        if (cloud.size() < 3) return std::nullopt;
        Plane p;
        p.normal = { 0.0f, 1.0f, 0.0f };
        p.distance_from_origin = 0.0f;
        p.inlier_indices.resize(cloud.size());
        std::iota(p.inlier_indices.begin(), p.inlier_indices.end(), 0);
        return p;
    }

    Image convertToGrayscale(const Image& color_image) {
        std::cout << "PERCEPTION: Converting image to grayscale...\n";
        Image gray;
        gray.width = color_image.width;
        gray.height = color_image.height;
        gray.channels = 1;
        gray.pixel_data.resize(gray.width * gray.height, 128);
        return gray;
    }

    Image detectEdges(const Image& grayscale_image, float low_threshold, float high_threshold) {
        std::cout << "PERCEPTION: Detecting edges with thresholds " << low_threshold << ", " << high_threshold << "...\n";
        return grayscale_image;
    }


    // --- Node Implementations & Registrations ---

    // DownsamplePointCloudNode
    DownsamplePointCloudNode::DownsamplePointCloudNode() {
	m_id = "perception_downsample";
        // FIX: Use nested initializer for DataType
        m_ports.push_back({ "Input Cloud", {"std::vector<glm::vec3>", "points"}, Port::Direction::Input, this });
        m_ports.push_back({ "Leaf Size", {"float", "meters"}, Port::Direction::Input, this });
        m_ports.push_back({ "Output Cloud", {"std::vector<glm::vec3>", "points"}, Port::Direction::Output, this });
    }

    void DownsamplePointCloudNode::compute() {
        auto cloud = getInput<std::vector<glm::vec3>>("Input Cloud");
        auto leaf_size = getInput<float>("Leaf Size");
        if (cloud && leaf_size) {
            setOutput("Output Cloud", downsamplePointCloud(*cloud, *leaf_size));
        }
    }

    namespace {
        struct DownsamplePointCloudRegistrar {
            DownsamplePointCloudRegistrar() {
                NodeDescriptor desc = { "Downsample Point Cloud", "Perception/PointCloud", "Reduces point cloud density." };
                // FIX: Use std::make_unique to return a std::unique_ptr<Node>
                NodeFactory::instance().registerNodeType("perception_downsample", desc, []() { return std::make_unique<DownsamplePointCloudNode>(); });
            }
        } g_downsamplePointCloudRegistrar;
    }

    // RemoveOutliersNode
    RemoveOutliersNode::RemoveOutliersNode() {
	m_id = "perception_remove_outliers";
        // FIX: Use nested initializer for DataType
        m_ports.push_back({ "Input Cloud", {"std::vector<glm::vec3>", "points"}, Port::Direction::Input, this });
        m_ports.push_back({ "Neighbors", {"int", "count"}, Port::Direction::Input, this });
        m_ports.push_back({ "Std Dev Multiplier", {"float", "unitless"}, Port::Direction::Input, this });
        m_ports.push_back({ "Output Cloud", {"std::vector<glm::vec3>", "points"}, Port::Direction::Output, this });
    }

    void RemoveOutliersNode::compute() {
        auto cloud = getInput<std::vector<glm::vec3>>("Input Cloud");
        auto neighbors = getInput<int>("Neighbors");
        auto std_dev = getInput<float>("Std Dev Multiplier");
        if (cloud && neighbors && std_dev) {
            setOutput("Output Cloud", removeStatisticalOutliers(*cloud, *neighbors, *std_dev));
        }
    }

    namespace {
        struct RemoveOutliersRegistrar {
            RemoveOutliersRegistrar() {
                NodeDescriptor desc = { "Remove Outliers", "Perception/PointCloud", "Removes statistical outliers from a point cloud." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("perception_remove_outliers", desc, []() { return std::make_unique<RemoveOutliersNode>(); });
            }
        } g_removeOutliersRegistrar;
    }

    // SegmentPlaneNode
    SegmentPlaneNode::SegmentPlaneNode() {
	m_id = "perception_segment_plane";
        // FIX: Use nested initializer for DataType
        m_ports.push_back({ "Input Cloud", {"std::vector<glm::vec3>", "points"}, Port::Direction::Input, this });
        m_ports.push_back({ "Distance Threshold", {"float", "meters"}, Port::Direction::Input, this });
        m_ports.push_back({ "Max Iterations", {"int", "count"}, Port::Direction::Input, this });
        m_ports.push_back({ "Plane", {"Plane", "unitless"}, Port::Direction::Output, this });
        m_ports.push_back({ "Success", {"bool", "unitless"}, Port::Direction::Output, this });
    }

    void SegmentPlaneNode::compute() {
        auto cloud = getInput<std::vector<glm::vec3>>("Input Cloud");
        auto threshold = getInput<float>("Distance Threshold");
        auto iterations = getInput<int>("Max Iterations");
        if (cloud && threshold && iterations) {
            auto plane_opt = segmentPlaneRANSAC(*cloud, *threshold, *iterations);
            if (plane_opt) {
                setOutput("Plane", *plane_opt);
                setOutput("Success", true);
            }
            else {
                setOutput("Success", false);
            }
        }
    }

    namespace {
        struct SegmentPlaneRegistrar {
            SegmentPlaneRegistrar() {
                NodeDescriptor desc = { "Segment Plane (RANSAC)", "Perception/PointCloud", "Finds the dominant plane in a point cloud." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("perception_segment_plane", desc, []() { return std::make_unique<SegmentPlaneNode>(); });
            }
        } g_segmentPlaneRegistrar;
    }

    // ConvertToGrayscaleNode
    ConvertToGrayscaleNode::ConvertToGrayscaleNode() {
	m_id = "perception_to_grayscale";
        // FIX: Use nested initializer for DataType
        m_ports.push_back({ "Color Image", {"Image", "pixels"}, Port::Direction::Input, this });
        m_ports.push_back({ "Grayscale Image", {"Image", "pixels"}, Port::Direction::Output, this });
    }

    void ConvertToGrayscaleNode::compute() {
        auto image = getInput<Image>("Color Image");
        if (image) {
            setOutput("Grayscale Image", convertToGrayscale(*image));
        }
    }

    namespace {
        struct ConvertToGrayscaleRegistrar {
            ConvertToGrayscaleRegistrar() {
                NodeDescriptor desc = { "To Grayscale", "Perception/Image", "Converts a color image to grayscale." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("perception_to_grayscale", desc, []() { return std::make_unique<ConvertToGrayscaleNode>(); });
            }
        } g_convertToGrayscaleRegistrar;
    }

    // DetectEdgesNode
    DetectEdgesNode::DetectEdgesNode() {
	m_id = "perception_detect_edges";
        // FIX: Use nested initializer for DataType
        m_ports.push_back({ "Grayscale Image", {"Image", "pixels"}, Port::Direction::Input, this });
        m_ports.push_back({ "Low Threshold", {"float", "intensity"}, Port::Direction::Input, this });
        m_ports.push_back({ "High Threshold", {"float", "intensity"}, Port::Direction::Input, this });
        m_ports.push_back({ "Edge Image", {"Image", "pixels"}, Port::Direction::Output, this });
    }

    void DetectEdgesNode::compute() {
        auto image = getInput<Image>("Grayscale Image");
        auto low_thresh = getInput<float>("Low Threshold");
        auto high_thresh = getInput<float>("High Threshold");
        if (image && low_thresh && high_thresh) {
            setOutput("Edge Image", detectEdges(*image, *low_thresh, *high_thresh));
        }
    }

    namespace {
        struct DetectEdgesRegistrar {
            DetectEdgesRegistrar() {
                NodeDescriptor desc = { "Detect Edges (Canny)", "Perception/Image", "Finds edges in a grayscale image." };
                // FIX: Use std::make_unique
                NodeFactory::instance().registerNodeType("perception_detect_edges", desc, []() { return std::make_unique<DetectEdgesNode>(); });
            }
        } g_detectEdgesRegistrar;
    }



QWidget* DetectEdgesNode::createCustomWidget()
{
    // TODO: Implement custom widget for "DetectEdgesNode"
    return nullptr;
}


QWidget* ConvertToGrayscaleNode::createCustomWidget()
{
    // TODO: Implement custom widget for "ConvertToGrayscaleNode"
    return nullptr;
}


QWidget* SegmentPlaneNode::createCustomWidget()
{
    // TODO: Implement custom widget for "SegmentPlaneNode"
    return nullptr;
}


QWidget* RemoveOutliersNode::createCustomWidget()
{
    // TODO: Implement custom widget for "RemoveOutliersNode"
    return nullptr;
}


QWidget* DownsamplePointCloudNode::createCustomWidget()
{
    // TODO: Implement custom widget for "DownsamplePointCloudNode"
    return nullptr;
}
} // namespace NodeLibrary