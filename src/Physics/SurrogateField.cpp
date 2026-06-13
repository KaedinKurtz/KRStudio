// Phase 5 Task 5 — surrogate scaffold: factory (Null active; ONNX behind
// KR_WITH_ONNX) + FEM training-data exporter. FEM stays the source of truth.
#include "SurrogateField.hpp"

#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <cstdint>

#ifdef KR_WITH_ONNX
#include <onnxruntime_cxx_api.h>
#endif

namespace krs::fem {

#ifdef KR_WITH_ONNX
// ONNX-backed surrogate. STUB: wires the Ort::Session lifecycle so a model trained
// on the FEM oracle (e.g. a 3D CNN/U-Net over the voxel grid; ROADMAP §L.3) can be
// dropped in. Not active until onnxruntime is added to vcpkg.json and a model path
// is provided. Compiled out by default (KR_WITH_ONNX undefined).
class OnnxSurrogate : public ISurrogateField {
public:
    explicit OnnxSurrogate(const std::string& path) {
        try {
            m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "krs-fem-surrogate");
            Ort::SessionOptions so; so.SetIntraOpNumThreads(1);
            std::wstring wpath(path.begin(), path.end());
            m_session = std::make_unique<Ort::Session>(*m_env, wpath.c_str(), so);
            m_ok = true;
        } catch (...) { m_ok = false; }
    }
    bool available() const override { return m_ok; }
    bool predictVonMises(const VoxelFemModel&, const FemMaterial&, const ElasticBC&,
                         std::vector<double>&) override {
        // TODO: pack the input feature grid (see exportTrainingSample channels),
        // run m_session->Run, unpack the per-node/voxel field. Until a trained
        // model exists, signal unavailable so the caller uses the FEM oracle.
        return false;
    }
private:
    std::unique_ptr<Ort::Env> m_env;
    std::unique_ptr<Ort::Session> m_session;
    bool m_ok = false;
};
#endif

std::unique_ptr<ISurrogateField> makeSurrogate(const std::string& onnxModelPath) {
#ifdef KR_WITH_ONNX
    if (!onnxModelPath.empty()) {
        auto s = std::make_unique<OnnxSurrogate>(onnxModelPath);
        if (s->available()) return s;
    }
#else
    (void)onnxModelPath;
#endif
    return std::make_unique<NullSurrogate>();  // FEM oracle is the source of truth
}

void exportTrainingSample(const std::string& dir, int index,
                          const VoxelFemModel& m, const FemMaterial& mat,
                          const ElasticBC& ebc, const std::vector<int>& sourceCells,
                          const ElasticResult& er, const ThermalResult& tr) {
    if (!m.valid() || dir.empty()) return;
    std::error_code ec; std::filesystem::create_directories(dir, ec);
    // One-time format description.
    const std::string fmt = dir + "/FORMAT.txt";
    if (!std::filesystem::exists(fmt)) {
        std::ofstream f(fmt);
        f << "KRStudio FEM training samples (Phase 5).\n"
             "Each fem_sample_<n>.bin: little-endian\n"
             "  char[8] magic = 'KRSFEM01'\n  int32 nx, ny, nz\n  float64 h (cell size, m)\n"
             "  int32 inChannels (=8), outChannels (=3)\n"
             "  float32 input[inChannels*nx*ny*nz]   (channel-major, then z,y,x; per CELL)\n"
             "  float32 output[outChannels*nx*ny*nz]\n"
             "input channels : 0 occupancy, 1 E/200e9, 2 nu, 3 k/400, 4 cp/1000,\n"
             "                 5 fixed-mask, 6 load-mask, 7 heat-source-mask\n"
             "output channels: 0 vonMises(Pa), 1 temperature(C), 2 strain-norm\n"
             "Recommended model: 3D CNN / U-Net per-voxel regressor (ROADMAP L.3).\n";
    }
    const int nx = m.nx, ny = m.ny, nz = m.nz, nc = nx * ny * nz;
    const int IN = 8, OUT = 3;
    std::vector<float> in(size_t(IN) * nc, 0.0f), out(size_t(OUT) * nc, 0.0f);
    std::unordered_set<int> fixedN(ebc.fixedNodes.begin(), ebc.fixedNodes.end());
    std::unordered_set<int> loadN; for (const auto& nf : ebc.nodalForces) loadN.insert(nf.first);
    std::unordered_set<int> srcC(sourceCells.begin(), sourceCells.end());
    auto cidx = [&](int i, int j, int k) { return (k * ny + j) * nx + i; };
    for (int k = 0; k < nz; ++k) for (int j = 0; j < ny; ++j) for (int i = 0; i < nx; ++i) {
        const int ci = cidx(i, j, k);
        int corner[8]; bool solid = true;
        for (int a = 0; a < 8; ++a) {
            const int di = a & 1, dj = (a >> 1) & 1, dk = (a >> 2) & 1;
            corner[a] = m.nodeId[m.gridIdx(i + di, j + dj, k + dk)];
            if (corner[a] < 0) solid = false;
        }
        if (!solid) continue;
        in[0 * nc + ci] = 1.0f;
        in[1 * nc + ci] = float(mat.E / 200.0e9);
        in[2 * nc + ci] = float(mat.nu);
        in[3 * nc + ci] = float(mat.k / 400.0);
        in[4 * nc + ci] = float(mat.cp / 1000.0);
        bool fx = false, ld = false;
        for (int a = 0; a < 8; ++a) { if (fixedN.count(corner[a])) fx = true; if (loadN.count(corner[a])) ld = true; }
        in[5 * nc + ci] = fx ? 1.0f : 0.0f;
        in[6 * nc + ci] = ld ? 1.0f : 0.0f;
        in[7 * nc + ci] = srcC.count(ci) ? 1.0f : 0.0f;
        // Cell-centre value = mean of the 8 corner nodal values (trilinear at centre).
        double vm = 0, tp = 0, st = 0;
        for (int a = 0; a < 8; ++a) {
            const int n = corner[a];
            if (!er.vonMises.empty()) vm += er.vonMises[n];
            if (!er.strainNorm.empty()) st += er.strainNorm[n];
            tp += tr.temperature.empty() ? 20.0 : tr.temperature[n];
        }
        out[0 * nc + ci] = float(vm / 8.0);
        out[1 * nc + ci] = float(tp / 8.0);
        out[2 * nc + ci] = float(st / 8.0);
    }
    std::ofstream b(dir + "/fem_sample_" + std::to_string(index) + ".bin", std::ios::binary);
    if (!b) return;
    const char magic[8] = { 'K','R','S','F','E','M','0','1' };
    b.write(magic, 8);
    const std::int32_t dims[3] = { nx, ny, nz };
    b.write(reinterpret_cast<const char*>(dims), sizeof(dims));
    const double h = m.h; b.write(reinterpret_cast<const char*>(&h), sizeof(double));
    const std::int32_t ch[2] = { IN, OUT }; b.write(reinterpret_cast<const char*>(ch), sizeof(ch));
    b.write(reinterpret_cast<const char*>(in.data()), std::streamsize(sizeof(float) * in.size()));
    b.write(reinterpret_cast<const char*>(out.data()), std::streamsize(sizeof(float) * out.size()));
}

} // namespace krs::fem
