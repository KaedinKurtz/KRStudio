// FieldVisualizerGate.cpp -- Phase 3: prove the (revived) FieldVisualizerPass arrow
// field is DATA, not decoration. Dispatches the REAL arrow_field_compute shader
// (field_visualizer_comp.glsl) over a sample grid with a known effector field, reads
// back the InstanceData arrows, and decodes each arrow's field vector straight out of
// its model matrix: modelMatrix = T(worldPos) * R(+Z -> fieldDir) * S(hs,hs,|f|*vs), so
// worldPos = column 3 and field = column 2 / vectorScale. VISUALIZER-DATA: the decoded
// arrow vectors (direction + magnitude) match the analytic effector field at each
// arrow's position to <tol, and the arrow COUNT equals the points above the culling
// threshold. NEG-CTRL: the SAME arrows, compared against a MOVED field, mismatch -- a
// stale visualizer (not recomputed after the field changed) shows the wrong field.
#include "RenderingSystem.hpp"
#include "GpuResources.hpp"     // PointEffectorGpu / DirectionalEffectorGpu / TriangleGpu
#include "GpuTypes.hpp"         // InstanceData
#include "components.hpp"       // DrawElementsIndirectCommand
#include "Shader.hpp"

#include <QOpenGLFunctions_4_3_Core>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>

namespace {
// the analytic effector field -- a CPU mirror of field_visualizer_comp.glsl's summation
// (one linear-falloff point effector + one directional effector). This is the "actual field"
// the arrows must encode.
struct PointCfg { glm::vec3 pos; float strength; float radius; };
glm::vec3 analyticField(const glm::vec3& worldPos, const PointCfg& pe, const glm::vec3& dirN, float dirStrength) {
    glm::vec3 total(0.0f);
    glm::vec3 diff = worldPos - pe.pos;
    float dist = glm::length(diff);
    if (dist < pe.radius && dist > 0.001f) {
        float s = pe.strength * (1.0f - dist / pe.radius);  // FalloffType::Linear (==1)
        total += glm::normalize(diff) * s;                  // normal==0 -> radial
    }
    total += dirN * dirStrength;                            // directional (already normalized)
    return total;
}
}

bool RenderingSystem::runFieldVisualizerGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[fieldvis] GATE VISUALIZER-DATA -- the arrow field encodes the REAL effector field (stale neg-ctrl)\n");

    Shader* compute = getShader("arrow_field_compute");
    if (!compute || !m_gl) { printf("[fieldvis] FAIL: arrow_field_compute shader / GL context missing\n"); std::fflush(stdout); return false; }
    QOpenGLFunctions_4_3_Core* gl = m_gl;

    // ---- fixed sample grid (matches FieldVisualizerPass::renderArrows generation) ----
    const glm::ivec3 density(5, 5, 5);
    const glm::vec3 bmin(-1.0f), bmax(1.0f);
    const glm::vec3 bsize = bmax - bmin;
    std::vector<glm::vec4> samplePoints;
    for (int x = 0; x < density.x; ++x)
        for (int y = 0; y < density.y; ++y)
            for (int z = 0; z < density.z; ++z) {
                glm::vec3 t((density.x > 1) ? float(x) / (density.x - 1) : 0.5f,
                            (density.y > 1) ? float(y) / (density.y - 1) : 0.5f,
                            (density.z > 1) ? float(z) / (density.z - 1) : 0.5f);
                samplePoints.emplace_back(glm::vec4(bmin + t * bsize, 1.0f));
            }
    const int numPts = int(samplePoints.size());
    const float vectorScale = 1.0f, headScale = 0.5f, cullThreshold = 0.01f;
    const glm::vec3 dirN = glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f));
    const float dirStrength = 0.5f;

    // ---- GPU buffers ----
    GLuint sampleSSBO = 0, instanceSSBO = 0, cmdSSBO = 0, effUBO = 0, triSSBO = 0;
    gl->glGenBuffers(1, &sampleSSBO);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, sampleSSBO);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(sizeof(glm::vec4)) * numPts, samplePoints.data(), GL_STATIC_DRAW);
    gl->glGenBuffers(1, &instanceSSBO);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, instanceSSBO);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(sizeof(InstanceData)) * numPts, nullptr, GL_DYNAMIC_DRAW);
    gl->glGenBuffers(1, &cmdSSBO);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, cmdSSBO);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(DrawElementsIndirectCommand), nullptr, GL_DYNAMIC_DRAW);
    gl->glGenBuffers(1, &effUBO);
    gl->glBindBuffer(GL_UNIFORM_BUFFER, effUBO);
    gl->glBufferData(GL_UNIFORM_BUFFER, 256 * sizeof(PointEffectorGpu) + 16 * sizeof(DirectionalEffectorGpu), nullptr, GL_DYNAMIC_DRAW);
    gl->glGenBuffers(1, &triSSBO);     // bound but unused (count 0); keep the binding valid
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, triSSBO);
    gl->glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(TriangleGpu), nullptr, GL_DYNAMIC_DRAW);
    gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    gl->glBindBuffer(GL_UNIFORM_BUFFER, 0);

    // dispatch arrow_field_compute for a given point-effector config; return the decoded arrows.
    struct Arrow { glm::vec3 pos; glm::vec3 field; float intensity; };
    auto dispatch = [&](const PointCfg& pe, std::vector<Arrow>& out) {
        // upload the effector UBO (1 point + 1 directional), matching uploadEffectorData's layout.
        PointEffectorGpu p{}; p.position = glm::vec4(pe.pos, 1.0f); p.normal = glm::vec4(0.0f);
        p.strength = pe.strength; p.radius = pe.radius; p.falloffType = 1; p.padding = 0.0f;
        DirectionalEffectorGpu d{}; d.direction = glm::vec4(dirN, 0.0f); d.strength = dirStrength;
        gl->glBindBuffer(GL_UNIFORM_BUFFER, effUBO);
        gl->glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(PointEffectorGpu), &p);
        gl->glBufferSubData(GL_UNIFORM_BUFFER, 256 * sizeof(PointEffectorGpu), sizeof(DirectionalEffectorGpu), &d);
        gl->glBindBuffer(GL_UNIFORM_BUFFER, 0);
        // reset the indirect-draw instance counter (the shader atomicAdds into instanceCount).
        DrawElementsIndirectCommand cmd{ 36u, 0u, 0u, 0u, 0u };
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, cmdSSBO);
        gl->glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(cmd), &cmd);
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        compute->use(gl);
        gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, sampleSSBO);
        gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, instanceSSBO);
        gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, cmdSSBO);
        gl->glBindBufferBase(GL_UNIFORM_BUFFER, 3, effUBO);
        gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, triSSBO);
        compute->setMat4(gl, "u_visualizerModelMatrix", glm::mat4(1.0f));
        compute->setFloat(gl, "u_vectorScale", vectorScale);
        compute->setFloat(gl, "u_arrowHeadScale", headScale);
        compute->setFloat(gl, "u_cullingThreshold", cullThreshold);
        compute->setInt(gl, "u_pointEffectorCount", 1);
        compute->setInt(gl, "u_directionalEffectorCount", 1);
        compute->setInt(gl, "u_triangleEffectorCount", 0);
        gl->glDispatchCompute(GLuint(numPts) / 256 + 1, 1, 1);
        gl->glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
        gl->glFinish();

        // read the produced instance count + arrows.
        DrawElementsIndirectCommand got{};
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, cmdSSBO);
        gl->glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(got), &got);
        const int n = std::min(int(got.instanceCount), numPts);
        std::vector<InstanceData> inst; inst.resize(size_t(numPts));
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, instanceSSBO);
        gl->glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(sizeof(InstanceData)) * numPts, inst.data());
        gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        out.clear();
        for (int i = 0; i < n; ++i) {
            Arrow a;
            a.pos   = glm::vec3(inst[i].modelMatrix[3]);          // column 3 = T
            a.field = glm::vec3(inst[i].modelMatrix[2]) / vectorScale;  // column 2 = field * vectorScale
            a.intensity = inst[i].color.x;
            out.push_back(a);
        }
    };

    // ---- config A (fresh) ----
    const PointCfg A{ glm::vec3(0.3f, 0.0f, 0.0f), 2.0f, 2.0f };
    std::vector<Arrow> arrowsA; dispatch(A, arrowsA);

    // expected arrow count: grid points whose analytic field magnitude clears the cull threshold.
    int expectN = 0;
    for (const auto& sp : samplePoints)
        if (glm::length(analyticField(glm::vec3(sp), A, dirN, dirStrength)) > cullThreshold) ++expectN;

    // VISUALIZER-DATA: decoded arrow vectors match the analytic field at their own position.
    double maxDirErr = 0.0, maxMagRel = 0.0, maxIntErr = 0.0;
    for (const auto& a : arrowsA) {
        const glm::vec3 ana = analyticField(a.pos, A, dirN, dirStrength);
        const double anaMag = glm::length(ana), encMag = glm::length(a.field);
        if (anaMag > 1e-4 && encMag > 1e-4)
            maxDirErr = std::max(maxDirErr, 1.0 - double(glm::dot(glm::normalize(a.field), glm::normalize(ana))));
        maxMagRel = std::max(maxMagRel, std::abs(encMag - anaMag) / std::max(anaMag, 1e-3));
        const double expectInt = std::min(std::abs(anaMag) / 5.0, 1.0);
        maxIntErr = std::max(maxIntErr, std::abs(double(a.intensity) - expectInt));
    }
    const bool countOk = int(arrowsA.size()) == expectN && expectN > 0;
    const bool dirOk = maxDirErr < 0.01;     // dot > 0.99
    const bool magOk = maxMagRel < 0.02;     // <2% magnitude error
    const bool intOk = maxIntErr < 0.02;
    const bool dataOk = countOk && dirOk && magOk && intOk;
    printf("[fieldvis]   VISUALIZER-DATA: %d arrows (expect %d above cull, count:%d); max dir err=%.4f (dot>0.99:%d); "
           "max |mag| relErr=%.4f (<2%%:%d); max intensity err=%.4f (ok:%d)  %s\n",
           int(arrowsA.size()), expectN, int(countOk), maxDirErr, int(dirOk), maxMagRel, int(magOk), maxIntErr, int(intOk),
           dataOk ? "PASS" : "FAIL");

    // ---- config B (the field MOVED): fresh-B arrows track, stale-A arrows do not ----
    const PointCfg B{ glm::vec3(-0.6f, 0.0f, 0.0f), 2.0f, 2.0f };
    std::vector<Arrow> arrowsB; dispatch(B, arrowsB);
    double maxDirErrB = 0.0;     // fresh-B arrows vs analytic-B (should match)
    for (const auto& a : arrowsB) {
        const glm::vec3 ana = analyticField(a.pos, B, dirN, dirStrength);
        if (glm::length(ana) > 1e-4 && glm::length(a.field) > 1e-4)
            maxDirErrB = std::max(maxDirErrB, 1.0 - double(glm::dot(glm::normalize(a.field), glm::normalize(ana))));
    }
    // NEG-CTRL: the STALE config-A arrows compared against the MOVED (B) field -- a real
    // failing model (the actual A arrows), so a non-recomputed visualizer mismatches.
    double maxStaleDirErr = 0.0;
    for (const auto& a : arrowsA) {
        const glm::vec3 anaB = analyticField(a.pos, B, dirN, dirStrength);
        if (glm::length(anaB) > 1e-4 && glm::length(a.field) > 1e-4)
            maxStaleDirErr = std::max(maxStaleDirErr, 1.0 - double(glm::dot(glm::normalize(a.field), glm::normalize(anaB))));
    }
    const bool freshBOk = maxDirErrB < 0.01;
    const bool staleMismatch = maxStaleDirErr > 0.1;   // the stale arrows clearly disagree with the moved field
    const bool negOk = freshBOk && staleMismatch;
    printf("[fieldvis]   STALE NEG-CTRL: fresh-B arrows track moved field (max dir err=%.4f, ok:%d); "
           "STALE-A arrows vs moved field max dir err=%.4f (>0.1 mismatch:%d)  %s\n",
           maxDirErrB, int(freshBOk), maxStaleDirErr, int(staleMismatch), negOk ? "PASS" : "FAIL");

    // cleanup
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, 0);
    gl->glBindBufferBase(GL_UNIFORM_BUFFER, 3, 0);
    gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, 0);
    GLuint bufs[] = { sampleSSBO, instanceSSBO, cmdSSBO, effUBO, triSSBO };
    gl->glDeleteBuffers(5, bufs);

    const bool allOk = dataOk && negOk;
    printf("[fieldvis] %s\n", allOk ? "ALL PASS (arrow field == analytic effector field; fresh tracks, stale mismatches)"
                                    : "FAILURES PRESENT");
    std::fflush(stdout);
    return allOk;
}
