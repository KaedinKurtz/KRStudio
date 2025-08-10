#include "OpaquePass.hpp"
#include "RenderingSystem.hpp"
#include "Shader.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "RenderUtils.hpp"
#include "Texture2D.hpp"
#include "GizmoSystem.hpp"

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

void printMatrix(const QString& name, const glm::mat4& m) {
    qDebug().noquote() << name << Qt::endl
        << QString::asprintf("  [%8.3f, %8.3f, %8.3f, %8.3f]", m[0][0], m[1][0], m[2][0], m[3][0]) << Qt::endl
        << QString::asprintf("  [%8.3f, %8.3f, %8.3f, %8.3f]", m[0][1], m[1][1], m[2][1], m[3][1]) << Qt::endl
        << QString::asprintf("  [%8.3f, %8.3f, %8.3f, %8.3f]", m[0][2], m[1][2], m[2][2], m[3][2]) << Qt::endl
        << QString::asprintf("  [%8.3f, %8.3f, %8.3f, %8.3f]", m[0][3], m[1][3], m[2][3], m[3][3]);
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

    // --- OpenGL State Setup ---
    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthMask(GL_TRUE);
    gl->glDisable(GL_BLEND);
    gl->glDisable(GL_CULL_FACE);

    // --- Get all required shader programs once ---
    Shader* uvShader = context.renderer.getShader("gbuffer_textured");
    Shader* triplanarShader = context.renderer.getShader("gbuffer_triplanar");
    Shader* untexturedShader = context.renderer.getShader("gbuffer_untextured");
    Shader* tessShader = context.renderer.getShader("gbuffer_tessellated");
    Shader* tessTriplanarShader = context.renderer.getShader("gbuffer_tessellated_triplanar");
    Shader* pomShader = context.renderer.getShader("gbuffer_triplanar_pom");
    // --- Render all entities ---
    auto view = context.registry.view<
        RenderableMeshComponent,
        TransformComponent
    >(OpaquePass::DeferredExclusionTags{});

    for (auto ent : view)
    {
        if (context.registry.any_of<CameraGizmoTag>(ent))
            continue;

        const auto& meshComp = context.registry.get<RenderableMeshComponent>(ent);
        if (meshComp.indices.empty()) {
            continue;
        }

        const MaterialComponent* mat = context.registry.try_get<MaterialComponent>(ent);
        Shader* activeShader = nullptr;

        // ===================================================================
        // ## STEP 1: VERBOSE SHADER SELECTION LOGIC
        // ===================================================================
        qDebug().noquote() << "\n--- Selecting shader for entity" << (uint32_t)ent << "---";
        const bool isTessellated = context.registry.all_of<TessellatedMaterialTag>(ent);
        const bool isTriPlanar = context.registry.all_of<TriPlanarMaterialTag>(ent);
        const bool hasTexture = mat && mat->albedoMap;
        const bool isParallax = context.registry.all_of<ParallaxMaterialTag>(ent);
        qDebug().noquote() << "  [Check] Tags -> isTessellated:" << isTessellated
            << "| isTriPlanar:" << isTriPlanar << "| hasTexture:" << hasTexture;
        if (isParallax && isTriPlanar && hasTexture) {
            activeShader = pomShader;
        }
        else if (isTessellated && isTriPlanar && hasTexture) {
            qDebug() << "  [Path 1] Condition met. Selecting 'gbuffer_tessellated_triplanar'.";
            activeShader = tessTriplanarShader;
        }
        else if (isTessellated && hasTexture) {
            qDebug() << "  [Path 2] Condition met. Selecting 'gbuffer_tessellated'.";
            activeShader = tessShader;
        }
        else if (isTriPlanar && hasTexture) {
            qDebug() << "  [Path 3] Condition met. Selecting 'gbuffer_triplanar'.";
            activeShader = triplanarShader;
        }
        else if (hasTexture) {
            qDebug() << "  [Path 4] Condition met. Selecting 'gbuffer_textured'.";
            activeShader = uvShader;
        }
        else {
            qDebug() << "  [Path 5] Condition met. Selecting 'gbuffer_untextured'.";
            activeShader = untexturedShader;
        }

        if (!activeShader) {
            qWarning() << "  [!!!] FATAL: activeShader is nullptr. Skipping entity.";
            continue;
        }
        qDebug() << "  [OK] Final shader program ID:" << activeShader->ID;


        // ===================================================================
        // ## STEP 2 & 3: SET UNIFORMS AND DRAW
        // ===================================================================
        // (The rest of the function remains the same as the last correct version)

        activeShader->use(gl);
        auto& xf = context.registry.get<TransformComponent>(ent);
        activeShader->setMat4(gl, "view", context.view);
        activeShader->setMat4(gl, "projection", context.projection);
        activeShader->setMat4(gl, "model", xf.getTransform());

        if (activeShader == untexturedShader) {
            activeShader->setVec3(gl, "material.albedoColor", mat ? mat->albedoColor : glm::vec3(0.8f));
            activeShader->setFloat(gl, "material.metallic", mat ? mat->metallic : 0.0f);
            activeShader->setFloat(gl, "material.roughness", mat ? mat->roughness : 0.5f);
            activeShader->setVec3(gl, "material.emissiveColor", mat ? mat->emissiveColor : glm::vec3(0.0f));
        }
        else {
            unsigned int unit = 0;
            (mat && mat->albedoMap ? mat->albedoMap : context.renderer.getDefaultAlbedo())->bind(unit++);
            (mat && mat->normalMap ? mat->normalMap : context.renderer.getDefaultNormal())->bind(unit++);
            (mat && mat->aoMap ? mat->aoMap : context.renderer.getDefaultAO())->bind(unit++);
            (mat && mat->metallicMap ? mat->metallicMap : context.renderer.getDefaultMetallic())->bind(unit++);
            (mat && mat->roughnessMap ? mat->roughnessMap : context.renderer.getDefaultRoughness())->bind(unit++);
            (mat && mat->emissiveMap ? mat->emissiveMap : context.renderer.getDefaultEmissive())->bind(unit++);
            activeShader->setInt(gl, "material.albedoMap", 0);
            activeShader->setInt(gl, "material.normalMap", 1);
            activeShader->setInt(gl, "material.aoMap", 2);
            activeShader->setInt(gl, "material.metallicMap", 3);
            activeShader->setInt(gl, "material.roughnessMap", 4);
            activeShader->setInt(gl, "material.emissiveMap", 5);
            if (isTessellated) {
                activeShader->setVec3(gl, "viewPos", context.camera.getPosition());
                activeShader->setFloat(gl, "minTess", 0.01f);
                activeShader->setFloat(gl, "maxTess", 32.0f);
                activeShader->setFloat(gl, "maxDist", 25.0f);
                activeShader->setFloat(gl, "displacementScale", 0.1f);
				activeShader->setFloat(gl, "heightScale", 0.20f);
                if (mat && mat->heightMap) {
                    mat->heightMap->bind(unit);
                    activeShader->setInt(gl, "heightMap", unit);
                    unit++;
                }
            }
            if (activeShader == triplanarShader || activeShader == tessTriplanarShader) {
                activeShader->setFloat(gl, "u_texture_scale", 1.0f);
            }
            if (activeShader == pomShader) {
                activeShader->setFloat(gl, "u_texture_scale", 1.0f);
                activeShader->setFloat(gl, "u_height_scale", 0.05f);
            }
        }

        const auto& buf = context.renderer.getOrCreateMeshBuffers(gl, QOpenGLContext::currentContext(), ent);
        gl->glBindVertexArray(buf.VAO);
        if (isTessellated) {
            gl->glPatchParameteri(GL_PATCH_VERTICES, 3);
            gl->glDrawElements(GL_PATCHES, GLsizei(meshComp.indices.size()), GL_UNSIGNED_INT, nullptr);
        }
        else {
            gl->glDrawElements(GL_TRIANGLES, GLsizei(meshComp.indices.size()), GL_UNSIGNED_INT, nullptr);
        }
    }
    gl->glBindVertexArray(0);
}