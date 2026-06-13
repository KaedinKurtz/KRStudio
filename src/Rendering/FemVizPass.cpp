#include "FemVizPass.hpp"
#include "RenderingSystem.hpp"
#include "MpmSystem.hpp"
#include "Shader.hpp"
#include "components.hpp"
#include "FemComponents.hpp"

#include <QOpenGLFunctions_4_3_Core>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

void FemVizPass::execute(const RenderFrameContext& context) {
    auto* gl = context.gl;
    MpmSystem* mpm = context.renderer.getMpmSystem();
    if (!gl || !mpm) return;
    const int mode = int(mpm->appearance().mode);
    if (mode == 0) return;  // Default -> bodies keep normal PBR

    Shader* shader = context.renderer.getShader("fem_viz");
    if (!shader) return;
    const glm::vec2 range = mpm->vizRange();

    shader->use(gl);
    shader->setMat4(gl, "u_view", context.view);
    shader->setMat4(gl, "u_projection", context.projection);
    shader->setFloat(gl, "u_rangeMin", range.x);
    shader->setFloat(gl, "u_rangeMax", range.y);

    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthFunc(GL_LEQUAL);       // re-draw the already-rasterised surface
    gl->glDepthMask(GL_TRUE);
    gl->glDisable(GL_BLEND);

    auto view = context.registry.view<krs::fem::FemResultComponent, TransformComponent>();
    for (auto e : view) {
        const auto& res = view.get<krs::fem::FemResultComponent>(e);
        if (!res.ready || !res.buffersBuilt || res.vao == 0 || res.indexCount == 0) continue;
        const auto& xf = view.get<TransformComponent>(e);
        glm::mat4 M = glm::translate(glm::mat4(1.0f), xf.translation);
        M *= glm::mat4_cast(xf.rotation);
        M = glm::scale(M, xf.scale);
        shader->setMat4(gl, "u_model", M);
        gl->glBindVertexArray(res.vao);
        gl->glDrawElements(GL_TRIANGLES, res.indexCount, GL_UNSIGNED_INT, nullptr);
    }
    gl->glBindVertexArray(0);
    gl->glDepthFunc(GL_LESS);         // restore default
}
