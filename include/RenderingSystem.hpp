#pragma once

/*------------------------------------------------------------------
 *  RenderingSystem – public interface
 *
 *  Works with Qt 6.9, OpenGL 4.1 core profile.
 *-----------------------------------------------------------------*/

#include <memory>
#include <vector>
#include "Camera.hpp"
#include <glm/glm.hpp>
#include <entt/entt.hpp>

 /*  Qt / OpenGL --------------------------------------------------- */
#include <QOpenGLFunctions_4_1_Core>   // gives GLuint / GLenum, etc.
#include <QOpenGLWidget>               // we pass a pointer to one

/*  Forward declarations (keeps compile times low) ---------------- */
class Shader;
class FieldSolver;
struct ColorStop;

/*==================================================================
 *  Class
 *================================================================*/
class RenderingSystem
{
public:
    /**  CTX must be current on the calling thread when you first
     *   construct the system; usually done inside ViewportWidget.    */
    explicit RenderingSystem(
        QOpenGLWidget* viewport,
        QOpenGLFunctions_4_1_Core* gl = nullptr  /* optional override */
    );

    ~RenderingSystem();
    void setViewportWidget(QOpenGLWidget* w) { m_viewportWidget = w; }
    /* ------------------------------------------------------------ */
    /*  Life-cycle                                                 */
    /* ------------------------------------------------------------ */
    void initialize(int width, int height);   // once, after GL is ready
    void shutdown();                          // tear down all GL state
    void resize(int width, int height);       // call from resizeGL()
    void ensureSize(int w, int h);
    
    void resetGLState();

    void checkAndLogGlError(const char* label);

    /* ------------------------------------------------------------ */
    /*  Per-frame pipeline                                          */
    /* ------------------------------------------------------------ */
    void beginFrame(entt::registry& registry);
    void renderSceneToFBOs(entt::registry& registry, const Camera& primaryCamera); // Renders the scene ONCE to our off-screen textures
    void compositeToScreen(const Camera& viewportCamera, int viewportWidth, int viewportHeight); // Composites the result for a specific viewport
    void endFrame();

    /* ------------------------------------------------------------ */
    /*  Scene / camera helpers                                      */
    /* ------------------------------------------------------------ */
    void setCurrentCamera(entt::entity e) { m_currentCamera = e; }
    void updateCameraTransforms(entt::registry& r);
    static bool isDescendantOf(entt::registry&, entt::entity child, entt::entity potentialAncestor);

    /* ------------------------------------------------------------ */
    /*  Misc                                                        */
    /* ------------------------------------------------------------ */
    [[nodiscard]] bool isInitialized() const { return m_isInitialized; }
    void setOpenGLFunctions(QOpenGLFunctions_4_1_Core* gl) { m_gl = gl; }

private:
    /* ------------------------------------------------------------ */
    /*  Init helpers                                                */
    /* ------------------------------------------------------------ */
    void initShaders();
    void initFullscreenQuad();
    void initRenderPrimitives();
    void initFramebuffers(int width, int height);

    /* ------------------------------------------------------------ */
    /*  Private render sub-passes                                   */
    /* ------------------------------------------------------------ */
    void renderMeshes(entt::registry& registry,
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& camPos);
    void renderGrid(entt::registry& registry,
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& camPos);
    void renderSplines(entt::registry& registry,
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& eye,
        int viewportWidth,
        int viewportHeight);
    void renderFieldVisualizers(entt::registry& registry,
        const glm::mat4& view,
        const glm::mat4& projection);
    void renderSelectionGlow(entt::registry& registry,
        const glm::mat4& view,
        const glm::mat4& projection);
    void drawIntersections(const std::vector<std::vector<glm::vec3>>& allOutlines,
        const glm::mat4& view,
        const glm::mat4& proj);

    glm::mat4 m_sceneViewMatrix;
    glm::mat4 m_sceneProjectionMatrix;
    glm::vec3 m_sceneCameraPos;
    /* ==============================
     *  Data members
     * ============================== */

     /* --- injected externals --- */
    QOpenGLWidget* m_viewportWidget = nullptr;   ///< owner widget
    QOpenGLFunctions_4_1_Core* m_gl = nullptr;   ///< GL 4.1 functions
    entt::registry* m_registry = nullptr;   ///< set per-frame

    /* --- dimensions --- */
    int m_width = 0;
    int m_height = 0;

    /* --- core services --- */
    std::unique_ptr<FieldSolver> m_fieldSolver;
    entt::entity                 m_currentCamera{ entt::null };

    /* --- shaders --- */
    std::unique_ptr<Shader> m_phongShader;
    std::unique_ptr<Shader> m_gridShader;
    std::unique_ptr<Shader> m_splineShader;
    std::unique_ptr<Shader> m_lineShader;
    std::unique_ptr<Shader> m_glowShader;
    std::unique_ptr<Shader> m_capShader;
    std::unique_ptr<Shader> m_instancedArrowShader;
    std::unique_ptr<Shader> m_emissiveSolidShader;
    std::unique_ptr<Shader> m_blurShader;
    std::unique_ptr<Shader> m_compositeShader;
    std::unique_ptr<Shader> m_outlineShader;

    /* --- GPU resources --- */
    GLuint m_gridQuadVAO = 0, m_gridQuadVBO = 0;

    GLuint m_arrowVAO = 0, m_arrowVBO = 0, m_arrowEBO = 0, m_instanceVBO = 0;
    size_t m_arrowIndexCount = 0;

    GLuint m_lineVAO = 0, m_lineVBO = 0;
    GLuint m_capVAO = 0, m_capVBO = 0;

    GLuint m_intersectionVAO = 0, m_intersectionVBO = 0;

    /* --- framebuffers / textures --- */
    GLuint m_mainFBO = 0,
        m_mainColorTexture = 0,
        m_mainDepthRenderbuffer = 0;

    GLuint m_glowFBO = 0,
        m_glowTexture = 0;

    GLuint m_pingpongFBO[2]{ 0, 0 },
        m_pingpongTexture[2]{ 0, 0 };

    GLuint m_quadVAO = 0;     ///< fullscreen composite quad

    /* --- state flags --- */
    bool   m_isInitialized = false;

    struct TargetFBOs
    {
        int    w = 0, h = 0;
        GLuint mainFBO = 0,
            mainClrTex = 0,
            mainDepthRbo = 0,
            glowFBO = 0,
            glowTex = 0,
            pingFBO[2] = { 0,0 },
            pingTex[2] = { 0,0 },
            quadVAO = 0;      // local fullscreen quad
    };
    std::unordered_map<QOpenGLWidget*, TargetFBOs> m_targets;
};