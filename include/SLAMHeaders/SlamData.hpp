#pragma once

#include <cstdint>
#include <vector>
#include <memory>

// Include the necessary Eigen headers.
// 'Dense' has Matrix and Vector types. 'Geometry' has transformations like Isometry.
#include <Eigen/Dense>
#include <Eigen/Geometry>

/**
 * @struct Surfel
 * @brief Represents a "Surface Element" using Eigen types for vector math.
 */
struct Surfel {
    Eigen::Vector3f position;       // Averaged position of the surface element.
    Eigen::Vector3f normal;         // Averaged normal vector of the surface.
    uint8_t r, g, b;                // Averaged color.
    float radius = 0.f;             // Represents the size/spread of points fused here.
    float confidence = 0.f;         // How certain we are about this surfel's properties.
    double last_update_time = 0.0;  // Timestamp of the last fusion.
    int update_count = 0;           // Number of measurements fused into this surfel.
};

/**
 * @struct KeyFrame
 * @brief A snapshot using Eigen types for its 6-DoF pose.
 */
struct KeyFrame {
    using Ptr = std::shared_ptr<KeyFrame>; // Use smart pointers for easy memory management

    long long id = 0;           // Unique identifier for the KeyFrame.
    double timestamp = 0.0;     // Timestamp of capture.

    // Use Eigen's Isometry3f for the pose. This is more robust and explicit
    // than a raw 4x4 matrix for representing rigid body transformations.
    Eigen::Isometry3f pose = Eigen::Isometry3f::Identity();

    // Raw data for reprocessing.
    std::vector<Eigen::Vector3f> point_cloud;
    std::vector<Eigen::Vector2f> texture_coordinates; // ADDED: Stores the (u,v) coords
    std::vector<uint8_t> color_data;
    int color_width = 0, color_height = 0;
    int color_bpp = 0; // ADDED: Bytes per pixel for color data
};