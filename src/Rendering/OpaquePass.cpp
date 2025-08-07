#include "OpaquePass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "RenderUtils.hpp"
#include "Texture2D.hpp"

#include <QOpenGLFunctions_4_3_Core>
#include <QDebug>
#include <vector>

// ===================================================================
// ==                       DEBUGGING HELPERS                       ==
// ===================================================================

struct GLStateSaver {
    GLStateSaver(QOpenGLFunctions_4_3_Core* gl)
        : m_gl(gl)
    {
        m_gl->glGetBooleanv(GL_BLEND, &blend);
        m_gl->glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        m_gl->glGetBooleanv(GL_CULL_FACE, &cullFace);
    }
    ~GLStateSaver() {
        if (blend)    m_gl->glEnable(GL_BLEND);  else m_gl->glDisable(GL_BLEND);
        m_gl->glDepthMask(depthMask);
        if (cullFace) m_gl->glEnable(GL_CULL_FACE); else m_gl->glDisable(GL_CULL_FACE);
    }
private:
    QOpenGLFunctions_4_3_Core* m_gl;
    GLboolean blend, depthMask, cullFace;
};


static void debugSample2DTexture(QOpenGLFunctions_4_3_Core* gl,
    GLuint textureId,
    int width, int height,
    const QString& textureName)
{
    if (!gl || textureId == 0 || width <= 0 || height <= 0) {
        qWarning() << "  [TexSample] Invalid parameters for" << textureName;
        return;
    }
    std::vector<float> pixelData(4, 0.0f); // RGBA
    GLuint tempFBO;
    gl->glGenFramebuffers(1, &tempFBO);
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, tempFBO);
    gl->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureId, 0);
    if (gl->glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        gl->glReadPixels(width / 2, height / 2, 1, 1, GL_RGBA, GL_FLOAT, pixelData.data());
        qDebug().noquote() << QString("  [TexSample] Center pixel of [%1]: R=%2, G=%3, B=%4, A=%5")
            .arg(textureName, -18)
            .arg(pixelData[0], 0, 'f', 4)
            .arg(pixelData[1], 0, 'f', 4)
            .arg(pixelData[2], 0, 'f', 4)
            .arg(pixelData[3], 0, 'f', 4);
    }
    else {
        qWarning() << "  [TexSample] Could not read from" << textureName << "- FBO incomplete.";
    }
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    gl->glDeleteFramebuffers(1, &tempFBO);
}

static void dumpAllGLState(QOpenGLFunctions_4_3_Core* gl, const RenderableMeshComponent& mesh) {
    qDebug() << "    =======================================================";
    qDebug() << "    ===           ULTIMATE OPENGL STATE DUMP            ===";
    qDebug() << "    =======================================================";
    GLint program = 0;
    gl->glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    qDebug() << "[Shader] Program ID:" << program;
    GLint fbo = 0;
    gl->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
    qDebug() << "[Framebuffer] Bound FBO:" << fbo;
    if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        qWarning() << "  [!!!] FBO IS INCOMPLETE!";
    }
    GLint vao = 0;
    gl->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    qDebug() << "[VAO] Bound VAO:" << vao;
    if (vao == 0) {
        qWarning() << "  [!!!] CRITICAL: NO VAO IS BOUND!";
    }
    GLint vbo = 0, ebo = 0;
    gl->glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &vbo);
    gl->glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &ebo);
    qDebug() << "[Buffers] Bound VBO:" << vbo << "| Bound EBO:" << ebo;
    if (vbo == 0 || ebo == 0) {
        qWarning() << "  [!!!] CRITICAL: VBO or EBO is not bound! This is often managed by the VAO.";
    }
    else {
        GLint eboSize = 0;
        gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        gl->glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &eboSize);
        qDebug() << "  - EBO Size on GPU:" << eboSize << "bytes";
    }
    qDebug() << "[Vertex Attributes] Checking status for 5 attributes...";
    for (int i = 0; i < 5; ++i) {
        GLint enabled = 0;
        gl->glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
        if (enabled) {
            GLint size = 0, type = 0, stride = 0;
            GLvoid* pointer = nullptr;
            gl->glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &size);
            gl->glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &type);
            gl->glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &stride);
            gl->glGetVertexAttribPointerv(i, GL_VERTEX_ATTRIB_ARRAY_POINTER, &pointer);
            qDebug().nospace() << "  - Attrib " << i << ": ENABLED | Size: " << size
                << " | Type: " << type << " (GL_FLOAT=" << GL_FLOAT << ")"
                << " | Stride: " << stride << " (Expected: " << sizeof(Vertex) << ")"
                << " | Offset: " << reinterpret_cast<uintptr_t>(pointer);
        }
        else {
            qWarning() << "  - Attrib " << i << ": DISABLED!";
        }
    }
    qDebug() << "[Draw Call] Preparing glDrawElements...";
    qDebug() << "  - Index Count:" << mesh.indices.size();
    qDebug() << "  - Index Type: GL_UNSIGNED_INT (" << GL_UNSIGNED_INT << ")";
    qDebug() << "    =======================================================";
}


// ===================================================================
// ==                       OPAQUE PASS CLASS                       ==
// ===================================================================

void OpaquePass::initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) {
    // This function is currently empty but is here for future initialization logic.
}

void OpaquePass::execute(const RenderFrameContext& context)
{
    auto* gl = context.gl;
    if (!gl) return;

    GLStateSaver state(gl);
    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthMask(GL_TRUE);
    gl->glDisable(GL_BLEND);
    gl->glDisable(GL_CULL_FACE);

    Shader* uvShader = context.renderer.getShader("gbuffer_textured");
    Shader* triplanarShader = context.renderer.getShader("gbuffer_triplanar");
    Shader* untexturedShader = context.renderer.getShader("gbuffer_untextured");

    if (!uvShader || !triplanarShader || !untexturedShader) {
        qWarning() << "[OpaquePass] Missing one or more G-Buffer shaders!";
        return;
    }

    if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        qWarning() << "[OpaquePass] G-BUFFER FBO IS NOT COMPLETE! Aborting pass.";
        return;
    }

    auto view = context.registry.view<RenderableMeshComponent, TransformComponent>();
    for (auto ent : view) {
        if (context.registry.any_of<CameraGizmoTag>(ent))
            continue;

        const auto& meshComp = context.registry.get<RenderableMeshComponent>(ent);
        if (meshComp.indices.empty()) {
            continue;
        }

        const MaterialComponent* mat = context.registry.try_get<MaterialComponent>(ent);
        Shader* activeShader = nullptr;

        // --- 4a. Automatic Shader Selection (LOGIC CORRECTED) ---
        bool isTriPlanar = context.registry.all_of<TriPlanarMaterialTag>(ent);
        bool isUV = context.registry.all_of<UVTexturedMaterialTag>(ent);
        bool hasTexture = mat && mat->albedoMap;

        if (isUV && hasTexture) {
            activeShader = uvShader;
        }
        else if (isTriPlanar && hasTexture) {
            activeShader = triplanarShader;
        }
        else {
            activeShader = untexturedShader;
        }

        activeShader->use(gl);
        activeShader->setMat4(gl, "view", context.view);
        activeShader->setMat4(gl, "projection", context.projection);
        auto& xf = context.registry.get<TransformComponent>(ent);
        activeShader->setMat4(gl, "model", xf.getTransform());

        // --- 4c. Set Material Uniforms ---
        if (activeShader == uvShader || activeShader == triplanarShader) {
            unsigned int unit = 0;
            auto albedoTex = mat->albedoMap ? mat->albedoMap : context.renderer.getDefaultAlbedo();
            albedoTex->bind(unit++);

            if (activeShader == triplanarShader) {
                const auto* tag = context.registry.try_get<TagComponent>(ent);
                qDebug() << "\n[OpaquePass] --- PRE-DRAW STATE for" << (tag ? tag->tag.c_str() : "Untitled") << "---";
                debugSample2DTexture(gl, albedoTex->getID(), albedoTex->getWidth(), albedoTex->getHeight(), "Input Albedo Tex");
            }

            (mat->normalMap ? mat->normalMap : context.renderer.getDefaultNormal())->bind(unit++);
            (mat->aoMap ? mat->aoMap : context.renderer.getDefaultAO())->bind(unit++);
            (mat->metallicMap ? mat->metallicMap : context.renderer.getDefaultMetallic())->bind(unit++);
            (mat->roughnessMap ? mat->roughnessMap : context.renderer.getDefaultRoughness())->bind(unit++);
            (mat->emissiveMap ? mat->emissiveMap : context.renderer.getDefaultEmissive())->bind(unit++);

            activeShader->setInt(gl, "material.albedoMap", 0);
            activeShader->setInt(gl, "material.normalMap", 1);
            activeShader->setInt(gl, "material.aoMap", 2);
            activeShader->setInt(gl, "material.metallicMap", 3);
            activeShader->setInt(gl, "material.roughnessMap", 4);
            activeShader->setInt(gl, "material.emissiveMap", 5);

            if (activeShader == triplanarShader) {
                activeShader->setFloat(gl, "u_texture_scale", 1.0f);
            }
        }
        else {
            activeShader->setVec3(gl, "material.albedoColor", mat ? mat->albedoColor : glm::vec3(0.8f));
            activeShader->setFloat(gl, "material.metallic", mat ? mat->metallic : 0.0f);
            activeShader->setFloat(gl, "material.roughness", mat ? mat->roughness : 0.5f);
            activeShader->setVec3(gl, "material.emissiveColor", mat ? mat->emissiveColor : glm::vec3(0.0f));
        }

        // --- 4d. Get Buffers and Draw ---
        const auto& buf = context.renderer.getOrCreateMeshBuffers(gl, QOpenGLContext::currentContext(), ent);
        if (buf.VAO) {
            gl->glBindVertexArray(buf.VAO);

            if (activeShader == triplanarShader) {
                dumpAllGLState(gl, meshComp);
            }

            gl->glDrawElements(
                GL_TRIANGLES,
                GLsizei(meshComp.indices.size()),
                GL_UNSIGNED_INT,
                nullptr
            );
        }

        if (activeShader == triplanarShader) {
            debugSample2DTexture(gl, context.renderer.getGBuffer().albedoAOTexture,
                context.renderer.getGBuffer().w, context.renderer.getGBuffer().h,
                "G-Buffer Albedo");
            qDebug() << "[OpaquePass] --- POST-DRAW STATE ---";
        }
    }
    gl->glBindVertexArray(0);

    qDebug() << "\n[OpaquePass] --- FINAL G-BUFFER STATE ---";
    debugSample2DTexture(gl, context.renderer.getGBuffer().albedoAOTexture,
        context.renderer.getGBuffer().w, context.renderer.getGBuffer().h,
        "Final G-Buffer");
}
