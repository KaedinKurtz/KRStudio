#include "RenderingSystem.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <QDebug>
#include <QOpenGLContext>


namespace RenderingSystem
{
    namespace
    {

        std::unique_ptr<QOpenGLFunctions_3_3_Core> g_gl;
        std::unique_ptr<Shader> g_gridShader;
        std::unique_ptr<Shader> g_phongShader;
        std::unique_ptr<Mesh>   g_gridMesh;
        std::unique_ptr<Mesh>   g_cubeMesh;

        unsigned int g_gridVAO = 0, g_gridVBO = 0;
        unsigned int g_cubeVAO = 0, g_cubeVBO = 0, g_cubeEBO = 0;

        glm::mat4 computeWorldTransform(entt::entity entity, entt::registry& registry)
        {
            auto& transform = registry.get<TransformComponent>(entity);
            glm::mat4 local = transform.getTransform();
            if (registry.all_of<ParentComponent>(entity))
            {
                auto parent = registry.get<ParentComponent>(entity).parent;
                if (registry.valid(parent))
                    return computeWorldTransform(parent, registry) * local;
            }
            return local;
        }
    }

    void initialize(QOpenGLFunctions_3_3_Core* gl)
    {

        if (!gl)
        {
            qWarning() << "[RenderingSystem] initialize called with nullptr";
            return;
        }
        if (!QOpenGLContext::currentContext())
        {
            qWarning() << "[RenderingSystem] initialize called without current context";
            return;
        }
        g_gl.reset(QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_3_Core>());
        if (!g_gl)
        {
            qWarning() << "[RenderingSystem] failed to obtain OpenGL functions";
            return;
        }
        g_gl->initializeOpenGLFunctions();

        const float halfSize = 1000.0f;
        const std::vector<float> grid_vertices = {
            -halfSize, 0.0f, -halfSize,
             halfSize, 0.0f, -halfSize,
             halfSize, 0.0f,  halfSize,
             halfSize, 0.0f,  halfSize,
            -halfSize, 0.0f,  halfSize,
            -halfSize, 0.0f, -halfSize };

        g_gridShader = std::make_unique<Shader>(g_gl.get(), "shaders/grid_vert.glsl", "shaders/grid_frag.glsl");
        g_phongShader = std::make_unique<Shader>(g_gl.get(), "shaders/vertex_shader.glsl", "shaders/fragment_shader.glsl");
      
        g_gridMesh = std::make_unique<Mesh>(grid_vertices);
        g_cubeMesh = std::make_unique<Mesh>(Mesh::getLitCubeVertices(), Mesh::getLitCubeIndices());


        g_gl->glGenVertexArrays(1, &g_gridVAO);
        g_gl->glGenBuffers(1, &g_gridVBO);
        g_gl->glBindVertexArray(g_gridVAO);
        g_gl->glBindBuffer(GL_ARRAY_BUFFER, g_gridVBO);
        g_gl->glBufferData(GL_ARRAY_BUFFER, grid_vertices.size() * sizeof(float), grid_vertices.data(), GL_STATIC_DRAW);
        g_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        g_gl->glEnableVertexAttribArray(0);
        g_gl->glBindVertexArray(0);

        g_gl->glGenVertexArrays(1, &g_cubeVAO);
        g_gl->glGenBuffers(1, &g_cubeVBO);
        g_gl->glGenBuffers(1, &g_cubeEBO);
        g_gl->glBindVertexArray(g_cubeVAO);
        g_gl->glBindBuffer(GL_ARRAY_BUFFER, g_cubeVBO);
        const auto& cubeVerts = g_cubeMesh->vertices();
        g_gl->glBufferData(GL_ARRAY_BUFFER, cubeVerts.size() * sizeof(float), cubeVerts.data(), GL_STATIC_DRAW);
        g_gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_cubeEBO);
        const auto& cubeInd = g_cubeMesh->indices();
        g_gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, cubeInd.size() * sizeof(unsigned int), cubeInd.data(), GL_STATIC_DRAW);
        g_gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        g_gl->glEnableVertexAttribArray(0);
        g_gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        g_gl->glEnableVertexAttribArray(1);
        g_gl->glBindVertexArray(0);

    }

    void shutdown(Scene* scene)
    {
        (void)scene;

        if (!g_gl)
            return;

        if (g_gridVAO) g_gl->glDeleteVertexArrays(1, &g_gridVAO);
        if (g_gridVBO) g_gl->glDeleteBuffers(1, &g_gridVBO);
        if (g_cubeVAO) g_gl->glDeleteVertexArrays(1, &g_cubeVAO);
        if (g_cubeVBO) g_gl->glDeleteBuffers(1, &g_cubeVBO);
        if (g_cubeEBO) g_gl->glDeleteBuffers(1, &g_cubeEBO);
        g_gridShader.reset();
        g_phongShader.reset();
        g_gridMesh.reset();
        g_cubeMesh.reset();
        g_gl.reset();

    }

    void uploadMeshes(Scene* scene)
    {
        if (!g_gl)
            return;


        auto& registry = scene->getRegistry();
        auto view = registry.view<RenderableMeshComponent>(entt::exclude<RenderResourceComponent>);
        for (auto entity : view)
        {
            registry.emplace<RenderResourceComponent>(entity);
        }
    }

    void render(Scene* scene,
                const glm::mat4& viewMatrix,
                const glm::mat4& projectionMatrix,
                const glm::vec3& cameraPos)
    {
        if (!g_gl)
            return;

        auto& registry = scene->getRegistry();

        if (g_gridShader)
        {
            g_gridShader->use();
            g_gridShader->setMat4("u_viewMatrix", viewMatrix);
            g_gridShader->setMat4("u_projectionMatrix", projectionMatrix);
            g_gridShader->setVec3("u_cameraPos", cameraPos);
            auto gridView = registry.view<const GridComponent, TransformComponent>();
            for (auto entity : gridView)
            {
                auto& transform = gridView.get<TransformComponent>(entity);
                g_gridShader->setMat4("u_gridModelMatrix", transform.getTransform());
                g_gl->glBindVertexArray(g_gridVAO);
                g_gl->glDrawArrays(GL_TRIANGLES, 0, static_cast<int>(g_gridMesh->vertices().size() / 3));
                g_gl->glBindVertexArray(0);
            }
        }

        if (g_phongShader)
        {
            g_phongShader->use();
            g_phongShader->setMat4("view", viewMatrix);
            g_phongShader->setMat4("projection", projectionMatrix);
            g_phongShader->setVec3("lightPos", cameraPos);
            g_phongShader->setVec3("viewPos", cameraPos);
            g_phongShader->setVec3("objectColor", glm::vec3(0.8f));
            g_phongShader->setVec3("lightColor", glm::vec3(1.0f));

            auto meshView = registry.view<const RenderableMeshComponent, TransformComponent>();
            for (auto entity : meshView)
            {
                glm::mat4 worldTransform = computeWorldTransform(entity, registry);
                g_phongShader->setMat4("model", worldTransform);
                g_gl->glBindVertexArray(g_cubeVAO);
                g_gl->glDrawElements(GL_TRIANGLES, static_cast<int>(g_cubeMesh->indices().size()), GL_UNSIGNED_INT, 0);
                g_gl->glBindVertexArray(0);
            }
        }
    }
}
