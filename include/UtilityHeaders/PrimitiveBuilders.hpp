#pragma once
#ifdef _WIN32
#   ifndef NOMINMAX      // keep <windows.h> from defining min/max macros
#     define NOMINMAX
#   endif
#endif

#include <algorithm>      // std::min / std::max
#include <cmath>
#include <vector>
#include <map>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <QtGui/qopengl.h>              // GLuint / GLenum / …
#include <QOpenGLFunctions_4_3_Core>
#include "components.hpp" // This header should contain your updated Vertex struct definition

enum class Primitive {
    Cube,
    Quad,
    Cylinder,
    Cone,
    Torus,
    IcoSphere
};

/* ------------------------------------------------------------------------- */
/* buildUnitCube – 24 verts, 12 triangles                                    */
/* ------------------------------------------------------------------------- */
inline void buildUnitCube(std::vector<Vertex>& verts,
    std::vector<uint32_t>& idx)
{
    verts.clear();
    verts.reserve(24);
    glm::vec2 defaultUV(0.0f);
    glm::vec3 defaultTangent(0.0f);
    glm::vec3 defaultBitangent(0.0f);

    verts.insert(verts.end(), {
        /* +X face */ { {+0.5f,-0.5f,-0.5f},{+1,0,0}, defaultUV, defaultTangent, defaultBitangent }, { {+0.5f,+0.5f,-0.5f},{+1,0,0}, defaultUV, defaultTangent, defaultBitangent }, { {+0.5f,+0.5f,+0.5f},{+1,0,0}, defaultUV, defaultTangent, defaultBitangent }, { {+0.5f,-0.5f,+0.5f},{+1,0,0}, defaultUV, defaultTangent, defaultBitangent },
        /* -X face */ { {-0.5f,-0.5f,+0.5f},{-1,0,0}, defaultUV, defaultTangent, defaultBitangent }, { {-0.5f,+0.5f,+0.5f},{-1,0,0}, defaultUV, defaultTangent, defaultBitangent }, { {-0.5f,+0.5f,-0.5f},{-1,0,0}, defaultUV, defaultTangent, defaultBitangent }, { {-0.5f,-0.5f,-0.5f},{-1,0,0}, defaultUV, defaultTangent, defaultBitangent },
        /* +Y face */ { {-0.5f,+0.5f,-0.5f},{0,+1,0}, defaultUV, defaultTangent, defaultBitangent }, { {-0.5f,+0.5f,+0.5f},{0,+1,0}, defaultUV, defaultTangent, defaultBitangent }, { {+0.5f,+0.5f,+0.5f},{0,+1,0}, defaultUV, defaultTangent, defaultBitangent }, { {+0.5f,+0.5f,-0.5f},{0,+1,0}, defaultUV, defaultTangent, defaultBitangent },
        /* -Y face */ { {-0.5f,-0.5f,+0.5f},{0,-1,0}, defaultUV, defaultTangent, defaultBitangent }, { {-0.5f,-0.5f,-0.5f},{0,-1,0}, defaultUV, defaultTangent, defaultBitangent }, { {+0.5f,-0.5f,-0.5f},{0,-1,0}, defaultUV, defaultTangent, defaultBitangent }, { {+0.5f,-0.5f,+0.5f},{0,-1,0}, defaultUV, defaultTangent, defaultBitangent },
        /* +Z face */ { {-0.5f,-0.5f,+0.5f},{0,0,+1}, defaultUV, defaultTangent, defaultBitangent }, { {+0.5f,-0.5f,+0.5f},{0,0,+1}, defaultUV, defaultTangent, defaultBitangent }, { {+0.5f,+0.5f,+0.5f},{0,0,+1}, defaultUV, defaultTangent, defaultBitangent }, { {-0.5f,+0.5f,+0.5f},{0,0,+1}, defaultUV, defaultTangent, defaultBitangent },
        /* -Z face */ { {+0.5f,-0.5f,-0.5f},{0,0,-1}, defaultUV, defaultTangent, defaultBitangent }, { {-0.5f,-0.5f,-0.5f},{0,0,-1}, defaultUV, defaultTangent, defaultBitangent }, { {-0.5f,+0.5f,-0.5f},{0,0,-1}, defaultUV, defaultTangent, defaultBitangent }, { {+0.5f,+0.5f,-0.5f},{0,0,-1}, defaultUV, defaultTangent, defaultBitangent }
        });

    idx = {
        0,1,2, 0,2,3,      // +X
        4,5,6, 4,6,7,      // -X
        8,9,10, 8,10,11,   // +Y
        12,13,14, 12,14,15, // -Y
        16,17,18, 16,18,19, // +Z
        20,21,22, 20,22,23  // -Z
    };
}


/* ------------------------------------------------------------------------- */
/* buildQuad – 4 verts, 2 triangles, facing +Z                               */
/* ------------------------------------------------------------------------- */
inline void buildQuad(std::vector<Vertex>& verts, std::vector<uint32_t>& idx)
{
    verts = {
        { {-0.5f, -0.5f, 0.0f}, {0,0,1}, {0,0}, {1,0,0}, {0,1,0} },
        { { 0.5f, -0.5f, 0.0f}, {0,0,1}, {1,0}, {1,0,0}, {0,1,0} },
        { { 0.5f,  0.5f, 0.0f}, {0,0,1}, {1,1}, {1,0,0}, {0,1,0} },
        { {-0.5f,  0.5f, 0.0f}, {0,0,1}, {0,1}, {1,0,0}, {0,1,0} }
    };
    idx = { 0, 1, 2, 2, 3, 0 };
}

/* ------------------------------------------------------------------------- */
/* buildCylinder – Parametric cylinder oriented along the Y axis             */
/* ------------------------------------------------------------------------- */
inline void buildCylinder(std::vector<Vertex>& verts, std::vector<uint32_t>& idx,
    float height = 1.0f, float radius = 0.5f, int segments = 16)
{
    verts.clear();
    idx.clear();
    float halfHeight = height / 2.0f;

    // Side vertices
    for (int i = 0; i <= segments; ++i) {
        float angle = (float)i / (float)segments * 2.0f * glm::pi<float>();
        float x = cos(angle) * radius;
        float z = sin(angle) * radius;
        glm::vec3 normal = glm::normalize(glm::vec3(x, 0, z));
        verts.push_back({ {x, -halfHeight, z}, normal, {}, {}, {} });
        verts.push_back({ {x,  halfHeight, z}, normal, {}, {}, {} });
    }

    // Indices for sides
    for (int i = 0; i < segments; ++i) {
        int i0 = i * 2;
        int i1 = i0 + 1;
        int i2 = i0 + 2;
        int i3 = i0 + 3;
        idx.push_back(i0); idx.push_back(i2); idx.push_back(i1);
        idx.push_back(i1); idx.push_back(i2); idx.push_back(i3);
    }
}

/* ------------------------------------------------------------------------- */
/* buildCone – Parametric cone oriented along the Y axis                     */
/* ------------------------------------------------------------------------- */
inline void buildCone(std::vector<Vertex>& verts, std::vector<uint32_t>& idx,
    float height = 1.0f, float radius = 0.5f, int segments = 16)
{
    verts.clear();
    idx.clear();
    glm::vec3 tip = { 0, height, 0 };
    verts.push_back({ tip, {0, 1, 0}, {}, {}, {} }); // Tip vertex

    // Base vertices
    for (int i = 0; i <= segments; ++i) {
        float angle = (float)i / (float)segments * 2.0f * glm::pi<float>();
        float x = cos(angle) * radius;
        float z = sin(angle) * radius;
        verts.push_back({ {x, 0, z}, {0, -1, 0}, {}, {}, {} });
    }

    // Indices for sides and base
    for (int i = 1; i <= segments; ++i) {
        idx.push_back(0); // Tip
        idx.push_back(i);
        idx.push_back(i + 1);
    }
}

/* ------------------------------------------------------------------------- */
/* buildTorus – Parametric torus in the XY plane                             */
/* ------------------------------------------------------------------------- */
inline void buildTorus(std::vector<Vertex>& verts, std::vector<uint32_t>& idx,
    float majorRadius = 1.0f, float minorRadius = 0.02f,
    int majorSegments = 24, int minorSegments = 12,
    float startAngleDegrees = 0.0f, float sweepAngleDegrees = 360.0f)
{
    verts.clear();
    idx.clear();

    // Correctly calculate the number of major segments for the sweep angle
    int numMajorSegments = static_cast<int>(std::ceil(majorSegments * (sweepAngleDegrees / 360.0f)));
    if (numMajorSegments == 0) return;

    for (int i = 0; i <= numMajorSegments; i++) {
        float majorFraction = (float)i / (float)numMajorSegments;
        float majorAngle = glm::radians(startAngleDegrees + majorFraction * sweepAngleDegrees);
        glm::vec3 majorPos(cos(majorAngle) * majorRadius, sin(majorAngle) * majorRadius, 0);

        for (int j = 0; j <= minorSegments; j++) {
            float minorAngle = (float)j / (float)minorSegments * 2.0f * glm::pi<float>();
            glm::vec3 normal = glm::vec3(cos(majorAngle) * cos(minorAngle), sin(majorAngle) * cos(minorAngle), sin(minorAngle));
            glm::vec3 pos = majorPos + normal * minorRadius;
            verts.push_back({ pos, normal, {}, {}, {} });
        }
    }

    for (int i = 0; i < numMajorSegments; i++) {
        for (int j = 0; j < minorSegments; j++) {
            int i0 = i * (minorSegments + 1) + j;
            int i1 = i0 + 1;
            int i2 = (i + 1) * (minorSegments + 1) + j;
            int i3 = i2 + 1;
            idx.push_back(i0); idx.push_back(i2); idx.push_back(i1);
            idx.push_back(i1); idx.push_back(i2); idx.push_back(i3);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* buildIcoSphere – very small helper; subdivisions = 0..3 is enough         */
/* ------------------------------------------------------------------------- */
inline void buildIcoSphere(std::vector<Vertex>& verts,
    std::vector<uint32_t>& idx,
    int subdivisions = 0)
{
    /* ---- golden-ratio icosahedron -------------------------------------- */
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<glm::vec3> base_positions;
    base_positions.reserve(12);
    base_positions.insert(base_positions.end(), {
        { -1,  t,  0 }, {  1,  t,  0 }, { -1, -t,  0 }, {  1, -t,  0 },
        {  0, -1,  t }, {  0,  1,  t }, {  0, -1, -t }, {  0,  1, -t },
        {  t,  0, -1 }, {  t,  0,  1 }, { -t,  0, -1 }, { -t,  0,  1 }
        });
    for (auto& v : base_positions) v = glm::normalize(v) * 0.5f;   // radius 0.5

    std::vector<uint32_t> faces = {
        0,11,5,  0,5,1,  0,1,7,  0,7,10, 0,10,11,
        1,5,9,   5,11,4, 11,10,2, 10,7,6, 7,1,8,
        3,9,4,   3,4,2,  3,2,6,   3,6,8,  3,8,9,
        4,9,5,   2,4,11, 6,2,10,  8,6,7,  9,8,1
    };

    /* ---- optional subdivision ----------------------------------------- */
    struct Edge {
        uint32_t a, b; bool operator<(Edge o)const {
            return a < o.a || (a == o.a && b < o.b);
        }
    };
    std::map<Edge, uint32_t> midpoint;

    auto getMid = [&](uint32_t a, uint32_t b)->uint32_t {
        Edge key{ std::min(a,b), std::max(a,b) };
        auto it = midpoint.find(key);
        if (it != midpoint.end()) return it->second;
        glm::vec3 mid = glm::normalize(base_positions[a] + base_positions[b]) * 0.5f;
        base_positions.push_back(mid);
        uint32_t new_idx = uint32_t(base_positions.size() - 1);
        midpoint[key] = new_idx;
        return new_idx;
        };

    for (int n = 0; n < subdivisions; ++n) {
        std::vector<uint32_t> newFaces;
        for (size_t i = 0; i < faces.size(); i += 3) {
            uint32_t a = faces[i], b = faces[i + 1], c = faces[i + 2];
            uint32_t ab = getMid(a, b);
            uint32_t bc = getMid(b, c);
            uint32_t ca = getMid(c, a);
            newFaces.insert(newFaces.end(),
                { a,ab,ca,  b,bc,ab,  c,ca,bc,  ab,bc,ca });
        }
        faces.swap(newFaces);
    }

    /* ---- convert to Vertex -------------------------------------------- */
    verts.resize(base_positions.size());
    for (size_t i = 0; i < base_positions.size(); ++i) {
        // THE FIX: Use the 5-argument constructor with default values for uv, tangent, bitangent
        verts[i] = Vertex(
            base_positions[i],                // position
            glm::normalize(base_positions[i]),// normal
            glm::vec2(0.0f),                  // uv
            glm::vec3(0.0f),                  // tangent
            glm::vec3(0.0f)                   // bitangent
        );
    }
    idx.assign(faces.begin(), faces.end());
}

/* ------------------------------------------------------------------------- */
/* createArrowPrimitive - simple 3D arrow along +Z for instancing            */
/* ------------------------------------------------------------------------- */
inline std::size_t createArrowPrimitive(std::vector<Vertex>& vertices, std::vector<unsigned int>& indices)
{
    vertices.clear();
    indices.clear();

    float shaftRadius = 0.05f;
    float headRadius = 0.12f;
    float headLength = 0.35f;
    float totalLength = 1.0f;
    float shaftLength = totalLength - headLength;
    float tipZ = totalLength / 2.0f;
    float headBaseZ = tipZ - headLength;
    float shaftBaseZ = -totalLength / 2.0f;

    // THE FIX: Provide default uv, tangent, and bitangent for each vertex.
    glm::vec2 defaultUV(0.0f);
    glm::vec3 defaultTangent(0.0f);
    glm::vec3 defaultBitangent(0.0f);

    // Arrow Tip (a simple cone)
    vertices.push_back({ {0.0f, 0.0f, tipZ}, {0.0f, 0.0f, 1.0f}, defaultUV, defaultTangent, defaultBitangent }); // 0: Tip vertex
    vertices.push_back({ {headRadius, 0.0f, headBaseZ}, {0.8f, 0.0f, 0.2f}, defaultUV, defaultTangent, defaultBitangent });  // 1
    vertices.push_back({ {0.0f, headRadius, headBaseZ}, {0.0f, 0.8f, 0.2f}, defaultUV, defaultTangent, defaultBitangent });  // 2
    vertices.push_back({ {-headRadius, 0.0f, headBaseZ}, {-0.8f, 0.0f, 0.2f}, defaultUV, defaultTangent, defaultBitangent }); // 3
    vertices.push_back({ {0.0f, -headRadius, headBaseZ}, {0.0f, -0.8f, 0.2f}, defaultUV, defaultTangent, defaultBitangent }); // 4

    indices.insert(indices.end(), { 0, 1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 1, 1, 4, 3, 1, 3, 2 }); // Tip + Base

    // Arrow Shaft (a simple cylinder)
    vertices.push_back({ {shaftRadius, 0.0f, headBaseZ}, {1.0f, 0.0f, 0.0f}, defaultUV, defaultTangent, defaultBitangent });    // 5
    vertices.push_back({ {0.0f, shaftRadius, headBaseZ}, {0.0f, 1.0f, 0.0f}, defaultUV, defaultTangent, defaultBitangent });    // 6
    vertices.push_back({ {-shaftRadius, 0.0f, headBaseZ}, {-1.0f, 0.0f, 0.0f}, defaultUV, defaultTangent, defaultBitangent });   // 7
    vertices.push_back({ {0.0f, -shaftRadius, headBaseZ}, {0.0f, -1.0f, 0.0f}, defaultUV, defaultTangent, defaultBitangent });   // 8
    vertices.push_back({ {shaftRadius, 0.0f, shaftBaseZ}, {1.0f, 0.0f, 0.0f}, defaultUV, defaultTangent, defaultBitangent });    // 9
    vertices.push_back({ {0.0f, shaftRadius, shaftBaseZ}, {0.0f, 1.0f, 0.0f}, defaultUV, defaultTangent, defaultBitangent });    // 10
    vertices.push_back({ {-shaftRadius, 0.0f, shaftBaseZ}, {-1.0f, 0.0f, 0.0f}, defaultUV, defaultTangent, defaultBitangent });  // 11
    vertices.push_back({ {0.0f, -shaftRadius, shaftBaseZ}, {0.0f, -1.0f, 0.0f}, defaultUV, defaultTangent, defaultBitangent });  // 12

    indices.insert(indices.end(), { 5, 9, 10, 5, 10, 6, 6, 10, 11, 6, 11, 7, 7, 11, 12, 7, 12, 8, 8, 12, 9, 8, 9, 5 });

    return indices.size();
}

/* ------------------------------------------------------------------------- *
 * VERY-SMALL helpers – they only create valid VAO / VBO data so that the
 * renderer has something to bind.  Feel free to replace them with higher
 * quality meshes later.                                                     *
 * ------------------------------------------------------------------------- */
inline void buildGrid(QOpenGLFunctions_4_3_Core* gl,
    GLuint vao, GLuint vbo)
{
    static const glm::vec3 verts[6] = {
        { -1.0f, 0.0f, -1.0f },
        { -1.0f, 0.0f,  1.0f },
        {  1.0f, 0.0f,  1.0f },
        { -1.0f, 0.0f, -1.0f },
        {  1.0f, 0.0f,  1.0f },
        {  1.0f, 0.0f, -1.0f }
    };
    gl->glBindVertexArray(vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, vbo);
    gl->glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    gl->glBindVertexArray(0);
}

inline void buildUnitLine(QOpenGLFunctions_4_3_Core* gl,
    GLuint vao, GLuint vbo)
{
    static const glm::vec3 verts[2] = {
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f }
    };
    gl->glBindVertexArray(vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, vbo);
    gl->glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    gl->glBindVertexArray(0);
}

inline void buildCap(QOpenGLFunctions_4_3_Core* gl,
    GLuint vao, GLuint vbo)
{
    const int segments = 32;          // match your shader’s expectations
    std::vector<glm::vec3> pts;
    pts.reserve(segments);
    for (int i = 0; i < segments; ++i) {
        float theta = 2.0f * 3.1415926f * float(i) / float(segments);
        pts.emplace_back(std::cos(theta), std::sin(theta), 0.0f);
    }
    gl->glBindVertexArray(vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, vbo);
    gl->glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3) * pts.size(), pts.data(), GL_STATIC_DRAW);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    gl->glBindVertexArray(0);
}

inline void buildGridMesh(QOpenGLFunctions_4_3_Core* gl, GLuint vao, GLuint vbo, float halfSize)
{
    // 6 verts => 2 triangles
    std::array<glm::vec3, 6> verts = { {
        { -halfSize, 0.0f, -halfSize },
        {  halfSize, 0.0f, -halfSize },
        {  halfSize, 0.0f,  halfSize },
        { -halfSize, 0.0f, -halfSize },
        {  halfSize, 0.0f,  halfSize },
        { -halfSize, 0.0f,  halfSize }
    } };

    gl->glBindVertexArray(vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, vbo);
    gl->glBufferData(GL_ARRAY_BUFFER,
        verts.size() * sizeof(glm::vec3),
        verts.data(),
        GL_STATIC_DRAW);

    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(glm::vec3),
        (void*)0);

    // unbind
    gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
    gl->glBindVertexArray(0);
}

inline void setupFullscreenQuadAttribs(QOpenGLFunctions_4_3_Core* gl, GLuint vao)
{
    gl->glBindVertexArray(vao);
    // no VBO/attribs needed — the shader computes the positions from gl_VertexID
    gl->glBindVertexArray(0);
}

/* ------------------------------------------------------------------------- *
 * buildArrowMesh  – uses createArrowPrimitive() you already have            *
 * ------------------------------------------------------------------------- */
inline std::size_t buildArrowMesh(QOpenGLFunctions_4_3_Core* gl,
    GLuint vao, GLuint vbo, GLuint ebo)
{
    std::vector<Vertex>       verts;
    std::vector<unsigned int>  idx;
    std::size_t indexCount = createArrowPrimitive(verts, idx);

    gl->glBindVertexArray(vao);

    gl->glBindBuffer(GL_ARRAY_BUFFER, vbo);
    gl->glBufferData(GL_ARRAY_BUFFER,
        verts.size() * sizeof(Vertex),
        verts.data(), GL_STATIC_DRAW);

    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        idx.size() * sizeof(unsigned int),
        idx.data(), GL_STATIC_DRAW);

    // layout : location 0 = position,  location 1 = normal
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
        sizeof(Vertex), (void*)offsetof(Vertex, position));
    gl->glEnableVertexAttribArray(1);
    gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
        sizeof(Vertex), (void*)offsetof(Vertex, normal));

    // THE FIX: Also enable the other vertex attributes for the arrow mesh
    gl->glEnableVertexAttribArray(2);
    gl->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
    gl->glEnableVertexAttribArray(3);
    gl->glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, tangent));
    gl->glEnableVertexAttribArray(4);
    gl->glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, bitangent));


    gl->glBindVertexArray(0);
    return indexCount;
}
