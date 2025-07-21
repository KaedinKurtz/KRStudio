#pragma once

#include <QObject>
#include "SlamData.hpp"
#include "VoxelMap.hpp"

/**
 * @class Backend
 * @brief Handles mapping and local optimization. (Worker Class)
 */
class Backend : public QObject {
    Q_OBJECT
public:
    explicit Backend(std::shared_ptr<VoxelMap> map, QObject* parent = nullptr);
    ~Backend();

public slots:
    void processNewKeyframe(KeyFrame::Ptr keyframe);

signals:
    void mapUpdated();

private:
    std::shared_ptr<VoxelMap> m_map;
};