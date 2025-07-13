#pragma once // Prevents the file from being included multiple times

#include <glm/glm.hpp>

// This struct defines the layout for the instance data that the compute shader
// generates and the vertex shader consumes for drawing arrows.
// Its layout MUST exactly match the 'InstanceData' struct in the compute shader,
// including any padding, to ensure correct data alignment on the GPU.
struct InstanceData {
    glm::mat4 modelMatrix; // 64 bytes (4x4 matrix of floats)
    glm::vec4 color;       // 16 bytes (4 floats for color/intensity/age)
    glm::vec4 padding;     // 16 bytes (explicit padding to match std430 rules)
};
// TOTAL SIZE: 64 + 16 + 16 = 96 bytes