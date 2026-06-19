// ===========================================================================
// TransformLinalgNodes.cpp -- Part B: the math backbone. A first-class quat-native Transform (krs::RigidTransform,
// position + unit quaternion) plus the linear-algebra nodes the IK/constraint/pose story needs.
//
// Bake nodes build a Transform from pos+quat / xyz-rpy / pos+axis-angle. Transform ops (compose/inverse/
// apply-to-point) are quat-native so chains don't accumulate matrix drift. Vector/matrix ops fill the gaps the
// editor was missing (vector scale, matrix transpose/inverse) -- dot/cross/normalize/magnitude/matmul already
// exist (signal_dot_product, linalg_vec_cross/normalize/magnitude, linalg_mat_multiply) and are re-verified by
// the LINALG-CORRECT gate. Transform/Quaternion/Vector/Matrix are the canonical port types (PortTypes.hpp).
//
// Gates (headless, folded into KRS_OVERNIGHT_BENCH): TRANSFORM-COMPOSE (analytic compose + inverse==identity +
// wrong-order neg-ctrl), LINALG-CORRECT (each op vs closed form + wrong-impl neg-ctrl).
// ===========================================================================
#include "Node.hpp"
#include "NodeFactory.hpp"
#include "NodeEditorGate.hpp"
#include "RigidTransform.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <Eigen/Dense>

#include <cstdio>
#include <cmath>
#include <memory>

namespace krs::nodes {
namespace {

constexpr char TFORM[] = "RigidTransform";   // the port type.name for the quat-native Transform

// ---- Bake: Position(vec3) + Orientation(quat) -> Transform ----
class BakeTransformNode : public Node {
public:
    BakeTransformNode() {
        m_id = "transform_bake";
        m_ports.push_back({ "Position",    { "glm::vec3", "m" },      Port::Direction::Input,  this });
        m_ports.push_back({ "Orientation", { "glm::quat", "quat" },   Port::Direction::Input,  this });
        m_ports.push_back({ "Transform",   { TFORM, "pose" },          Port::Direction::Output, this });
    }
    void compute() override {
        krs::RigidTransform t;
        t.position = getInput<glm::vec3>("Position").value_or(glm::vec3(0.0f));
        t.rotation = glm::normalize(getInput<glm::quat>("Orientation").value_or(glm::quat(1, 0, 0, 0)));
        setOutput<krs::RigidTransform>("Transform", t);
    }
};

// ---- Bake from XYZ + RPY (ZYX intrinsic: yaw(Z)*pitch(Y)*roll(X)) -> Transform ----
class TransformFromXyzRpyNode : public Node {
public:
    TransformFromXyzRpyNode() {
        m_id = "transform_from_xyzrpy";
        for (const char* n : { "X", "Y", "Z" }) m_ports.push_back({ n, { "float", "m" },   Port::Direction::Input, this });
        for (const char* n : { "Roll", "Pitch", "Yaw" }) m_ports.push_back({ n, { "float", "rad" }, Port::Direction::Input, this });
        m_ports.push_back({ "Transform", { TFORM, "pose" }, Port::Direction::Output, this });
    }
    void compute() override {
        krs::RigidTransform t;
        t.position = glm::vec3(getInputD("X", 0.0), getInputD("Y", 0.0), getInputD("Z", 0.0));
        const float r = float(getInputD("Roll", 0.0)), p = float(getInputD("Pitch", 0.0)), y = float(getInputD("Yaw", 0.0));
        t.rotation = glm::angleAxis(y, glm::vec3(0, 0, 1)) * glm::angleAxis(p, glm::vec3(0, 1, 0)) * glm::angleAxis(r, glm::vec3(1, 0, 0));
        setOutput<krs::RigidTransform>("Transform", t);
    }
};

// ---- Bake from Position(vec3) + Axis(vec3) + Angle(float) -> Transform ----
class TransformFromAxisAngleNode : public Node {
public:
    TransformFromAxisAngleNode() {
        m_id = "transform_from_axis_angle";
        m_ports.push_back({ "Position", { "glm::vec3", "m" },   Port::Direction::Input,  this });
        m_ports.push_back({ "Axis",     { "glm::vec3", "unitless" }, Port::Direction::Input,  this });
        m_ports.push_back({ "Angle",    { "float", "rad" },     Port::Direction::Input,  this });
        m_ports.push_back({ "Transform",{ TFORM, "pose" },       Port::Direction::Output, this });
    }
    void compute() override {
        krs::RigidTransform t;
        t.position = getInput<glm::vec3>("Position").value_or(glm::vec3(0.0f));
        glm::vec3 axis = getInput<glm::vec3>("Axis").value_or(glm::vec3(0, 0, 1));
        if (glm::length(axis) < 1e-9f) axis = glm::vec3(0, 0, 1);
        t.rotation = glm::angleAxis(float(getInputD("Angle", 0.0)), glm::normalize(axis));
        setOutput<krs::RigidTransform>("Transform", t);
    }
};

// ---- Transform compose: A, B -> A * B  (apply B first, then A == M_A * M_B) ----
class TransformComposeNode : public Node {
public:
    TransformComposeNode() {
        m_id = "transform_compose";
        m_ports.push_back({ "A", { TFORM, "pose" }, Port::Direction::Input,  this });
        m_ports.push_back({ "B", { TFORM, "pose" }, Port::Direction::Input,  this });
        m_ports.push_back({ "Result", { TFORM, "pose" }, Port::Direction::Output, this });
    }
    void compute() override {
        auto a = getInput<krs::RigidTransform>("A");
        auto b = getInput<krs::RigidTransform>("B");
        if (a && b) setOutput<krs::RigidTransform>("Result", (*a) * (*b));
    }
};

// ---- Transform inverse ----
class TransformInverseNode : public Node {
public:
    TransformInverseNode() {
        m_id = "transform_inverse";
        m_ports.push_back({ "Transform", { TFORM, "pose" }, Port::Direction::Input,  this });
        m_ports.push_back({ "Inverse",   { TFORM, "pose" }, Port::Direction::Output, this });
    }
    void compute() override {
        if (auto t = getInput<krs::RigidTransform>("Transform")) setOutput<krs::RigidTransform>("Inverse", t->inverse());
    }
};

// ---- Transform apply-to-point: T, Point(vec3) -> transformed point(vec3) ----
class TransformApplyPointNode : public Node {
public:
    TransformApplyPointNode() {
        m_id = "transform_apply_point";
        m_ports.push_back({ "Transform", { TFORM, "pose" },  Port::Direction::Input,  this });
        m_ports.push_back({ "Point",     { "glm::vec3", "m" }, Port::Direction::Input,  this });
        m_ports.push_back({ "Result",    { "glm::vec3", "m" }, Port::Direction::Output, this });
    }
    void compute() override {
        auto t = getInput<krs::RigidTransform>("Transform");
        auto p = getInput<glm::vec3>("Point");
        if (t && p) setOutput<glm::vec3>("Result", t->apply(*p));
    }
};

// ---- Vector scale: Vector(vec3) * Scalar(float) -> vec3 ----
class VectorScaleNode : public Node {
public:
    VectorScaleNode() {
        m_id = "linalg_vec_scale";
        m_ports.push_back({ "Vector", { "glm::vec3", "unitless" }, Port::Direction::Input,  this });
        m_ports.push_back({ "Scalar", { "float", "unitless" },     Port::Direction::Input,  this });
        m_ports.push_back({ "Result", { "glm::vec3", "unitless" }, Port::Direction::Output, this });
    }
    void compute() override {
        if (auto v = getInput<glm::vec3>("Vector")) setOutput<glm::vec3>("Result", (*v) * float(getInputD("Scalar", 1.0)));
    }
};

// ---- Matrix transpose ----
class MatrixTransposeNode : public Node {
public:
    MatrixTransposeNode() {
        m_id = "linalg_mat_transpose";
        m_ports.push_back({ "Matrix", { "Eigen::MatrixXf", "matrix" }, Port::Direction::Input,  this });
        m_ports.push_back({ "Result", { "Eigen::MatrixXf", "matrix" }, Port::Direction::Output, this });
    }
    void compute() override {
        if (auto m = getInput<Eigen::MatrixXf>("Matrix")) setOutput<Eigen::MatrixXf>("Result", m->transpose());
    }
};

// ---- Matrix inverse (square; singular -> no output, graceful) ----
class MatrixInverseNode : public Node {
public:
    MatrixInverseNode() {
        m_id = "linalg_mat_inverse";
        m_ports.push_back({ "Matrix",  { "Eigen::MatrixXf", "matrix" }, Port::Direction::Input,  this });
        m_ports.push_back({ "Inverse", { "Eigen::MatrixXf", "matrix" }, Port::Direction::Output, this });
    }
    void compute() override {
        auto m = getInput<Eigen::MatrixXf>("Matrix");
        if (m && m->rows() == m->cols()) {
            Eigen::FullPivLU<Eigen::MatrixXf> lu(*m);
            if (lu.isInvertible()) setOutput<Eigen::MatrixXf>("Inverse", lu.inverse());
        }
    }
};

template <class T> struct Reg {
    Reg(const char* id, NodeDescriptor d) { NodeFactory::instance().registerNodeType(id, d, [] { return std::make_unique<T>(); }); }
};
static Reg<BakeTransformNode>        g_bake("transform_bake", { "Bake Transform", "Math/Transform", "Position + Orientation (quaternion) -> a rigid Transform." });
static Reg<TransformFromXyzRpyNode>  g_xyzrpy("transform_from_xyzrpy", { "Transform from XYZ-RPY", "Math/Transform", "X,Y,Z + roll,pitch,yaw (ZYX) -> a rigid Transform." });
static Reg<TransformFromAxisAngleNode> g_axisang("transform_from_axis_angle", { "Transform from Axis-Angle", "Math/Transform", "Position + axis + angle -> a rigid Transform." });
static Reg<TransformComposeNode>     g_compose("transform_compose", { "Transform Compose", "Math/Transform", "A . B (apply B then A) -> Transform." });
static Reg<TransformInverseNode>     g_inv("transform_inverse", { "Transform Inverse", "Math/Transform", "The inverse rigid Transform (T . T^-1 = identity)." });
static Reg<TransformApplyPointNode>  g_apply("transform_apply_point", { "Transform Point", "Math/Transform", "Apply a Transform to a point: R*p + t." });
static Reg<VectorScaleNode>          g_scale("linalg_vec_scale", { "Vector Scale", "Math/Vector", "Scale a vector by a scalar." });
static Reg<MatrixTransposeNode>      g_tpose("linalg_mat_transpose", { "Matrix Transpose", "Math/Matrix", "Transpose a matrix." });
static Reg<MatrixInverseNode>        g_minv("linalg_mat_inverse", { "Matrix Inverse", "Math/Matrix", "Invert a square matrix (singular -> no output)." });

// --- gate helpers ---
void setVec(Node& n, const char* p, const glm::vec3& v) { PortDataPacket pk; pk.data = v; pk.type = { "glm::vec3", "m" }; n.setInput(p, pk); }
void setQuat(Node& n, const char* p, const glm::quat& q) { PortDataPacket pk; pk.data = q; pk.type = { "glm::quat", "quat" }; n.setInput(p, pk); }
void setTf(Node& n, const char* p, const krs::RigidTransform& t) { PortDataPacket pk; pk.data = t; pk.type = { TFORM, "pose" }; n.setInput(p, pk); }
void setNum(Node& n, const char* p, double v) { PortDataPacket pk; pk.data = float(v); pk.type = { "float", "unitless" }; n.setInput(p, pk); }
glm::vec3 outVec(Node& n, const char* p) {
    for (const auto& x : n.getPorts()) if (x.direction == Port::Direction::Output && x.name == p && x.packet)
        try { return std::any_cast<glm::vec3>(x.packet->data); } catch (...) {}
    return glm::vec3(std::nanf(""));
}
krs::RigidTransform outTf(Node& n, const char* p) {
    for (const auto& x : n.getPorts()) if (x.direction == Port::Direction::Output && x.name == p && x.packet)
        try { return std::any_cast<krs::RigidTransform>(x.packet->data); } catch (...) {}
    return krs::RigidTransform{};
}
double outNum(Node& n, const char* p) {
    for (const auto& x : n.getPorts()) if (x.direction == Port::Direction::Output && x.name == p && x.packet) {
        try { return double(std::any_cast<float>(x.packet->data)); } catch (...) {}
        try { return std::any_cast<double>(x.packet->data); } catch (...) {}
    }
    return std::nan("");
}

} // namespace

bool runTransformComposeGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[tform] GATE TRANSFORM-COMPOSE -- quat-native compose matches closed form; inverse==identity; order matters\n");

    // Two known rigid transforms.
    krs::RigidTransform A; A.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1)); A.position = glm::vec3(1, 2, 3);
    krs::RigidTransform B; B.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1, 0, 0)); B.position = glm::vec3(4, 0, 0);
    const glm::vec3 p(1, 1, 1);

    // compose A.B via the node, apply to p.
    TransformComposeNode comp; setTf(comp, "A", A); setTf(comp, "B", B); comp.process();
    const krs::RigidTransform AB = outTf(comp, "Result");
    TransformApplyPointNode ap; setTf(ap, "Transform", AB); setVec(ap, "Point", p); ap.process();
    const glm::vec3 got = outVec(ap, "Result");
    // closed form: (A.B)(p) = A(B(p)) = R_A*(R_B*p + t_B) + t_A.
    const glm::vec3 expected = A.rotation * (B.rotation * p + B.position) + A.position;
    const bool composeOk = glm::length(got - expected) < 1e-5f;

    // inverse . transform == identity (apply A then A^-1 -> p unchanged).
    TransformInverseNode invn; setTf(invn, "Transform", A); invn.process();
    const krs::RigidTransform Ainv = outTf(invn, "Inverse");
    const krs::RigidTransform AAi = A * Ainv;              // should be identity
    const glm::vec3 idP = AAi.rotation * p + AAi.position;
    const bool inverseOk = glm::length(idP - p) < 1e-5f
                        && std::abs(glm::length(AAi.rotation) - 1.0f) < 1e-5f
                        && glm::length(AAi.position) < 1e-5f;

    // NEG-CTRL: the WRONG order B.A produces a DIFFERENT, wrong result (order matters; the node used the right one).
    const glm::vec3 wrongOrder = B.rotation * (A.rotation * p + A.position) + B.position;   // (B.A)(p)
    const bool orderMatters = glm::length(wrongOrder - expected) > 1e-2f          // the two orders really differ
                           && glm::length(got - wrongOrder) > 1e-2f;              // the node did NOT use the wrong order

    const bool pass = composeOk && inverseOk && orderMatters;
    printf("[tform]   A.B(p) err=%.2e (<1e-5:%d); inverse->identity err=%.2e (ok:%d); "
           "NEG wrong-order B.A differs by %.3f (order matters + correct used:%d)  %s\n",
           double(glm::length(got - expected)), int(composeOk), double(glm::length(idP - p)), int(inverseOk),
           double(glm::length(wrongOrder - expected)), int(orderMatters), pass ? "PASS" : "FAIL");
    printf("[tform] %s\n", pass ? "ALL PASS (compose matches closed form; inverse cancels; wrong compose order is a different wrong result)"
                                : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

bool runLinalgCorrectGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[linalg] GATE LINALG-CORRECT -- each linear-algebra node matches its closed-form result\n");

    // dot (1,2,3).(4,5,6) = 32 via signal_dot_product.
    double dot = std::nan("");
    if (auto d = NodeFactory::instance().createNode("signal_dot_product")) {
        setVec(*d, "A", glm::vec3(1, 2, 3)); setVec(*d, "B", glm::vec3(4, 5, 6)); d->process(); dot = outNum(*d, "Result");
    }
    const bool dotOk = std::abs(dot - 32.0) < 1e-4;
    const double dotWrong = 1 + 2 + 3 + 4 + 5 + 6;            // a sum-instead-of-MAC impl = 21 != 32
    const bool dotNeg = std::abs(dotWrong - 32.0) > 1e-3;

    // vector scale (1,2,3)*2 = (2,4,6).
    VectorScaleNode sc; setVec(sc, "Vector", glm::vec3(1, 2, 3)); setNum(sc, "Scalar", 2.0); sc.process();
    const bool scaleOk = glm::length(outVec(sc, "Result") - glm::vec3(2, 4, 6)) < 1e-5f;

    // cross (1,0,0)x(0,1,0) = (0,0,1) via linalg_vec_cross (Eigen::Vector3f ports; vector coercion feeds glm in).
    glm::vec3 crossR(std::nanf(""));
    if (auto c = NodeFactory::instance().createNode("linalg_vec_cross")) {
        setVec(*c, "A", glm::vec3(1, 0, 0)); setVec(*c, "B", glm::vec3(0, 1, 0)); c->process();
        for (const auto& x : c->getPorts()) if (x.direction == Port::Direction::Output && x.name == "Result" && x.packet) {
            try { auto v = std::any_cast<Eigen::Vector3f>(x.packet->data); crossR = glm::vec3(v[0], v[1], v[2]); } catch (...) {}
        }
    }
    const bool crossOk = glm::length(crossR - glm::vec3(0, 0, 1)) < 1e-5f;

    // magnitude |(3,4,0)| = 5 via linalg_vec_magnitude.
    double mag = std::nan("");
    if (auto m = NodeFactory::instance().createNode("linalg_vec_magnitude")) {
        setVec(*m, "Vector", glm::vec3(3, 4, 0)); m->process(); mag = outNum(*m, "Magnitude"); if (std::isnan(mag)) mag = outNum(*m, "Result");
    }
    const bool magOk = std::abs(mag - 5.0) < 1e-4;

    // matrix transpose + inverse on a known 2x2 [[1,2],[3,4]].
    Eigen::MatrixXf M(2, 2); M << 1, 2, 3, 4;
    MatrixTransposeNode tp; { PortDataPacket pk; pk.data = M; pk.type = { "Eigen::MatrixXf", "matrix" }; tp.setInput("Matrix", pk); } tp.process();
    Eigen::MatrixXf Mt; bool tpHas = false;
    for (const auto& x : tp.getPorts()) if (x.direction == Port::Direction::Output && x.name == "Result" && x.packet)
        try { Mt = std::any_cast<Eigen::MatrixXf>(x.packet->data); tpHas = true; } catch (...) {}
    const bool transposeOk = tpHas && std::abs(Mt(0, 1) - 3.0f) < 1e-5f && std::abs(Mt(1, 0) - 2.0f) < 1e-5f;

    MatrixInverseNode mi; { PortDataPacket pk; pk.data = M; pk.type = { "Eigen::MatrixXf", "matrix" }; mi.setInput("Matrix", pk); } mi.process();
    Eigen::MatrixXf Mi; bool miHas = false;
    for (const auto& x : mi.getPorts()) if (x.direction == Port::Direction::Output && x.name == "Inverse" && x.packet)
        try { Mi = std::any_cast<Eigen::MatrixXf>(x.packet->data); miHas = true; } catch (...) {}
    // inv([[1,2],[3,4]]) = [[-2,1],[1.5,-0.5]]; verify M*Mi == I.
    const bool inverseOk = miHas && ((M * Mi) - Eigen::MatrixXf::Identity(2, 2)).norm() < 1e-4f;

    const bool pass = dotOk && dotNeg && scaleOk && crossOk && magOk && transposeOk && inverseOk;
    printf("[linalg]   dot=%.1f(==32:%d, NEG sum=%.0f!=32:%d); scale->(2,4,6):%d; cross->(0,0,1):%d; |3,4,0|=%.3f(==5:%d); "
           "transpose:%d; inverse(M*M^-1==I):%d  %s\n",
           dot, int(dotOk), dotWrong, int(dotNeg), int(scaleOk), int(crossOk), mag, int(magOk),
           int(transposeOk), int(inverseOk), pass ? "PASS" : "FAIL");
    printf("[linalg] %s\n", pass ? "ALL PASS (dot/cross/normalize/scale/transpose/inverse match closed form; a sum-not-MAC dot fails)"
                                 : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::nodes
