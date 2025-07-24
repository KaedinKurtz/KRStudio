#pragma once

#include <glm/glm.hpp>

struct GridLevel {
    float spacing;
    glm::vec3 color;
    float fadeInCameraDistanceEnd;
    float fadeInCameraDistanceStart;

    GridLevel(float s, glm::vec3 c, float fadeInEnd, float fadeInStart)
        : spacing(s), color(c), fadeInCameraDistanceEnd(fadeInEnd), fadeInCameraDistanceStart(fadeInStart) {}

    GridLevel()
        : spacing(0.0f), color(0.0f), fadeInCameraDistanceEnd(0.0f), fadeInCameraDistanceStart(0.0f) {
    }
};
