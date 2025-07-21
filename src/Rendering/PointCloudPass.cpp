#include "PointCloudPass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "components.hpp" // For RenderFrameContext
#include "Scene.hpp"

#include <QOpenGLContext>
#include <QOpenGLFunctions_4_3_Core>
#include <QDebug>

PointCloudPass::PointCloudPass()
    : m_points(rs2::frame{})   // wrap an empty frame as a points object
    , m_color_frame(rs2::frame{})   // wrap an empty frame as a video_frame  :contentReference[oaicite:1]{index=1}
    , m_slam_model_matrix(1.0f)
    , m_has_new_data(false)
{
    // nothing else to do here
}

PointCloudPass::~PointCloudPass() {
    // This destructor will be called when the RenderingSystem is destroyed.
    // It's a good place to check for resource leaks.
    if (!m_vaos.isEmpty()) {
        qWarning() << "PointCloudPass destroyed, but VAO resources still exist. This indicates a resource leak.";
    }
}

void PointCloudPass::initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) {
    // This is called once when the RenderingSystem initializes its passes.
    // We just need to get a pointer to the shader we'll be using.
    m_shader = renderer.getShader("pointcloud");
    if (!m_shader) {
        qWarning() << "PointCloudPass: Failed to get 'pointcloud' shader from RenderingSystem.";
    }
}

void PointCloudPass::update(const rs2::points& points, const rs2::video_frame& colorFrame, const glm::mat4& pose) {
    // This method is called from outside the render loop (e.g., by the SlamManager).
    // It caches the latest data and sets a flag. No OpenGL calls happen here.
    if (!points || !colorFrame) return;

    m_points = points;
    m_color_frame = colorFrame;
    m_slam_model_matrix = pose;
    m_has_new_data = true;
}

void PointCloudPass::execute(const RenderFrameContext& context) {
    if (!m_shader || !m_points) {
        return; // Don't render if the shader isn't loaded or we have no data.
    }

    auto* gl = context.gl;
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    // --- Lazy Initialization of GL Resources ---
    // Following the pattern from GridPass, check if we've created buffers
    // for this specific OpenGL context yet. If not, create them now.
    if (!m_vaos.contains(ctx)) {
        createResourcesForContext(ctx, gl);
    }

    // --- Upload New Data if Available ---
    // If the update() method has been called, we have new data to send to the GPU.
    if (m_has_new_data) {
        // Bind the buffers for the current context
        gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo_vertices.value(ctx));
        gl->glBufferData(GL_ARRAY_BUFFER, m_points.size() * sizeof(float) * 3, m_points.get_vertices(), GL_DYNAMIC_DRAW);

        gl->glBindBuffer(GL_ARRAY_BUFFER, m_vbo_texcoords.value(ctx));
        gl->glBufferData(GL_ARRAY_BUFFER, m_points.size() * sizeof(float) * 2, m_points.get_texture_coordinates(), GL_DYNAMIC_DRAW);

        gl->glBindTexture(GL_TEXTURE_2D, m_texture_ids.value(ctx));
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_color_frame.get_width(), m_color_frame.get_height(), 0, GL_RGB, GL_UNSIGNED_BYTE, m_color_frame.get_data());

        m_has_new_data = false; // Clear the flag after uploading
    }

    // --- Render ---
    m_shader->use(gl);
    m_shader->setMat4(gl, "model", m_slam_model_matrix);
    m_shader->setMat4(gl, "view", context.view);
    m_shader->setMat4(gl, "projection", context.projection);

    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, m_texture_ids.value(ctx));
    m_shader->setInt(gl, "texture_color", 0);

    gl->glEnable(GL_PROGRAM_POINT_SIZE);
    gl->glDepthMask(GL_FALSE); // Render points without writing to depth buffer to avoid z-fighting

    gl->glBindVertexArray(m_vaos.value(ctx));
    gl->glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(m_points.size()));
    gl->glBindVertexArray(0);

    gl->glDepthMask(GL_TRUE);
    gl->glDisable(GL_PROGRAM_POINT_SIZE);
}

void PointCloudPass::onContextDestroyed(QOpenGLContext* dyingContext, QOpenGLFunctions_4_3_Core* gl) {
    // This is called by the RenderingSystem when a viewport is closed.
    // We must clean up the GPU resources associated with that specific context.
    if (m_vaos.contains(dyingContext)) {
        qDebug() << "PointCloudPass: Cleaning up resources for context" << dyingContext;

        GLuint vao_id = m_vaos.value(dyingContext);
        GLuint vbo_vert_id = m_vbo_vertices.value(dyingContext);
        GLuint vbo_tex_id = m_vbo_texcoords.value(dyingContext);
        GLuint tex_id = m_texture_ids.value(dyingContext);

        gl->glDeleteVertexArrays(1, &vao_id);
        gl->glDeleteBuffers(1, &vbo_vert_id);
        gl->glDeleteBuffers(1, &vbo_tex_id);
        gl->glDeleteTextures(1, &tex_id);

        m_vaos.remove(dyingContext);
        m_vbo_vertices.remove(dyingContext);
        m_vbo_texcoords.remove(dyingContext);
        m_texture_ids.remove(dyingContext);
    }
}

void PointCloudPass::createResourcesForContext(QOpenGLContext* ctx, QOpenGLFunctions_4_3_Core* gl) {
    qDebug() << "PointCloudPass: Creating new GL resources for context" << ctx;

    GLuint vao = 0, vbo_v = 0, vbo_t = 0, tex = 0;

    gl->glGenVertexArrays(1, &vao);
    gl->glGenBuffers(1, &vbo_v);
    gl->glGenBuffers(1, &vbo_t);
    gl->glGenTextures(1, &tex);

    // Configure VAO
    gl->glBindVertexArray(vao);

    // Vertex Positions (Attribute 0)
    gl->glBindBuffer(GL_ARRAY_BUFFER, vbo_v);
    gl->glEnableVertexAttribArray(0); // aPos
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3, (void*)0);

    // Texture Coordinates (Attribute 1)
    gl->glBindBuffer(GL_ARRAY_BUFFER, vbo_t);
    gl->glEnableVertexAttribArray(1); // aTexCoord
    gl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, (void*)0);

    gl->glBindVertexArray(0); // Unbind VAO

    // Configure Texture
    gl->glBindTexture(GL_TEXTURE_2D, tex);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glBindTexture(GL_TEXTURE_2D, 0); // Unbind texture

    // Store the newly created handles in our per-context maps.
    m_vaos[ctx] = vao;
    m_vbo_vertices[ctx] = vbo_v;
    m_vbo_texcoords[ctx] = vbo_t;
    m_texture_ids[ctx] = tex;
}