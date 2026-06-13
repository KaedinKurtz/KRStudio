#include "MpmPass.hpp"
#include "RenderingSystem.hpp"
#include "MpmSystem.hpp"
#include "Shader.hpp"

#include <QOpenGLFunctions_4_3_Core>

void MpmPass::execute(const RenderFrameContext& context)
{
    auto* gl = context.gl;
    MpmSystem* mpm = context.renderer.getMpmSystem();
    if (!gl || !mpm || !mpm->active()) return;

    Shader* shader = context.renderer.getShader("mpm_render");
    if (!shader) return;

    shader->use(gl);
    shader->setMat4(gl, "u_view", context.view);
    shader->setMat4(gl, "u_projection", context.projection);
    shader->setFloat(gl, "u_viewportHeight", float(context.viewportHeight));
    shader->setFloat(gl, "u_radius", mpm->particleRadius());

    // Phase 3 visualization: scalar field -> colour ramp on the splats.
    const glm::vec2 range = mpm->vizRange();
    shader->setInt(gl, "u_vizMode", int(mpm->appearance().mode));
    shader->setFloat(gl, "u_rangeMin", range.x);
    shader->setFloat(gl, "u_rangeMax", range.y);

    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, mpm->particleBuffer());

    gl->glEnable(GL_PROGRAM_POINT_SIZE);
    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthMask(GL_TRUE);
    gl->glDisable(GL_BLEND);

    static GLuint s_vao = 0;
    if (s_vao == 0) gl->glGenVertexArrays(1, &s_vao);
    gl->glBindVertexArray(s_vao);
    gl->glDrawArrays(GL_POINTS, 0, mpm->particleCount());
    gl->glBindVertexArray(0);

    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
    gl->glDisable(GL_PROGRAM_POINT_SIZE);
}
