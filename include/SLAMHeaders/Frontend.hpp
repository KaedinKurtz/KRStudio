#pragma once

#include <QObject>
#include "SlamData.hpp"
#include <librealsense2/rs.hpp>
#include <memory>
#include <glm/glm.hpp>

/**
 * @class Frontend
 * @brief Handles real-time tracking and KeyFrame creation. (Worker Class)
 */
class Frontend : public QObject {
    Q_OBJECT
public:
    explicit Frontend(QObject* parent = nullptr);
    ~Frontend();

public slots:
    void processNewFrame(double timestamp, std::shared_ptr<rs2::points> points, std::shared_ptr<rs2::video_frame> colorFrame);

signals:
    void keyframeCreated(KeyFrame::Ptr keyframe);
    void poseUpdatedForRender(const glm::mat4& pose, std::shared_ptr<rs2::points> points, std::shared_ptr<rs2::video_frame> colorFrame);
};