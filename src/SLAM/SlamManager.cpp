#include "SlamManager.hpp"
#include "VoxelMap.hpp"
#include "Frontend.hpp"
#include "Backend.hpp"
#include "RenderingSystem.hpp"

#include <memory>

SlamManager::SlamManager(QObject* parent) : QObject(parent) {
    // 1. Create the shared map resource.
    m_voxel_map = std::make_shared<VoxelMap>();

    // 2. Create the worker objects.
    m_frontend = new Frontend();
    m_backend = new Backend(m_voxel_map);

    // 3. Move workers to their respective threads.
    m_frontend->moveToThread(&m_frontend_thread);
    m_backend->moveToThread(&m_backend_thread);

    // 4. Connect signals and slots to create the data pipeline.
    connect(this, &SlamManager::newFrameData, m_frontend, &Frontend::processNewFrame);
    connect(m_frontend, &Frontend::keyframeCreated, m_backend, &Backend::processNewKeyframe);
    connect(m_backend, &Backend::mapUpdated, this, &SlamManager::mapUpdated);

    // ADDED: Connect the Frontend's new signal to the manager's new slot.
    // This creates the new visualization pipeline.
    connect(m_frontend, &Frontend::poseUpdatedForRender, this, &SlamManager::onPoseUpdatedForRender);

    // 5. Connect thread finished signals for proper cleanup.
    connect(&m_frontend_thread, &QThread::finished, m_frontend, &QObject::deleteLater);
    connect(&m_backend_thread, &QThread::finished, m_backend, &QObject::deleteLater);

    m_frontend_thread.setObjectName("FrontendThread");
    m_backend_thread.setObjectName("BackendThread");
}

SlamManager::~SlamManager() {
    stop();
}

void SlamManager::start() {
    m_frontend_thread.start();
    m_backend_thread.start();
}

void SlamManager::stop() {
    if (m_frontend_thread.isRunning()) {
        m_frontend_thread.requestInterruption();
        m_frontend_thread.quit();
        m_frontend_thread.wait();
    }
    if (m_backend_thread.isRunning()) {
        m_backend_thread.requestInterruption();
        m_backend_thread.quit();
        m_backend_thread.wait();
    }
}

void SlamManager::onPointCloudReady(const rs2::points& points, const rs2::video_frame& colorFrame) {
    // REMOVED: The incorrect call to the rendering system is gone from here.
    // if (m_rendering_system && points) {
    //     m_rendering_system->updatePointCloud(points); // This was incorrect.
    // }

    // This function's ONLY job now is to forward raw data to the SLAM backend.
    auto points_ptr = std::make_shared<rs2::points>(points);
    auto frame_ptr = std::make_shared<rs2::video_frame>(colorFrame);
    emit newFrameData(points.get_timestamp(), points_ptr, frame_ptr);
}

// ADDED: Implementation for the new slot.
void SlamManager::onPoseUpdatedForRender(const glm::mat4& pose, std::shared_ptr<rs2::points> points, std::shared_ptr<rs2::video_frame> colorFrame) {
    // This slot receives the complete data package from the Frontend thread.
    // It is now safe to call the renderer.
    if (m_rendering_system && points && colorFrame) {
        // Call the public update method with all three required pieces of data.
        m_rendering_system->updatePointCloud(*points, *colorFrame, pose);
    }
}


void SlamManager::setRenderingSystem(RenderingSystem* renderer) {
    m_rendering_system = renderer;
}