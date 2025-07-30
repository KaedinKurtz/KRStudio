#pragma once
#include "IRenderPass.hpp"
#include "components.hpp"
#include <QHash>
#include <QOpenGLFunctions_4_3_Core>
#include <QtGui/qopengl.h>
#include <vector>

class Shader;
class QOpenGLContext;

class FieldVisualizerPass : public IRenderPass {
public:
    ~FieldVisualizerPass() override;

    void initialize(RenderingSystem& renderer, QOpenGLFunctions_4_3_Core* gl) override;
    void execute(const RenderFrameContext& context) override;
    void onContextDestroyed(QOpenGLContext* dyingContext, QOpenGLFunctions_4_3_Core* gl) override;

    // This is a public helper now, which is fine.
    void createResourcesForContext(QOpenGLFunctions_4_3_Core* gl);

private:  
    
    struct VisResources {
        GLuint particleBuffer[2] = { 0 };
        GLuint particleVAO[2] = { 0 };
        int currentReadBuffer = 0;

        GLuint samplePointsSSBO = 0;
        GLuint instanceDataSSBO = 0;
        GLuint commandUBO = 0; // For indirect drawing
        int numSamplePoints = 0;
    };

    // A map from a context to the resources for each entity in that context.
    QHash<QOpenGLContext*, QHash<entt::entity, VisResources>> m_perContextResources;

    void createArrowPrimitiveForContext(QOpenGLContext* ctx, QOpenGLFunctions_4_3_Core* gl);
    void gatherEffectorData(const RenderFrameContext& context);

    // --- UPDATED FUNCTION SIGNATURES ---
    // These functions now take the specific resources they need as parameters.
    void uploadEffectorData(QOpenGLFunctions_4_3_Core* gl, GLuint uboID, GLuint ssboID);

    // --- These function signatures need to be updated to pass the resources ---
    void renderParticles(const RenderFrameContext& context, FieldVisualizerComponent& vis, const TransformComponent& xf,
        Shader* renderShader, Shader* computeShader, GLuint uboID, GLuint ssboID, VisResources& res);

    void renderFlow(const RenderFrameContext& context, FieldVisualizerComponent& vis, const TransformComponent& xf,
        Shader* renderShader, Shader* computeShader, GLuint uboID, GLuint ssboID, VisResources& res);

    void renderArrows(const RenderFrameContext& context, FieldVisualizerComponent& vis, const TransformComponent& xf,
        Shader* renderShader, Shader* computeShader, GLuint uboID, GLuint ssboID, VisResources& res);

    // --- END UPDATED SIGNATURES ---

    // You have correctly updated these members to be per-context.
    QHash<QOpenGLContext*, GLuint> m_effectorDataUBOs;
    QHash<QOpenGLContext*, GLuint> m_triangleDataSSBOs;
    QHash<QOpenGLContext*, GLuint> m_arrowVAOs;
    QHash<QOpenGLContext*, GLuint> m_arrowVBOs;
    QHash<QOpenGLContext*, GLuint> m_arrowEBOs;
    QHash<QOpenGLContext*, size_t> m_arrowIndexCounts;

    // This CPU-side data is fine as-is.
    std::vector<PointEffectorGpu> m_pointEffectors;
    std::vector<TriangleGpu> m_triangleEffectors;
    std::vector<DirectionalEffectorGpu> m_directionalEffectors;


};