#pragma once

#include "SlamData.hpp"
#include <unordered_map>
#include <mutex>

// A simple hashable struct to use as a key in our unordered_map.
struct VoxelIndex {
    int x, y, z;
    bool operator==(const VoxelIndex& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

// Custom hash function for VoxelIndex
namespace std {
    template <> struct hash<VoxelIndex> {
        size_t operator()(const VoxelIndex& i) const {
            return ((hash<int>()(i.x) ^ (hash<int>()(i.y) << 1)) >> 1) ^ (hash<int>()(i.z) << 1);
        }
    };
}

/**
 * @class VoxelMap
 * @brief Manages the dense 3D map of the world.
 * Internally uses a sparse voxel grid implemented with a hash map for memory efficiency.
 */
class VoxelMap {
public:
    VoxelMap(float voxel_size = 0.01f); // Voxel size in meters

    /**
     * @brief Fuses the point cloud from a new KeyFrame into the map.
     * This is the primary way the map is updated.
     * @param keyframe A shared pointer to the new KeyFrame to process.
     */
    void fuse(const KeyFrame::Ptr& keyframe);

    /**
     * @brief Returns a copy of all surfels in the map.
     * @return A vector of all surfels, for rendering or analysis.
     */
    std::vector<Surfel> getSurfels() const;

private:
    // The core map data structure: a hash map from a voxel's index to the Surfel it contains.
    std::unordered_map<VoxelIndex, Surfel> m_grid;
    float m_voxel_size;
    float m_inv_voxel_size; // Pre-calculated for efficiency

    // A mutex to protect the grid from simultaneous read/write access from different threads.
    mutable std::mutex m_map_mutex;
};