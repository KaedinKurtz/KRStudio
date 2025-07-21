#pragma once

#include <QObject>
#include <QThread>
#include "SlamData.hpp"
#include <librealsense2/rs.hpp>
#include <glm/glm.hpp> // ADDED: For passing the pose matrix

// Forward-declare the worker classes to keep this header clean.
class Frontend;
class Backend;
class VoxelMap;
class RenderingSystem;

class SlamManager : public QObject {
    Q_OBJECT

public:
    explicit SlamManager(QObject* parent = nullptr);
    ~SlamManager();

    void start();
    void stop();
    void setRenderingSystem(RenderingSystem* renderer);

public slots:
    void onPointCloudReady(const rs2::points& points, const rs2::video_frame& colorFrame);

    // ADDED: Slot to receive the complete data package from the Frontend
    void onPoseUpdatedForRender(const glm::mat4& pose, std::shared_ptr<rs2::points> points, std::shared_ptr<rs2::video_frame> colorFrame);


signals:
    void newFrameData(double timestamp, std::shared_ptr<rs2::points> points, std::shared_ptr<rs2::video_frame> colorFrame);
    void mapUpdated();

private:
    // ... (member variables are unchanged)
    QThread m_frontend_thread;
    QThread m_backend_thread;
    Frontend* m_frontend = nullptr;
    Backend* m_backend = nullptr;
    std::shared_ptr<VoxelMap> m_voxel_map;
    RenderingSystem* m_rendering_system = nullptr;
};