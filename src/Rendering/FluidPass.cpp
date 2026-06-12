#include "FluidPass.hpp"
#include "RenderingSystem.hpp"
#include "FluidSystem.hpp"
#include "Shader.hpp"

#include <QOpenGLFunctions_4_3_Core>

void FluidPass::execute(const RenderFrameContext& context)
{
    auto* gl = context.gl;
    FluidSystem* fluid = context.renderer.getFluidSystem();
    if (!gl || !fluid || fluid->particleCount() == 0) return;

    Shader* shader = context.renderer.getShader("fluid_render");
    if (!shader) return;

    shader->use(gl);
    shader->setMat4(gl, "u_view", context.view);
    shader->setMat4(gl, "u_projection", context.projection);
    shader->setFloat(gl, "u_particleRadius", fluid->particleRadius());
    shader->setFloat(gl, "u_viewportHeight", float(context.viewportHeight));

    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, fluid->particleBuffer());

    gl->glEnable(GL_PROGRAM_POINT_SIZE);
    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthMask(GL_TRUE);
    gl->glDisable(GL_BLEND);

    // Attributeless draw: the vertex shader indexes the SSBO by gl_VertexID.
    // An empty VAO is still required by core profile.
    static GLuint s_emptyVao = 0;
    if (s_emptyVao == 0) gl->glGenVertexArrays(1, &s_emptyVao);
    gl->glBindVertexArray(s_emptyVao);
    gl->glDrawArrays(GL_POINTS, 0, fluid->particleCount());
    gl->glBindVertexArray(0);

    gl->glDisable(GL_PROGRAM_POINT_SIZE);
}
