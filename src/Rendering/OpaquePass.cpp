#include "OpaquePass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "Camera.hpp"
#include "BlackBox.hpp"
#include "ViewportWidget.hpp" 
#include "RenderUtils.hpp"
#include "Texture2D.hpp"

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

    // Save/restore blend/depth/cull state
    GLStateSaver state(gl);
    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthMask(GL_TRUE);
    gl->glDisable(GL_BLEND);
    gl->glDisable(GL_CULL_FACE);

    // Get the two specialized shaders
    Shader* texturedShader = context.renderer.getShader("gbuffer_textured");
    Shader* untexturedShader = context.renderer.getShader("gbuffer_untextured");

    if (!texturedShader || !untexturedShader) {
        qWarning() << "[OpaquePass] Missing G-Buffer shaders!";
        return;
    }

    auto view = context.registry.view<RenderableMeshComponent, TransformComponent>();
    for (auto ent : view) {
        if (context.registry.any_of<CameraGizmoTag>(ent))
            continue;

        const MaterialComponent* mat = context.registry.try_get<MaterialComponent>(ent);

        // Decide which shader to use based on whether a material with an albedo map exists.
        Shader* activeShader = (mat && mat->albedoMap) ? texturedShader : untexturedShader;

        activeShader->use(gl);
        activeShader->setMat4(gl, "view", context.view);
        activeShader->setMat4(gl, "projection", context.projection);

        auto& xf = context.registry.get<TransformComponent>(ent);
        activeShader->setMat4(gl, "model", xf.getTransform());

        if (activeShader == texturedShader) {
            // --- TEXTURED PATH (CRASH-PROOF) ---
            unsigned int unit = 0;

            // For each texture, bind the material's map if it exists, otherwise bind the default.
            (mat->albedoMap ? mat->albedoMap : context.renderer.getDefaultAlbedo())->bind(unit++);
            (mat->normalMap ? mat->normalMap : context.renderer.getDefaultNormal())->bind(unit++);
            (mat->aoMap ? mat->aoMap : context.renderer.getDefaultAO())->bind(unit++);
            (mat->metallicMap ? mat->metallicMap : context.renderer.getDefaultMetallic())->bind(unit++);
            (mat->roughnessMap ? mat->roughnessMap : context.renderer.getDefaultRoughness())->bind(unit++);
            (mat->emissiveMap ? mat->emissiveMap : context.renderer.getDefaultEmissive())->bind(unit++);

            // Set the samplers to their corresponding texture units
            activeShader->setInt(gl, "material.albedoMap", 0);
            activeShader->setInt(gl, "material.normalMap", 1);
            activeShader->setInt(gl, "material.aoMap", 2);
            activeShader->setInt(gl, "material.metallicMap", 3);
            activeShader->setInt(gl, "material.roughnessMap", 4);
            activeShader->setInt(gl, "material.emissiveMap", 5);

        }
        else {
            // --- UNTEXTURED PATH ---
            activeShader->setVec3(gl, "material.albedoColor", mat ? mat->albedoColor : glm::vec3(0.8f));
            activeShader->setFloat(gl, "material.metallic", mat ? mat->metallic : 0.0f);
            activeShader->setFloat(gl, "material.roughness", mat ? mat->roughness : 0.5f);
            activeShader->setVec3(gl, "material.emissiveColor", mat ? mat->emissiveColor : glm::vec3(0.0f));
        }

        // --- DRAW CALL (applies to both paths) ---
        const auto& buf = context.renderer.getOrCreateMeshBuffers(gl, QOpenGLContext::currentContext(), ent);
        if (buf.VAO) {
            gl->glBindVertexArray(buf.VAO);
            gl->glDrawElements(
                GL_TRIANGLES,
                GLsizei(context.registry.get<RenderableMeshComponent>(ent).indices.size()),
                GL_UNSIGNED_INT,
                nullptr
            );
        }
    }
    gl->glBindVertexArray(0);
}