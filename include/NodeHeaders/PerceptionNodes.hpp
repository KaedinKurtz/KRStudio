#pragma once

#include "Node.hpp"
#include "NodeFactory.hpp"
#include <vector>
#include <optional>
#include <glm/glm.hpp>

namespace NodeLibrary {

    // --- Data Structures ---

    // A simple struct to represent an image for node inputs/outputs.
    struct Image {
        std::vector<unsigned char> pixel_data;
        int width = 0, height = 0, channels = 0;
    };

    // A simple struct to represent a detected plane.
    struct Plane {
        glm::vec3 normal{ 0.f };
        float distance_from_origin = 0.f;
        std::vector<int> inlier_indices;
    };


    // --- Free Functions (to be wrapped by nodes) ---

    std::vector<glm::vec3> downsamplePointCloud(const std::vector<glm::vec3>& cloud, float leaf_size);
    std::vector<glm::vec3> removeStatisticalOutliers(const std::vector<glm::vec3>& cloud, int num_neighbors, float std_dev_multiplier);
    std::optional<Plane> segmentPlaneRANSAC(const std::vector<glm::vec3>& cloud, float distance_threshold, int max_iterations);
    Image convertToGrayscale(const Image& color_image);
    Image detectEdges(const Image& grayscale_image, float low_threshold, float high_threshold);


    // --- Node Classes ---

    class DownsamplePointCloudNode : public Node {
    public:
        DownsamplePointCloudNode();
        void compute() override;
    };

    class RemoveOutliersNode : public Node {
    public:
        RemoveOutliersNode();
        void compute() override;
    };

    class SegmentPlaneNode : public Node {
    public:
        SegmentPlaneNode();
        void compute() override;
    };

    class ConvertToGrayscaleNode : public Node {
    public:
        ConvertToGrayscaleNode();
        void compute() override;
    };

    class DetectEdgesNode : public Node {
    public:
        DetectEdgesNode();
        void compute() override;
    };

} // namespace NodeLibrary