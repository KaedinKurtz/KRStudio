#pragma once

#include <glm/glm.hpp>
#include <qopengl.h> // For GLuint

// --- GPU-Aligned Data Structures for Uniform Buffers ---

struct PointEffectorGpu {
    glm::vec4 position;
    glm::vec4 normal;
    float strength;
    float radius;
    int falloffType;
    float padding;
};

struct DirectionalEffectorGpu {
    glm::vec4 direction;
    float strength;
    float padding1, padding2, padding3;
};

// This struct represents a single triangle on the GPU, used for mesh effectors.
// It holds the world-space positions of its vertices and its face normal.
// The 'w' components of v0 and normal are used to pack extra data (strength/radius).
struct TriangleGpu {
    glm::vec4 v0;
    glm::vec4 v1;
    glm::vec4 v2;
    glm::vec4 normal;
};


// --- GPU Resource Handles ---
// This struct now only contains data unique to each visualizer grid.
struct FieldVisGpuData {
    GLuint samplePointsSSBO = 0;
    GLuint instanceDataSSBO = 0;
    GLuint commandUBO = 0;
    int numSamplePoints = 0;
};
