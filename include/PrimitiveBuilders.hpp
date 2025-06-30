#pragma once

#include <vector>
#include <map>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include "components.hpp"

/* ------------------------------------------------------------------------- */
/* buildUnitCube – 24 verts, 12 triangles                                   */
/* ------------------------------------------------------------------------- */
inline void buildUnitCube(std::vector<Vertex>& verts,
    std::vector<uint32_t>& idx)
{
    verts.clear();
    verts.reserve(24);
    verts.insert(verts.end(), {
        /* +X face */ { {+0.5f,-0.5f,-0.5f},{+1,0,0} }, { {+0.5f,+0.5f,-0.5f},{+1,0,0} }, { {+0.5f,+0.5f,+0.5f},{+1,0,0} }, { {+0.5f,-0.5f,+0.5f},{+1,0,0} },
        /* -X face */ { {-0.5f,-0.5f,+0.5f},{-1,0,0} }, { {-0.5f,+0.5f,+0.5f},{-1,0,0} }, { {-0.5f,+0.5f,-0.5f},{-1,0,0} }, { {-0.5f,-0.5f,-0.5f},{-1,0,0} },
        /* +Y face */ { {-0.5f,+0.5f,-0.5f},{0,+1,0} }, { {-0.5f,+0.5f,+0.5f},{0,+1,0} }, { {+0.5f,+0.5f,+0.5f},{0,+1,0} }, { {+0.5f,+0.5f,-0.5f},{0,+1,0} },
        /* -Y face */ { {-0.5f,-0.5f,+0.5f},{0,-1,0} }, { {-0.5f,-0.5f,-0.5f},{0,-1,0} }, { {+0.5f,-0.5f,-0.5f},{0,-1,0} }, { {+0.5f,-0.5f,+0.5f},{0,-1,0} },
        /* +Z face */ { {-0.5f,-0.5f,+0.5f},{0,0,+1} }, { {+0.5f,-0.5f,+0.5f},{0,0,+1} }, { {+0.5f,+0.5f,+0.5f},{0,0,+1} }, { {-0.5f,+0.5f,+0.5f},{0,0,+1} },
        /* -Z face */ { {+0.5f,-0.5f,-0.5f},{0,0,-1} }, { {-0.5f,-0.5f,-0.5f},{0,0,-1} }, { {-0.5f,+0.5f,-0.5f},{0,0,-1} }, { {+0.5f,+0.5f,-0.5f},{0,0,-1} }
        });

    idx = {
        0,1,2, 0,2,3,       // +X
        4,5,6, 4,6,7,       // -X
        8,9,10, 8,10,11,    // +Y
        12,13,14, 12,14,15, // -Y
        16,17,18, 16,18,19, // +Z
        20,21,22, 20,22,23  // -Z
    };
}

/* ------------------------------------------------------------------------- */
/* buildIcoSphere – very small helper; subdivisions = 0..3 is enough        */
/* ------------------------------------------------------------------------- */
inline void buildIcoSphere(std::vector<Vertex>& verts,
    std::vector<uint32_t>& idx,
    int subdivisions = 0)
{
    /* ---- golden-ratio icosahedron -------------------------------------- */
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    const float s = 1.0f / std::sqrt(1 + t * t);

    std::vector<glm::vec3> base = {
        { -1,  t,  0 }, {  1,  t,  0 }, { -1, -t,  0 }, {  1, -t,  0 },
        {  0, -1,  t }, {  0,  1,  t }, {  0, -1, -t }, {  0,  1, -t },
        {  t,  0, -1 }, {  t,  0,  1 }, { -t,  0, -1 }, { -t,  0,  1 }
    };
    for (auto& v : base) v = glm::normalize(v) * 0.5f;   // radius 0.5

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
        glm::vec3 mid = glm::normalize(base[a] + base[b]) * 0.5f;
        base.push_back(mid);
        uint32_t idx = uint32_t(base.size() - 1);
        midpoint[key] = idx;
        return idx;
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
    verts.resize(base.size());
    for (size_t i = 0; i < base.size(); ++i) {
        verts[i].position = base[i];
        verts[i].normal = glm::normalize(base[i]);
    }
    idx.assign(faces.begin(), faces.end());
}

/* ------------------------------------------------------------------------- */
/* createArrowPrimitive - simple 3D arrow along +Z for instancing           */
/* ------------------------------------------------------------------------- */
inline void createArrowPrimitive(std::vector<Vertex>& vertices, std::vector<unsigned int>& indices)
{
    vertices.clear();
    indices.clear();

    // Defines the proportions of the arrow primitive.
    float shaftRadius = 0.05f;
    float headRadius = 0.12f;
    float headLength = 0.35f;
    float totalLength = 1.0f;
    float shaftLength = totalLength - headLength;
    float tipZ = totalLength / 2.0f;
    float headBaseZ = tipZ - headLength;
    float shaftBaseZ = -totalLength / 2.0f;

    // Arrow Tip (a simple cone)
    vertices.push_back({ {0.0f, 0.0f, tipZ}, {0.0f, 0.0f, 1.0f} }); // 0: Tip vertex
    vertices.push_back({ {headRadius, 0.0f, headBaseZ}, {0.8f, 0.0f, 0.2f} });  // 1
    vertices.push_back({ {0.0f, headRadius, headBaseZ}, {0.0f, 0.8f, 0.2f} });  // 2
    vertices.push_back({ {-headRadius, 0.0f, headBaseZ}, {-0.8f, 0.0f, 0.2f} }); // 3
    vertices.push_back({ {0.0f, -headRadius, headBaseZ}, {0.0f, -0.8f, 0.2f} }); // 4

    indices.insert(indices.end(), { 0, 1, 2 });
    indices.insert(indices.end(), { 0, 2, 3 });
    indices.insert(indices.end(), { 0, 3, 4 });
    indices.insert(indices.end(), { 0, 4, 1 });
    indices.insert(indices.end(), { 1, 4, 3, 1, 3, 2 }); // Base of the cone

    // Arrow Shaft (a simple cylinder)
    vertices.push_back({ {shaftRadius, 0.0f, headBaseZ}, {1.0f, 0.0f, 0.0f} });    // 5
    vertices.push_back({ {0.0f, shaftRadius, headBaseZ}, {0.0f, 1.0f, 0.0f} });    // 6
    vertices.push_back({ {-shaftRadius, 0.0f, headBaseZ}, {-1.0f, 0.0f, 0.0f} });   // 7
    vertices.push_back({ {0.0f, -shaftRadius, headBaseZ}, {0.0f, -1.0f, 0.0f} });   // 8

    vertices.push_back({ {shaftRadius, 0.0f, shaftBaseZ}, {1.0f, 0.0f, 0.0f} });   // 9
    vertices.push_back({ {0.0f, shaftRadius, shaftBaseZ}, {0.0f, 1.0f, 0.0f} });   // 10
    vertices.push_back({ {-shaftRadius, 0.0f, shaftBaseZ}, {-1.0f, 0.0f, 0.0f} });  // 11
    vertices.push_back({ {0.0f, -shaftRadius, shaftBaseZ}, {0.0f, -1.0f, 0.0f} });  // 12

    indices.insert(indices.end(), { 5, 9, 10, 5, 10, 6 });
    indices.insert(indices.end(), { 6, 10, 11, 6, 11, 7 });
    indices.insert(indices.end(), { 7, 11, 12, 7, 12, 8 });
    indices.insert(indices.end(), { 8, 12, 9, 8, 9, 5 });
}
