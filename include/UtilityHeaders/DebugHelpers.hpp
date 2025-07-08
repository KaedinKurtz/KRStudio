#pragma once

#include <QDebug>
#include <QOpenGLFunctions_4_3_Core>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <QString>
#include <iomanip>

// Forward declarations to keep headers clean
class Shader;
struct BoundingBoxComponent;

// This operator overload allows you to stream entt::entity directly to qDebug.
inline QDebug operator<<(QDebug dbg, const entt::entity entity) {
    dbg.nospace() << "e(" << static_cast<uint32_t>(entity) << ")";
    return dbg.space();
}

namespace DebugHelpers
{
    // Helper to print a glm::mat4 clearly to the console for debugging.
    inline void printMatrix(const glm::mat4& mat, const QString& label) {
        //qDebug().noquote() << label;
        for (int i = 0; i < 4; ++i) {
            QString row = QString("  | %1 %2 %3 %4 |")
                .arg(mat[i][0], 8, 'f', 2)
                .arg(mat[i][1], 8, 'f', 2)
                .arg(mat[i][2], 8, 'f', 2)
                .arg(mat[i][3], 8, 'f', 2);
            //qDebug().noquote() << row;
        }
    }

    // The missing function that draws the outline for a selected entity's bounding box.
    inline void drawAABBOutline(
        QOpenGLFunctions_4_3_Core& gl,
        Shader& outlineShader,
        const BoundingBoxComponent& box,
        unsigned int vao,
        unsigned int vbo,
        const glm::mat4& view,
        const glm::mat4& projection)
    {
        // Define the 8 vertices of the bounding box
        glm::vec3 vertices[] = {
            {box.min.x, box.min.y, box.min.z}, // 0
            {box.max.x, box.min.y, box.min.z}, // 1
            {box.max.x, box.max.y, box.min.z}, // 2
            {box.min.x, box.max.y, box.min.z}, // 3
            {box.min.x, box.min.y, box.max.z}, // 4
            {box.max.x, box.min.y, box.max.z}, // 5
            {box.max.x, box.max.y, box.max.z}, // 6
            {box.min.x, box.max.y, box.max.z}  // 7
        };

        // Define the 12 lines (24 indices) that form the box outline
        unsigned int indices[] = {
            0, 1, 1, 2, 2, 3, 3, 0, // Bottom face
            4, 5, 5, 6, 6, 7, 7, 4, // Top face
            0, 4, 1, 5, 2, 6, 3, 7  // Connecting lines
        };

        QOpenGLFunctions_4_3_Core* glPtr = &gl;

        outlineShader.use(glPtr);
        outlineShader.setMat4(glPtr, "view", view);
        outlineShader.setMat4(glPtr, "projection", projection);
        outlineShader.setMat4(glPtr, "model", glm::mat4(1.0f)); // Outline is in world space
        outlineShader.setVec3(glPtr, "outlineColor", glm::vec3(1.0f, 0.8f, 0.0f));

        gl.glBindVertexArray(vao);
        gl.glBindBuffer(GL_ARRAY_BUFFER, vbo);
        gl.glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

        // Create and bind a temporary EBO for drawing the lines
        unsigned int tempEBO;
        gl.glGenBuffers(1, &tempEBO);
        gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tempEBO);
        gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

        gl.glDrawElements(GL_LINES, 24, GL_UNSIGNED_INT, 0);

        gl.glBindVertexArray(0);
        gl.glDeleteBuffers(1, &tempEBO); // Clean up the temporary EBO
    }
}