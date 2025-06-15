#include "RenderingSystem.hpp"
#include "Scene.hpp"
#include "components.hpp"      // RenderableMeshComponent, RenderResourceComponent, TransformComponent
#include "Shader.hpp"

#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLVersionFunctionsFactory>
#include <QDebug>
#include <memory>

namespace {
    // Our global Phong shader
    std::unique_ptr<Shader> s_phongShader;
}

bool RenderingSystem::s_isInitialized = false;

void RenderingSystem::initialize() {
    if (s_isInitialized) {
        return;
    }

    qDebug() << "[RenderingSystem] Initializing...";
    auto* ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        qWarning() << "[RenderingSystem] No current GL context!";
        return;
    }

    // Get the GL function pointers
    auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>(ctx);
    if (!gl) {
        qWarning() << "[RenderingSystem] Unable to get GL functions!";
        return;
    }

    // Enable depth testing
    gl->glEnable(GL_DEPTH_TEST);

    // Load the Phong shader, passing 'gl' (not 'ctx')
    try {
        s_phongShader = std::make_unique<Shader>(
            gl,
            "shaders/vertex_shader.glsl",
            "shaders/fragment_shader.glsl"
        );
    }
    catch (const std::exception& e) {
        qWarning() << "[RenderingSystem] Shader load failed:" << e.what();
        return;
    }

    s_isInitialized = true;
}

void RenderingSystem::shutdown(Scene* scene) {
    if (!s_isInitialized) {
        return;
    }

    qDebug() << "[RenderingSystem] Shutting down...";
    auto* ctx = QOpenGLContext::currentContext();
    if (ctx && scene) {
        auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>(ctx);
        if (gl) {
            // Delete all VAO/VBO/EBO for existing RenderResourceComponent
            auto& registry = scene->getRegistry();
            auto view = registry.view<RenderResourceComponent>();
            for (auto entity : view) {
                auto& rc = view.get<RenderResourceComponent>(entity);
                if (rc.VAO) gl->glDeleteVertexArrays(1, &rc.VAO);
                if (rc.VBO) gl->glDeleteBuffers(1, &rc.VBO);
                if (rc.EBO) gl->glDeleteBuffers(1, &rc.EBO);
                rc.VAO = rc.VBO = rc.EBO = 0;
            }
        }
        else {
            qWarning() << "[RenderingSystem] No GL functions for shutdown.";
        }
    }
    else {
        qWarning() << "[RenderingSystem] Shutdown called with null scene or GL context.";
    }

    // Destroy the shader
    s_phongShader.reset();
    s_isInitialized = false;
}

void RenderingSystem::render(Scene* scene,
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& camPosition)
{
    if (!s_isInitialized || !scene || !s_phongShader) {
        return;
    }

    auto* ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        return;
    }

    auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_3_3_Core>(ctx);
    if (!gl) {
        return;
    }

    auto& registry = scene->getRegistry();

    // 1) Set up shader uniforms
    s_phongShader->use();
    s_phongShader->setMat4("view", view);
    s_phongShader->setMat4("projection", projection);
    s_phongShader->setVec3("lightPos", camPosition);
    s_phongShader->setVec3("viewPos", camPosition);
    s_phongShader->setVec3("lightColor", glm::vec3(1.0f));

    // 2) Draw each renderable mesh
    auto viewEntities = registry.view<
        const RenderableMeshComponent,
        RenderResourceComponent,
        const TransformComponent
    >();

    for (auto entity : viewEntities) {
        auto const& mesh = viewEntities.get<const RenderableMeshComponent>(entity);
        auto& res = viewEntities.get<RenderResourceComponent>(entity);
        auto const& xf = viewEntities.get<const TransformComponent>(entity);

        // Lazy-create GL resources
        if (res.VAO == 0) {
            gl->glGenVertexArrays(1, &res.VAO);
            gl->glGenBuffers(1, &res.VBO);
            gl->glGenBuffers(1, &res.EBO);

            gl->glBindVertexArray(res.VAO);

            gl->glBindBuffer(GL_ARRAY_BUFFER, res.VBO);
            gl->glBufferData(
                GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(glm::vec3)),
                mesh.vertices.data(),
                GL_STATIC_DRAW
            );

            gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, res.EBO);
            gl->glBufferData(
                GL_ELEMENT_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(unsigned int)),
                mesh.indices.data(),
                GL_STATIC_DRAW
            );

            gl->glEnableVertexAttribArray(0);
            gl->glVertexAttribPointer(
                0, 3, GL_FLOAT, GL_FALSE,
                sizeof(glm::vec3),
                nullptr
            );

            gl->glBindVertexArray(0);
        }

        // 3) Set per-object uniforms
        s_phongShader->setMat4("model", xf.getTransform());
        s_phongShader->setVec3("objectColor", mesh.color);

        // 4) Draw
        gl->glBindVertexArray(res.VAO);
        gl->glDrawElements(
            GL_TRIANGLES,
            static_cast<GLsizei>(mesh.indices.size()),
            GL_UNSIGNED_INT,
            nullptr
        );
        gl->glBindVertexArray(0);
    }
}
