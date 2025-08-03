#include "OpaquePass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "Camera.hpp"
#include "BlackBox.hpp"
#include "ViewportWidget.hpp" 
#include "RenderUtils.hpp"

#include <QOpenGLContext>
#include <QOpenGLFunctions_4_3_Core>
#include <QOpenGLExtraFunctions>
#include <QDebug>

struct GLStateSaver {
    GLStateSaver(QOpenGLFunctions_4_3_Core* gl)
        : m_gl(gl)
    {
        m_gl->glGetBooleanv(GL_BLEND, &blend);
        m_gl->glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        m_gl->glGetBooleanv(GL_CULL_FACE, &cullFace);
    }
    ~GLStateSaver() {
        if (blend)      m_gl->glEnable(GL_BLEND);  else m_gl->glDisable(GL_BLEND);
        m_gl->glDepthMask(depthMask);
        if (cullFace)   m_gl->glEnable(GL_CULL_FACE); else m_gl->glDisable(GL_CULL_FACE);
    }
private:
    QOpenGLFunctions_4_3_Core* m_gl;
    GLboolean blend, depthMask, cullFace;
};


void OpaquePass::initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) {

}

void OpaquePass::execute(const RenderFrameContext& context)
{
    auto* gl = context.gl;
    if (!gl) return;

    // DEBUG: What FBO are we bound to?
    GLint curFBO = 0;
    gl->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &curFBO);
    qDebug() << "[OpaquePass] Bound FBO =" << curFBO;

    // DEBUG: Are draw?buffers still correct?
    for (int i = 0; i < 3; ++i) {
        GLint db = 0;
        gl->glGetIntegerv(GL_DRAW_BUFFER0 + i, &db);
        qDebug() << "[OpaquePass] DRAW_BUFFER[" << i << "] =" << db;
    }

    // Manually establish the correct GL state for this pass.
    gl->glEnable(GL_DEPTH_TEST);                                  // Enable depth testing for 3D occlusion.
    gl->glDepthMask(GL_TRUE);                                     // Allow writing to the depth buffer.
    //gl->glEnable(GL_CULL_FACE);                                   // Enable the face culling optimization.
    gl->glDisable(GL_BLEND);                                      // Opaque objects are not blended.
    gl->glDisable(GL_CULL_FACE);
    // --- THE FIX ---
    // These two lines are the key to fixing the disappearing faces.
    gl->glFrontFace(GL_CW);                                      // Define a "front face" as having counter-clockwise vertices. This is the standard.
    gl->glCullFace(GL_BACK);                                      // Tell OpenGL to cull (not draw) the back faces of the mesh.
    // Clearing is now handled in the RenderingSystem's geometryPass,
    // so we remove the glClear calls from here.

    // Set up G-Buffer shader
    Shader* gbufShader = context.renderer.getShader("gbuffer");
    if (!gbufShader) {
        qWarning() << "[OpaquePass] Missing gbuffer shader!";
        return;
    }
    gbufShader->use(gl);
    gbufShader->setMat4(gl, "view", context.view);
    gbufShader->setMat4(gl, "projection", context.projection);

    Shader* shader = context.renderer.getShader("gbuffer");
    shader->use(gl);


    GLint locAlbedo = gl->glGetUniformLocation(shader->id(), "material.albedo");
    GLint locUseMap = gl->glGetUniformLocation(shader->id(), "material.useAlbedoMap");
    qDebug() << "[OpaquePass] uniform 'material.albedo' location =" << locAlbedo;
    qDebug() << "[OpaquePass] uniform 'material.useAlbedoMap' location =" << locUseMap;

    // --- The rest of your function remains the same ---
    // Draw every opaque mesh...
    auto view = context.registry.view<RenderableMeshComponent, TransformComponent>();
    for (auto ent : view) {
        if (context.registry.any_of<CameraGizmoTag>(ent))
            continue;

        auto& meshComp = view.get<RenderableMeshComponent>(ent);
        auto& xfComp = view.get<TransformComponent>(ent);

        if (auto* mat = context.registry.try_get<MaterialComponent>(ent)) {
            uint32_t entId = static_cast<uint32_t>(ent);
            qDebug() << "[OpaquePass] entity" << entId
                << "albedo =" << mat->albedo.x
                << mat->albedo.y
                << mat->albedo.z
                << "useMap =" << (mat->albedoMap ? 1 : 0);
        }
        else {
            uint32_t entId = static_cast<uint32_t>(ent);
            qDebug() << "[OpaquePass] entity" << entId
                << "using fallback albedo";
        }

        // fetch or create VAO/VBO/EBO
        const auto& buf = context.renderer.getOrCreateMeshBuffers(
            gl,
            QOpenGLContext::currentContext(),
            ent
        );
        if (buf.VAO == 0) continue;

        // material setup
        if (auto* mat = context.registry.try_get<MaterialComponent>(ent)) {
            gbufShader->setVec3(gl, "objectColor", mat->albedo);
            gbufShader->setFloat(gl, "material.metallic", mat->metallic);
            gbufShader->setFloat(gl, "material.roughness", mat->roughness);

            if (mat->albedoMap && mat->albedoMap->id) {
                gl->glActiveTexture(GL_TEXTURE0);
                gl->glBindTexture(GL_TEXTURE_2D, mat->albedoMap->id);
                gbufShader->setInt(gl, "material.albedoMap", 0);
                gbufShader->setInt(gl, "material.useAlbedoMap", 1);
            }
            else {
                gbufShader->setInt(gl, "material.useAlbedoMap", 0);
            }
        }
        else {
            // fallback
            gbufShader->setVec3(gl, "objectColor", glm::vec3(0.8f));
            gbufShader->setFloat(gl, "material.metallic", 0.1f);
            gbufShader->setFloat(gl, "material.roughness", 0.8f);
            gbufShader->setInt(gl, "material.useAlbedoMap", 0);
        }

        // model matrix
        gbufShader->setMat4(gl, "model", xfComp.getTransform());

        // draw call
        gl->glBindVertexArray(buf.VAO);
        gl->glDrawElements(
            GL_TRIANGLES,
            GLsizei(meshComp.indices.size()),
            GL_UNSIGNED_INT,
            nullptr
        );
    }

    // unbind VAO
    gl->glBindVertexArray(0);
}