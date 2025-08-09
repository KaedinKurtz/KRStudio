#pragma once
#include "IRenderPass.hpp"
#include "components.hpp"
#include <QHash>

class Shader;
class QOpenGLContext;

class SelectionGlowPass : public IRenderPass {
public:
    ~SelectionGlowPass() override;
    void initialize(RenderingSystem&, QOpenGLFunctions_4_3_Core*) override;
    void execute(const RenderFrameContext& ctx) override;
    void onContextDestroyed(QOpenGLContext*, QOpenGLFunctions_4_3_Core*) override;
private:
    void createCompositeVAOForContext(QOpenGLContext*, QOpenGLFunctions_4_3_Core*);
    QHash<QOpenGLContext*, GLuint> m_compositeVAOs;
    Shader* m_shaderMask = nullptr;
    Shader* m_shaderBlur = nullptr;
    Shader* m_shaderEdge = nullptr;
    Shader* m_shaderComposite = nullptr;
    Shader* m_shaderMaskDepthCmp = nullptr;
    GLuint  m_maskCopyTex = 0;
    // UI-tweakable params:
    glm::vec3 m_outlineColor = { 1.0f, 0.85f, 0.2f };
    float     m_intensity = 3.0f;
    float     m_threshold = 0.00f;
};
