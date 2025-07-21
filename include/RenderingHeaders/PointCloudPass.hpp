#pragma once

#include "IRenderPass.hpp"
#include <librealsense2/rs.hpp>
#include <glm/glm.hpp>
#include <QHash>

// Forward declarations
class Shader;
class QOpenGLContext;

class PointCloudPass : public IRenderPass {
public:
    PointCloudPass();
    ~PointCloudPass() override;

    // --- Overridden from IRenderPass ---
    void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) override;
    void execute(const RenderFrameContext& context) override;
    void onContextDestroyed(QOpenGLContext* dyingContext, QOpenGLFunctions_4_3_Core* gl) override;

    /**
     * @brief Updates the point cloud data and its world pose for rendering.
     * @param points The latest point cloud (contains vertices and texture coords).
     * @param colorFrame The corresponding color frame to use as a texture.
     * @param pose The world-space pose of the physical camera as a glm::mat4 model matrix.
     */
    void update(const rs2::points& points, const rs2::video_frame& colorFrame, const glm::mat4& pose);

private:
    // Helper to create GL resources for a new context.
    void createResourcesForContext(QOpenGLContext* ctx, QOpenGLFunctions_4_3_Core* gl);

    Shader* m_shader = nullptr;

    // --- Per-Context OpenGL Resources ---
    // This mirrors the architecture used in your other passes.
    QHash<QOpenGLContext*, GLuint> m_vaos;
    QHash<QOpenGLContext*, GLuint> m_vbo_vertices;
    QHash<QOpenGLContext*, GLuint> m_vbo_texcoords;
    QHash<QOpenGLContext*, GLuint> m_texture_ids;

    // --- Data to be Rendered ---
    // This data is cached here and uploaded to the GPU for the relevant context
    // during the execute() call.
    rs2::points m_points;
    rs2::video_frame m_color_frame;
    glm::mat4 m_slam_model_matrix = glm::mat4(1.0f); // The model matrix for the point cloud
    bool m_has_new_data = false;
};