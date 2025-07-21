// In Backend.cpp

#include "Backend.hpp"
#include <iostream>
// A real implementation requires a non-linear optimization library.
// #include <g2o/core/block_solver.h>
// #include <g2o/core/optimization_algorithm_levenberg.h>
// ... etc.

Backend::Backend(std::shared_ptr<VoxelMap> map, QObject* parent) : QObject(parent), m_map(map) {}
Backend::~Backend() {}

void Backend::processNewKeyframe(KeyFrame::Ptr keyframe) {
    // This function receives new KeyFrames and performs mapping and optimization.

    // =================================================================================
    // STEP 1: FUSE KEYFRAME INTO DENSE MAP
    // =================================================================================
    // This step is already implemented. The VoxelMap takes care of the fusion logic.
    if (m_map) {
        m_map->fuse(keyframe);
        emit mapUpdated(); // Notify the UI that the map has changed
    }

    // =================================================================================
    // STEP 2: LOCAL BUNDLE ADJUSTMENT (Requires g2o or Ceres)
    // =================================================================================
    // This is a crucial optimization step to refine the local map area.
    // It adjusts the poses of a small window of recent, connected KeyFrames and the 3D
    // positions of the map points (Surfels) they observe to minimize reprojection error.

    // --- Pseudocode for Local BA ---
    // 1. Get a "local window" of this keyframe and its neighbors in the pose graph.
    //    std::vector<KeyFrame::Ptr> local_window = m_pose_graph->get_local_window(keyframe);

    // 2. Identify all map points (Surfels) observed by these keyframes.
    //    std::vector<Surfel*> local_map_points = m_map->get_points_seen_by(local_window);

    // 3. Build an optimization problem (a "factor graph").
    //    g2o::SparseOptimizer optimizer;
    //    // setup solver...
    //    
    //    // Add camera poses as vertices in the graph.
    //    for (auto& kf : local_window) { optimizer.add_vertex(new PoseVertex(kf)); }
    //
    //    // Add map points as vertices in the graph.
    //    for (auto& point : local_map_points) { optimizer.add_vertex(new PointVertex(point)); }
    //
    //    // Add projection measurements as edges between poses and points.
    //    for (auto& observation : all_observations) { optimizer.add_edge(new ProjectionEdge(...)); }

    // 4. Run the optimizer for a few iterations.
    //    optimizer.initializeOptimization();
    //    optimizer.optimize(10);

    // 5. Update the real poses and map points with the optimized results.
    //    m_pose_graph->update_poses(optimizer.results());
    //    m_map->update_points(optimizer.results());

    // NOTE: This is a highly non-trivial task that forms the core of a SLAM backend.
}