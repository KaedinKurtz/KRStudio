#pragma once
#include "components.hpp" // For SplineComponent
#include <glm/glm.hpp>
#include <vector>
#include <functional>

namespace SplineUtils {

    // (The four evaluate's are free functions within this namespace)

    static std::vector<glm::vec3> evaluateCatmullRom(const std::vector<glm::vec3>& controlPoints, int segmentsPerCurve) {
        std::vector<glm::vec3> lineVertices;
        if (controlPoints.size() < 4) { // A segment requires at least 4 control points.
            return lineVertices;
        }

        lineVertices.reserve(static_cast<size_t>(controlPoints.size() - 3) * segmentsPerCurve); // Pre-allocate memory for efficiency.

        // Iterate through each 4-point segment of the spline.
        for (size_t i = 0; i < controlPoints.size() - 3; ++i) {
            const glm::vec3& p0 = controlPoints[i];
            const glm::vec3& p1 = controlPoints[i + 1];
            const glm::vec3& p2 = controlPoints[i + 2];
            const glm::vec3& p3 = controlPoints[i + 3];

            // Generate the points for the current segment.
            for (int j = 0; j < segmentsPerCurve; ++j) {
                float t = static_cast<float>(j) / (segmentsPerCurve - 1); // Parameter t from 0 to 1.
                float t2 = t * t;
                float t3 = t2 * t;

                // Catmull-Rom interpolation formula.
                glm::vec3 point = 0.5f * (
                    (2.0f * p1) +
                    (-p0 + p2) * t +
                    (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                    (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
                    );
                lineVertices.push_back(point);
            }
        }
        return lineVertices;
    }

    static std::vector<glm::vec3> evaluateLinear(const std::vector<glm::vec3>& controlPoints) {
        return controlPoints;
    }

    static std::vector<glm::vec3> evaluateBezier(const std::vector<glm::vec3>& controlPoints, int numSegments) {
        std::vector<glm::vec3> lineVertices;
        if (controlPoints.empty()) {
            return lineVertices;
        }
        lineVertices.reserve(numSegments); // Pre-allocate memory.

        // Lambda to calculate binomial coefficients (nCk).
        auto binomialCoeff = [](int n, int k) {
            long long res = 1;
            if (k > n - k) k = n - k;
            for (int i = 0; i < k; ++i) {
                res = res * (n - i);
                res = res / (i + 1);
            }
            return static_cast<float>(res);
            };

        int n = static_cast<int>(controlPoints.size()) - 1; // Degree of the curve.
        for (int i = 0; i < numSegments; ++i) {
            float t = static_cast<float>(i) / (numSegments - 1); // Parameter t from 0 to 1.
            glm::vec3 point(0.0f);
            // Sum up the control points weighted by the Bernstein polynomials.
            for (int j = 0; j <= n; ++j) {
                float bernstein = binomialCoeff(n, j) * pow(t, j) * pow(1 - t, n - j);
                point += controlPoints[j] * bernstein;
            }
            lineVertices.push_back(point);
        }
        return lineVertices;
    }

    static std::vector<glm::vec3> evaluateParametric(const std::function<glm::vec3(float)>& func, int numSegments) {
        std::vector<glm::vec3> lineVertices;
        if (!func) { // Ensure the function object is valid.
            return lineVertices;
        }
        lineVertices.reserve(numSegments);
        for (int i = 0; i < numSegments; ++i) {
            float t = static_cast<float>(i) / (numSegments - 1); // Parameter t from 0 to 1.
            lineVertices.push_back(func(t)); // Evaluate the function at t.
        }
        return lineVertices;
    }

    // A convenience dispatcher function that takes a SplineComponent
    // and calls the correct evaluation function.
    static void updateCache(SplineComponent& spline) {
        switch (spline.type) {
        case SplineType::Linear:
            spline.cachedVertices = evaluateLinear(spline.controlPoints);
            break;
        case SplineType::CatmullRom:
            spline.cachedVertices = evaluateCatmullRom(spline.controlPoints, 64);
            break;
        case SplineType::Bezier:
            spline.cachedVertices = evaluateBezier(spline.controlPoints, 64);
            break;
        case SplineType::Parametric:
            spline.cachedVertices = evaluateParametric(spline.parametric.func, 128);
            break;
        }
        spline.isDirty = false; // Mark as clean after updating
    }

} // namespace SplineUtils