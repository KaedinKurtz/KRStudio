// In Frontend.cpp

#include "Frontend.hpp"
#include <iostream>
// A real implementation requires computer vision and math libraries.
// #include <opencv2/features2d.hpp> 

Frontend::Frontend(QObject* parent) : QObject(parent) {}
Frontend::~Frontend() {}

void Frontend::processNewFrame(double timestamp, std::shared_ptr<rs2::points> points, std::shared_ptr<rs2::video_frame> colorFrame) {
    // This function implements the Visual-Inertial Odometry (VIO) pipeline.

    // =================================================================================
    // STEP 1: FEATURE EXTRACTION (Requires OpenCV)
    // =================================================================================
    // Extract sparse features like ORB from the color image. These are used for tracking.
    // auto image = cv::Mat(cv::Size(colorFrame->get_width(), colorFrame->get_height()), ...);
    // std::vector<cv::KeyPoint> keypoints;
    // cv::Mat descriptors;
    // m_orb_detector->detectAndCompute(image, cv::noArray(), keypoints, descriptors);

    // =================================================================================
    // STEP 2: POSE TRACKING
    // =================================================================================
    // Match features against the previous KeyFrame to estimate motion. If tracking is lost,
    // initiate a relocalization procedure.
    // If IMU data is available, it would be used here to provide a robust prediction
    // of the current pose, making tracking much more reliable.
    // Eigen::Isometry3f new_pose = m_tracker->track(last_pose, keypoints, descriptors);

    // =================================================================================
    // STEP 3: KEYFRAME SELECTION
    // =================================================================================
    // Decide if this frame is "important" enough to be a new KeyFrame. This prevents
    // redundant data and is critical for real-time performance.
    // bool is_keyframe = m_keyframe_selector->check(new_pose, last_keyframe_pose);

    // if (is_keyframe) {
        // --- If it's a KeyFrame, create and populate the data structure ---
    auto kf = std::make_shared<KeyFrame>();
    // kf->id = m_next_kf_id++;
    // kf->timestamp = timestamp;
    // kf->pose = new_pose;

    // --- Populate the point cloud data for the mapping thread ---
    // This is where you would also copy the texture coordinates.
    // const rs2::vertex* vertices = points->get_vertices();
    // const rs2::texture_coordinate* texs = points->get_texture_coordinates();
    // for (size_t i = 0; i < points->size(); ++i) {
    //     if(vertices[i].z > 0) { // Add only valid points
    //         kf->point_cloud.emplace_back(vertices[i].x, vertices[i].y, vertices[i].z);
    //         kf->texture_coordinates.emplace_back(texs[i].u, texs[i].v);
    //     }
    // }
    // kf->color_data = ...; // Copy color data
    // kf->color_width = colorFrame->get_width();
    // kf->color_height = colorFrame->get_height();
    // kf->color_bpp = colorFrame->get_bytes_per_pixel();

    // --- Emit the new KeyFrame for the backend to process ---
    // std::cout << "Frontend: Created KeyFrame " << kf->id << std::endl;
    // emit keyframeCreated(kf);
// }
}

