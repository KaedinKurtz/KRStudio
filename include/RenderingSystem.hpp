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
#include "GpuResources.hpp"
 /*  Qt / OpenGL --------------------------------------------------- */
#include <QOpenGLFunctions_4_3_Core>   // gives GLuint / GLenum, etc.
#include <QOpenGLWidget>               // we pass a pointer to one
#include <QTimer>

/*  Forward declarations (keeps compile times low) ---------------- */
class Shader;
class FieldSolver;
struct ColorStop;
class QOpenGLContext;

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
        QOpenGLFunctions_4_3_Core* gl = nullptr,  /* optional override */
        QObject* parent = nullptr
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
    
    struct PerContextData {
        GLuint quadVAO = 0;
    };
    QHash<QOpenGLContext*, PerContextData> m_ctxData;
    QOpenGLContext* currentCtxOrNull();

    QOpenGLContext* m_ownerCtx = nullptr;

    void renderView(QOpenGLWidget* viewport, entt::registry& registry, entt::entity cameraEntity, int viewportWidth, int viewportHeight);

    void shutdown(entt::registry& registry);
    void ensureGlResolved();
    void resetGLState();
    void dumpRenderTargets() const;
    void checkAndLogGlError(const char* label);
    QOpenGLWidget* getViewportWidget() const { return m_viewportWidget; }
    /* ------------------------------------------------------------ */
    /*  Per-frame pipeline                                          */
    /* ------------------------------------------------------------ */
    void beginFrame(entt::registry& registry);
    void renderSceneToFBOs(entt::registry& registry, const Camera& primaryCamera); // Renders the scene ONCE to our off-screen textures
    void compositeToScreen(int viewportWidth, int viewportHeight);
    void updateSceneLogic(entt::registry& registry, float deltaTime); // Add deltaTime here
    /* ------------------------------------------------------------ */
    /*  Scene / camera helpers                                      */
    /* ------------------------------------------------------------ */
    void setCurrentCamera(entt::entity e) { m_currentCamera = e; }
    void updateCameraTransforms(entt::registry& r);
    void updateAnimations(entt::registry& registry, float frameDt);
    static bool isDescendantOf(entt::registry&, entt::entity child, entt::entity potentialAncestor);
    bool isRenderCtx(const QOpenGLContext* ctx) const noexcept;
    /* ------------------------------------------------------------ */
    /*  Misc                                                        */
    /* ------------------------------------------------------------ */
    [[nodiscard]] bool isInitialized() const { return m_isInitialized; }
    void setOpenGLFunctions(QOpenGLFunctions_4_3_Core* funcs)
    {
        m_gl = funcs; // Set the internal pointer to the one provided by the active context.
    }
    
    struct TargetFBOs
    {
        int    w = 0, h = 0;
        GLuint mainFBO = 0,
            mainColorTexture = 0,
            mainDepthTexture = 0,
            glowFBO = 0,
            glowTexture = 0,
            pingpongFBO[2] = { 0,0 },
            pingpongTexture[2] = { 0,0 };
    };




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
        const glm::mat4& projection,
        float deltaTime);
    void renderSelectionGlow(QOpenGLWidget* viewport, entt::registry& registry,
        const glm::mat4& view, 
        const glm::mat4& projection, 
        TargetFBOs& target);
    void drawIntersections(const std::vector<std::vector<glm::vec3>>& allOutlines,
        const glm::mat4& view,
        const glm::mat4& proj);

private slots:
    void onContextDestroyed(QObject* context);


private:
    /* ------------------------------------------------------------ */
    /*  Init helpers                                                */
    /* ------------------------------------------------------------ */
    void initShaders();
    void initFramebuffers(int width, int height);
    
    /* ------------------------------------------------------------ */
    /*  Private render sub-passes                                   */
    /* ------------------------------------------------------------ */
    
    glm::mat4 m_sceneViewMatrix;
    glm::mat4 m_sceneProjectionMatrix;
    glm::vec3 m_sceneCameraPos;
    /* ==============================
     *  Data members
     * ============================== */

     /* --- injected externals --- */
    QOpenGLWidget* m_viewportWidget = nullptr;   ///< owner widget
    QOpenGLFunctions_4_3_Core* m_gl = nullptr;   ///< GL 4.1 functions
    QOpenGLContext* m_lastContext = nullptr;
    entt::registry* m_registry = nullptr;   ///< set per-frame

    /* --- dimensions --- */
    int m_width = 0;
    int m_height = 0;
    float m_elapsedTime = 0.0f;
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
    std::unique_ptr<Shader> m_arrowFieldComputeShader;
    std::unique_ptr<Shader> m_particleUpdateComputeShader;
    std::unique_ptr<Shader> m_particleRenderShader;
    std::unique_ptr<Shader> m_flowVectorComputeShader;
    /* --- GPU resources --- */

    GLuint m_intersectionVAO = 0, m_intersectionVBO = 0;

    /* --- framebuffers / textures --- */
 
    GLuint m_quadVAO = 0;     ///< fullscreen composite quad

    /* --- state flags --- */
    bool m_isInitialized = false;

    
    std::unordered_map<QOpenGLWidget*, TargetFBOs> m_targets;


    struct ContextPrimitives
    {
        GLuint gridVAO = 0, gridVBO = 0;
        GLuint lineVAO = 0, lineVBO = 0;
        GLuint capVAO = 0, capVBO = 0;
        GLuint compositeVAO = 0; // For the fullscreen composite pass

        GLuint arrowVAO = 0, arrowVBO = 0, arrowEBO = 0, instanceVBO = 0;
        size_t arrowIndexCount = 0;
    };

    GLuint m_effectorDataUBO = 0;
    GLuint m_triangleDataSSBO = 0;
    const GLsizei stride = 96;
    GLuint m_debugBuffer = 0;
    GLuint m_debugAtomicCounter = 0;


    QHash<QOpenGLContext*, ContextPrimitives> m_contextPrimitives;
    void initOrResizeFBOsForTarget(TargetFBOs& target, int width, int height);
};