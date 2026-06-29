#include "GhostRobotPass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "RobotModel.hpp"          // krs::robot::RobotRegistry / LiveRobot / GhostVizState

#include <glm/gtc/type_ptr.hpp>    // glm::make_mat4
#include <QOpenGLFunctions_4_3_Core>
#include <QOpenGLContext>
#include <QtGlobal>
#include <QString>
#include <Eigen/Dense>
#include <cstdio>

void GhostRobotPass::initialize(RenderingSystem&, QOpenGLFunctions_4_3_Core*)
{
    // The "ghost" shader is loaded centrally (RenderingSystem shader table) and fetched per-frame.
}

void GhostRobotPass::execute(const RenderFrameContext& context)
{
    using namespace krs::robot;
    auto& reg = context.registry;

    // MAIN-SCENE ONLY: the RobotRegistry lives in the main scene's ctx; the isolated Robot-View scene
    // has none, so this no-ops there (and never tries to draw main-scene entities in the wrong context).
    RobotRegistry* rr = reg.ctx().find<RobotRegistry>();
    if (!rr) return;

    // Enable toggle: ctx GhostVizState (UI/default) AND an env kill-switch (KRS_GHOST_OFF).
    static int envOff = -1;
    if (envOff < 0) envOff = qEnvironmentVariableIsSet("KRS_GHOST_OFF") ? 1 : 0;
    bool  enabled = true;
    float alpha   = 0.40f;
    if (auto* gv = reg.ctx().find<GhostVizState>()) { enabled = gv->enabled; alpha = gv->alpha; }
    if (envOff) enabled = false;
    if (!enabled) return;

    // Draw a robot ONLY when a joint is actually clamped -> invisible in normal operation.
    bool anyToDraw = false;
    for (auto& rp : rr->robots) if (rp && rp->anyJointInvalid()) { anyToDraw = true; break; }

    // KRS_GHOST_DBG=<file>: one-time file diagnostic (qInfo doesn't reach a redirected GUI stdout
    // reliably). Gated on elapsedTime so it captures a SETTLED frame, not the first. Off by default.
    if (qEnvironmentVariableIsSet("KRS_GHOST_DBG")) {
        static bool logged = false;
        if (!logged && context.elapsedTime > 5.0f) {
            logged = true;
            FILE* f = std::fopen(qEnvironmentVariable("KRS_GHOST_DBG").toStdString().c_str(), "w");
            if (f) {
                std::fprintf(f, "GhostRobotPass: robots=%d anyToDraw=%d\n", int(rr->robots.size()), anyToDraw ? 1 : 0);
                for (auto& rp : rr->robots) if (rp) {
                    std::fprintf(f, "  robot id=%d ndof=%d anyInvalid=%d jointValid=[",
                                 rp->robotId, rp->ndof(), rp->anyJointInvalid() ? 1 : 0);
                    for (char c : rp->jointValid) std::fprintf(f, "%d ", int(c));
                    std::fprintf(f, "]\n");
                }
                std::fclose(f);
            }
        }
    }

    if (!anyToDraw) return;

    Shader* shader = context.renderer.getShader("ghost");
    if (!shader) return;
    auto* gl = context.gl;

    // Save the GL state we touch (later overlay passes set their own, but be a good citizen).
    const GLboolean wasDepthTest = gl->glIsEnabled(GL_DEPTH_TEST);
    const GLboolean wasBlend     = gl->glIsEnabled(GL_BLEND);
    const GLboolean wasCull      = gl->glIsEnabled(GL_CULL_FACE);
    GLboolean depthMask = GL_TRUE; gl->glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    GLint depthFunc = GL_LESS;     gl->glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);

    // Forward translucent: depth-TEST against the real scene (occluded by nearer geometry) but do NOT
    // write depth; alpha blend; both sides (the ghost is a thin shell).
    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthFunc(GL_LEQUAL);
    gl->glDepthMask(GL_FALSE);
    gl->glEnable(GL_BLEND);
    gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl->glDisable(GL_CULL_FACE);

    shader->use(gl);
    shader->setMat4(gl, "view", context.view);
    shader->setMat4(gl, "projection", context.projection);
    shader->setVec3(gl, "uViewPos", context.camera.getPosition());

    const glm::vec3 kGreen(0.16f, 0.95f, 0.34f);   // reachable
    const glm::vec3 kRed  (1.00f, 0.20f, 0.13f);   // a joint hit its limit (this link & all downstream)

    for (auto& rp : rr->robots) {
        if (!rp || !rp->anyJointInvalid()) continue;
        LiveRobot& lr = *rp;
        const std::vector<krs::dyn::Pose> ghost = lr.fkGhostLinks();   // FK(qCommandRaw)
        const int n = lr.ndof();
        bool cumulativeBad = false;   // a link is RED once any joint up the chain has been clamped
        for (int k = 0; k < n; ++k) {
            if (k < int(lr.jointValid.size()) && !lr.jointValid[k]) cumulativeBad = true;
            if (k >= int(ghost.size()) || k >= int(lr.restLinkWorld.size()) ||
                k >= int(lr.linkEntities.size()) || k >= int(lr.linkEntityRestWorld.size()))
                continue;

            // Ghost link world pose + delta-from-rest (mirrors writeBackRobotViz, with the ghost FK).
            Eigen::Matrix4d gp = Eigen::Matrix4d::Identity();
            gp.block<3, 3>(0, 0) = ghost[k].R; gp.block<3, 1>(0, 3) = ghost[k].p;
            const Eigen::Matrix4d linkWorld = lr.model.basePlacement * gp;
            const Eigen::Matrix4d delta     = linkWorld * lr.restLinkWorld[k].inverse();

            const glm::vec3 tint = cumulativeBad ? kRed : kGreen;
            shader->setVec4(gl, "uColor", glm::vec4(tint, alpha));

            for (size_t ei = 0; ei < lr.linkEntities[k].size(); ++ei) {
                const entt::entity e = lr.linkEntities[k][ei];
                if (!reg.valid(e)) continue;
                const auto* mesh = reg.try_get<RenderableMeshComponent>(e);
                if (!mesh || mesh->indices.empty()) continue;

                const Eigen::Matrix4d rest = (ei < lr.linkEntityRestWorld[k].size())
                                                 ? lr.linkEntityRestWorld[k][ei]
                                                 : Eigen::Matrix4d::Identity();
                const Eigen::Matrix4f m = (delta * rest).cast<float>();
                shader->setMat4(gl, "model", glm::make_mat4(m.data()));

                const auto& buffers = context.renderer.getOrCreateMeshBuffers(
                    gl, QOpenGLContext::currentContext(), e);
                gl->glBindVertexArray(buffers.VAO);
                gl->glDrawElements(GL_TRIANGLES, GLsizei(mesh->indices.size()), GL_UNSIGNED_INT, nullptr);
            }
        }
    }
    gl->glBindVertexArray(0);

    // Restore.
    if (!wasBlend)      gl->glDisable(GL_BLEND);
    if (wasCull)        gl->glEnable(GL_CULL_FACE);
    gl->glDepthMask(depthMask);
    gl->glDepthFunc(GLenum(depthFunc));
    if (!wasDepthTest)  gl->glDisable(GL_DEPTH_TEST);
}
