#include "VoxelMap.hpp"
#include <cmath> // for std::floor

VoxelMap::VoxelMap(float voxel_size)
    : m_voxel_size(voxel_size), m_inv_voxel_size(1.0f / voxel_size) {
}

void VoxelMap::fuse(const KeyFrame::Ptr& keyframe) {
    std::lock_guard<std::mutex> lock(m_map_mutex);

    const Eigen::Isometry3f& pose = keyframe->pose;

    // Iterate using an index to access points and texture coordinates simultaneously.
    for (size_t i = 0; i < keyframe->point_cloud.size(); ++i) {
        const auto& local_point = keyframe->point_cloud[i];
        Eigen::Vector3f world_point = pose * local_point;

        VoxelIndex index = { /* ... calculate index ... */ };
        auto it = m_grid.find(index);

        // --- Get the color for this specific point ---
        const auto& tex_coord = keyframe->texture_coordinates[i];
        int u = static_cast<int>(tex_coord.x() * keyframe->color_width);
        int v = static_cast<int>(tex_coord.y() * keyframe->color_height);
        uint8_t r = 128, g = 128, b = 128; // Default color

        if (u >= 0 && u < keyframe->color_width && v >= 0 && v < keyframe->color_height) {
            int color_idx = (v * keyframe->color_width + u) * keyframe->color_bpp;
            r = keyframe->color_data[color_idx];
            g = keyframe->color_data[color_idx + 1];
            b = keyframe->color_data[color_idx + 2];
        }

        if (it == m_grid.end()) {
            // --- This voxel is empty: Create a new Surfel ---
            Surfel new_surfel;
            new_surfel.position = world_point;

            // The normal of a point from a depth camera can be approximated by the
            // vector from the camera to the point, transformed into the world frame.
            Eigen::Vector3f camera_position = pose.translation();
            new_surfel.normal = (camera_position - world_point).normalized();

            new_surfel.update_count = 1;
            new_surfel.confidence = 0.1f; // Initial low confidence
            new_surfel.last_update_time = keyframe->timestamp;

            new_surfel.r = r;
            new_surfel.g = g;
            new_surfel.b = b;

            m_grid[index] = new_surfel; // Insert the new surfel into the map.
        }
        else {
            // --- This voxel is occupied: Fuse the measurement with the existing Surfel ---
            Surfel& existing_surfel = it->second;

            int n = existing_surfel.update_count;
            float weight = 1.0f / (n + 1);

            // Update the position with a weighted average.
            existing_surfel.position = (existing_surfel.position * n + world_point) * weight;

            // Update the normal with a weighted average.
            Eigen::Vector3f new_normal = (pose.translation() - world_point).normalized();
            existing_surfel.normal = (existing_surfel.normal * n + new_normal).normalized();

            // Increment the update count and update timestamp.
            existing_surfel.update_count++;
            existing_surfel.last_update_time = keyframe->timestamp;

            // Increase confidence, capping at 1.0.
            existing_surfel.confidence = std::min(1.0f, existing_surfel.confidence + 0.05f);

            existing_surfel.r = static_cast<uint8_t>((existing_surfel.r * n + r) * weight);
            existing_surfel.g = static_cast<uint8_t>((existing_surfel.g * n + g) * weight);
            existing_surfel.b = static_cast<uint8_t>((existing_surfel.b * n + b) * weight);
        }
    }
}

std::vector<Surfel> VoxelMap::getSurfels() const {
    // Lock for thread-safe reading.
    std::lock_guard<std::mutex> lock(m_map_mutex);

    std::vector<Surfel> surfels;
    surfels.reserve(m_grid.size()); // Pre-allocate for efficiency.

    for (const auto& pair : m_grid) {
        surfels.push_back(pair.second);
    }
    return surfels;
}