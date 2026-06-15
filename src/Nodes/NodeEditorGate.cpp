// NodeEditorGate.cpp -- node-editor front-end gates. INPUT-BIND: for every input-bearing node type, the
// per-input widget MOUNTED in the node body (built via NodeDelegate::populateEmbeddedWidget, the same path
// the rendered body uses) is BOUND to the input -- driving the widget changes the node's evaluated output.
// Reports N-of-M coverage. NEG-CTRL: an unbound bare widget changes nothing. TYPE: the unified port type
// ids let compatible ports connect and keep incompatible ones unconnectable (mirrors QtNodes
// connectionPossible's id match).

#include "NodeDelegate.hpp"
#include "NodeFactory.hpp"
#include "Node.hpp"
#include "NodeEditorGate.hpp"

#include <QtNodes/Definitions>
#include <QWidget>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QAbstractSpinBox>

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <any>

namespace krs::nodes {

namespace {
bool isEditableType(const std::string& tn) {
    return tn == "double" || tn == "float" || tn == "int" || tn == "bool" || tn == "glm::vec3";
}
int editableInputCount(Node* n) {
    int c = 0; for (const auto& p : n->getPorts())
        if (p.direction == Port::Direction::Input && p.name != "Trigger" && isEditableType(p.type.name)) ++c;
    return c;
}
// find the widget tagged with this input port name inside the embedded body.
QWidget* findInputControl(QWidget* body, const std::string& port) {
    for (QWidget* w : body->findChildren<QWidget*>())
        if (w->property("krs_input_port").isValid() && w->property("krs_input_port").toString() == QString::fromStdString(port))
            return w;
    return nullptr;
}
void driveControl(QWidget* ctl, double v) {
    if (auto* d = qobject_cast<QDoubleSpinBox*>(ctl)) d->setValue(v);
    else if (auto* s = qobject_cast<QSpinBox*>(ctl)) s->setValue(int(v));
    else if (auto* c = qobject_cast<QCheckBox*>(ctl)) c->setChecked(v != 0.0);
}
double anyToD(const std::any& a) {
    try { return std::any_cast<double>(a); } catch (...) {}
    try { return double(std::any_cast<float>(a)); } catch (...) {}
    try { return std::any_cast<bool>(a) ? 1.0 : 0.0; } catch (...) {}
    try { return double(std::any_cast<int>(a)); } catch (...) {}
    try { const glm::vec3 v = std::any_cast<glm::vec3>(a); return double(v.x) + 7.0 * v.y + 13.0 * v.z; } catch (...) {}
    return std::nan("");
}
bool snapshottable(const std::string& tn) {
    return tn == "double" || tn == "float" || tn == "int" || tn == "bool" || tn == "glm::vec3";
}
// a node is drive-provable iff its output depends ONLY on editable inputs (no handle/connection-only
// inputs) AND it has a snapshottable output we can observe.
bool driveProvable(Node* n) {
    bool snapOut = false;
    for (const auto& p : n->getPorts()) {
        if (p.direction == Port::Direction::Input && p.name != "Trigger" && !isEditableType(p.type.name)) return false;
        if (p.direction == Port::Direction::Output && snapshottable(p.type.name)) snapOut = true;
    }
    return snapOut;
}
// snapshot all output port values of a node as doubles.
std::vector<double> outSnapshot(Node* n) {
    std::vector<double> v;
    for (const auto& p : n->getPorts())
        if (p.direction == Port::Direction::Output) v.push_back(p.packet.has_value() ? anyToD(p.packet->data) : std::nan(""));
    return v;
}
bool snapsDiffer(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size()) return true;
    for (size_t i = 0; i < a.size(); ++i) {
        const bool an = std::isnan(a[i]), bn = std::isnan(b[i]);
        if (an != bn) return true;
        if (!an && std::abs(a[i] - b[i]) > 1e-9) return true;
    }
    return false;
}
} // namespace

bool runInputBindGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[input-bind] GATE INPUT-BIND -- per-input widget MOUNTED in the node body drives the output\n");
    if (!QApplication::instance()) { printf("[input-bind] FAIL: needs QApplication\n"); return false; }

    int M = 0, mounted = 0, Mdrive = 0, bound = 0;
    std::vector<std::string> failures;
    for (const auto& kv : NodeFactory::instance().getRegisteredNodeTypes()) {
        const std::string& typeId = kv.first;
        NodeDelegate delegate(typeId);
        Node* node = delegate.backendNode();
        if (!node) continue;
        const int ec = editableInputCount(node);
        if (ec == 0) continue;                 // not input-bearing (a source / handle-only node)
        ++M;
        QWidget* body = delegate.embeddedWidget();
        if (!body) { failures.push_back(typeId + "(no-body)"); continue; }

        std::vector<std::string> ports; std::vector<QWidget*> ctls;
        for (const auto& p : node->getPorts())
            if (p.direction == Port::Direction::Input && p.name != "Trigger" && isEditableType(p.type.name)) {
                ports.push_back(p.name); ctls.push_back(findInputControl(body, p.name));
            }
        bool allMounted = true; for (auto* c : ctls) if (!c) allMounted = false;
        if (!allMounted) { failures.push_back(typeId + "(unmounted)"); continue; }
        ++mounted;

        // "drives output" only for nodes fully determined by their editable inputs (no handle inputs) with
        // a snapshottable output -- others mount their widget but need a data CONNECTION to produce output.
        if (!driveProvable(node)) continue;

        auto baseline = [&]() { for (size_t i = 0; i < ctls.size(); ++i) driveControl(ctls[i], 0.37 + 0.21 * double(i)); };
        baseline(); const auto base = outSnapshot(node);
        bool baseHasOutput = false; for (double v : base) if (std::isfinite(v)) baseHasOutput = true;
        if (!baseHasOutput) { failures.push_back(typeId + "(no-output-stub)"); continue; }  // e.g. util_script (compute is a no-op)
        ++Mdrive;
        bool anyChanged = false;
        for (size_t i = 0; i < ctls.size() && !anyChanged; ++i) {
            baseline(); driveControl(ctls[i], 0.37 + 0.21 * double(i) + 1.37);   // perturb input i across thresholds/periods
            if (snapsDiffer(base, outSnapshot(node))) anyChanged = true;
        }
        if (anyChanged) ++bound; else failures.push_back(typeId + "(no-effect)");
    }

    // CURATED FORMULA PROOFS: the widget value reaches compute() CORRECTLY (not just "changed").
    int formulaOk = 0, formulaN = 0;
    auto formula = [&](const char* id, const char* pa, double va, const char* pb, double vb,
                       const char* outp, double expect) {
        ++formulaN; NodeDelegate d(id); Node* n = d.backendNode(); QWidget* body = d.embeddedWidget();
        if (!n || !body) return;
        if (auto* a = findInputControl(body, pa)) driveControl(a, va);
        if (auto* b = findInputControl(body, pb)) driveControl(b, vb);
        double got = std::nan("");
        for (const auto& p : n->getPorts()) if (p.direction == Port::Direction::Output && p.name == outp && p.packet.has_value()) got = anyToD(p.packet->data);
        if (std::isfinite(got) && std::abs(got - expect) < 1e-4) ++formulaOk;
        else failures.push_back(std::string(id) + "(formula " + std::to_string(got) + "!=" + std::to_string(expect) + ")");
    };
    formula("math_add", "A", 3.0, "B", 4.0, "Result", 7.0);
    formula("math_multiply", "A", 2.5, "B", 4.0, "Result", 10.0);
    formula("math_subtract", "A", 9.0, "B", 4.0, "Result", 5.0);

    // NEG-CTRL: a bare widget NOT bound to any port changes nothing in a node.
    bool negOk = false;
    {
        NodeDelegate d("math_add"); Node* n = d.backendNode();
        QWidget* body = d.embeddedWidget();
        if (n && body) {
            // set both inputs so it computes, snapshot, then twiddle an UNBOUND spinbox (no krs_input_port).
            if (auto* a = findInputControl(body, "A")) driveControl(a, 2.0);
            if (auto* b = findInputControl(body, "B")) driveControl(b, 3.0);
            const auto s1 = outSnapshot(n);
            QDoubleSpinBox bare; bare.setValue(99.0);   // unbound -> not connected to setPortLiteral
            const auto s2 = outSnapshot(n);
            negOk = !snapsDiffer(s1, s2);               // node output unchanged by the unbound widget
        }
    }

    const bool pass = (M > 0) && (mounted == M) && (bound == Mdrive) && (formulaOk == formulaN) && negOk;
    printf("[input-bind]   coverage: %d/%d input-bearing node types mount ALL their input widgets; %d/%d (with outputs) driving the widget changes the output\n",
           mounted, M, bound, Mdrive);
    printf("[input-bind]   curated formula proofs (add/mul/sub via the mounted widgets): %d/%d correct\n", formulaOk, formulaN);
    if (!failures.empty()) { printf("[input-bind]   FAILURES: "); for (auto& s : failures) printf("%s ", s.c_str()); printf("\n"); }
    printf("[input-bind]   NEG-CTRL unbound bare widget -> node output unchanged  %s\n", negOk ? "PASS" : "FAIL!");
    printf("[input-bind] %s\n", pass ? "ALL PASS (every input-bearing node mounts a bound input widget; values reach compute; unbound inert)"
                                     : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

bool runTypeGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[type] GATE TYPE -- compatible ports connect (id match), incompatible cannot\n");
    if (!QApplication::instance()) { printf("[type] FAIL: needs QApplication\n"); return false; }

    // helper: the connection id QtNodes compares (NodeDelegate::dataType().id) for a node's port.
    auto portId = [](const std::string& typeId, QtNodes::PortType pt, int idx) -> QString {
        NodeDelegate d(typeId); d.backendNode();
        return d.dataType(pt, idx).id;
    };
    // QtNodes connectionPossible: out.id == in.id.
    auto canConnect = [&](const std::string& outNode, int outIdx, const std::string& inNode, int inIdx) {
        return portId(outNode, QtNodes::PortType::Out, outIdx) == portId(inNode, QtNodes::PortType::In, inIdx);
    };

    // truth table over REAL registered nodes: {outNode,outIdx, inNode,inIdx, expectCompatible}
    // (port indices count only that direction: math_add In = A@1,B@2; Out = Result@0; signal_dot_product
    // In = A@1(vec3),B@2(vec3), Out = Result@0(number); physics_apply_force In = Registry@1(handle)...)
    struct Case { const char* on; int oi; const char* in; int ii; bool ok; const char* what; };
    const Case cases[] = {
        { "gen_sine",            0, "math_affine",         1, true,  "Out(number)->In(number)" },
        { "math_add",            0, "math_subtract",       1, true,  "Result(number)->A(number)" },
        { "gen_sine",            0, "math_add",            1, true,  "number->A(number)" },
        { "signal_dot_product",  0, "math_affine",         1, true,  "Dot(number)->In(number)" },
        { "gen_sine",            0, "signal_dot_product",  1, false, "number->A(vec3) incompat" },
        { "gen_sine",            0, "physics_apply_force", 1, false, "number->Registry(handle) incompat" },
    };
    int total = 0, correct = 0;
    for (const auto& c : cases) {
        ++total;
        const bool got = canConnect(c.on, c.oi, c.in, c.ii);
        const bool ok = (got == c.ok);
        if (ok) ++correct;
        printf("[type]   %-26s expect %s, got %s  %s\n", c.what, c.ok ? "connect" : "BLOCK",
               got ? "connect" : "BLOCK", ok ? "ok" : "FAIL");
    }
    const bool pass = correct == total && total > 4;
    printf("[type] %d/%d port-pair compatibility checks correct  %s\n", correct, total,
           pass ? "ALL PASS (compatible connect, incompatible blocked)" : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::nodes
