#pragma once
#include "IRenderPass.hpp"
#include "components.hpp"
#include <QHash>
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

private:
    void createArrowPrimitiveForContext(QOpenGLContext* ctx, QOpenGLFunctions_4_3_Core* gl);
    void gatherEffectorData(const RenderFrameContext& context);
    void uploadEffectorData(QOpenGLFunctions_4_3_Core* gl);
    void renderParticles(const RenderFrameContext& context, FieldVisualizerComponent& vis, const TransformComponent& xf);
    void renderFlow(const RenderFrameContext& context, FieldVisualizerComponent& vis, const TransformComponent& xf);
    void renderArrows(const RenderFrameContext& context, FieldVisualizerComponent& vis, const TransformComponent& xf);

    // --- Member Variables ---
    Shader* m_instancedArrowShader = nullptr;
    Shader* m_arrowFieldComputeShader = nullptr;
    Shader* m_particleUpdateComputeShader = nullptr;
    Shader* m_particleRenderShader = nullptr;
    Shader* m_flowVectorComputeShader = nullptr;

    GLuint m_effectorDataUBO = 0;
    GLuint m_triangleDataSSBO = 0;

    QHash<QOpenGLContext*, GLuint> m_arrowVAOs;
    QHash<QOpenGLContext*, GLuint> m_arrowVBOs;
    QHash<QOpenGLContext*, GLuint> m_arrowEBOs;
    QHash<QOpenGLContext*, size_t> m_arrowIndexCounts;

    std::vector<PointEffectorGpu> m_pointEffectors;
    std::vector<TriangleGpu> m_triangleEffectors;
    std::vector<DirectionalEffectorGpu> m_directionalEffectors;
};