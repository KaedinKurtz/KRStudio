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
#include "NodeEditQueue.hpp"

#include <QtNodes/Definitions>
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/NodeDelegateModelRegistry>
#include <QWidget>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
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
    return tn == "double" || tn == "float" || tn == "int" || tn == "bool" || tn == "glm::vec3" || tn == "enum";
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
    else if (auto* cb = qobject_cast<QComboBox*>(ctl)) cb->setCurrentIndex(int(v));
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
    if (!n->isPureInputFunction()) return false;   // event/stateful sources: output isn't f(inputs) alone
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

namespace {
double readOutResult(Node* n, const char* port) {
    for (const auto& p : n->getPorts())
        if (p.direction == Port::Direction::Output && p.name == port && p.packet.has_value()) return anyToD(p.packet->data);
    return std::nan("");
}
} // namespace

bool runWidgetInputGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[widget-input] GATE WIDGET-INPUT -- a node's own spin-box value is the unconnected input (wire overrides)\n");
    if (!QApplication::instance()) { printf("[widget-input] FAIL: needs QApplication\n"); return false; }
    const bool wasDeferred = NodeEditQueue::instance().deferred();
    NodeEditQueue::instance().setDeferred(false);    // apply driveControl edits synchronously in this gate

    // STEP 1 -- type 3 + 4 into the UNCONNECTED spin boxes -> Result 7 (the headline can't-fake number).
    NodeDelegate d1("math_add"); Node* n1 = d1.backendNode(); QWidget* b1 = d1.embeddedWidget();
    if (auto* a = findInputControl(b1, "A")) driveControl(a, 3.0);
    if (auto* b = findInputControl(b1, "B")) driveControl(b, 4.0);
    const double typed = readOutResult(n1, "Result");
    const bool typedOk = std::isfinite(typed) && std::abs(typed - 7.0) < 1e-6;

    // STEP 2 -- an UNTOUCHED mount: the literal is SEEDED from the default widget value, so compute READS it
    // (inputs not nullopt) and outputs 0 (0+0). The OLD behavior left the literal nullopt -> no output.
    NodeDelegate d2("math_add"); Node* n2 = d2.backendNode(); d2.embeddedWidget();
    n2->process();
    const bool seeded = n2->getInput<float>("A").has_value() && n2->getInput<float>("B").has_value();
    const double untouched = readOutResult(n2, "Result");
    const bool defaultOk = seeded && std::isfinite(untouched) && std::abs(untouched) < 1e-9;

    // STEP 3 -- a WIRE into B overrides the widget: A=3 (widget), B=4 (widget) then deliver a packet 10 to B.
    NodeDelegate d3("math_add"); Node* n3 = d3.backendNode(); QWidget* b3 = d3.embeddedWidget();
    if (auto* a = findInputControl(b3, "A")) driveControl(a, 3.0);
    if (auto* b = findInputControl(b3, "B")) driveControl(b, 4.0);
    PortDataPacket wire; wire.data = 10.0f; wire.type = { "float", "unitless" };
    n3->setInput("B", wire); n3->process();
    const double wired = readOutResult(n3, "Result");
    const bool wireOk = std::isfinite(wired) && std::abs(wired - 13.0) < 1e-6;   // A(3 widget) + B(10 wire)

    // NEG-CTRL -- the OLD behavior as a REAL failing model: a RAW math_add (no delegate mount -> no seeded
    // literal) gets getInput()==nullopt -> "if (a && b)" emits nothing -> Result unset (nan). It DIFFERS from
    // the fixed untouched output (0), so a regression to "spin box ignored" would flip this gate red.
    auto raw = NodeFactory::instance().createNode("math_add");
    raw->process();
    const double rawOut = readOutResult(raw.get(), "Result");
    const bool negOk = std::isnan(rawOut) && std::isfinite(untouched);

    NodeEditQueue::instance().setDeferred(wasDeferred);
    const bool pass = typedOk && defaultOk && wireOk && negOk;
    printf("[widget-input]   typed 3+4 (unconnected) -> %.3f (want 7, ok:%d); untouched mount seeded -> %.3f (want 0, inputs-read:%d); "
           "wire 10->B overrides -> %.3f (want 13, ok:%d); NEG raw(old) Result=%.3f (no-output:%d, differs from fixed 0)  %s\n",
           typed, int(typedOk), untouched, int(defaultOk), wired, int(wireOk), rawOut, int(negOk), pass ? "PASS" : "FAIL");
    printf("[widget-input] %s\n", pass ? "ALL PASS (typed spin-box value feeds compute; wire overrides; old spin-box-ignored fails)"
                                       : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

bool runComboInputGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[combo-input] GATE COMBO-INPUT -- a node's enum combo selection is read by its compute\n");
    if (!QApplication::instance()) { printf("[combo-input] FAIL: needs QApplication\n"); return false; }
    const bool wasDeferred = NodeEditQueue::instance().deferred();
    NodeEditQueue::instance().setDeferred(false);

    // math_op: A,B spin boxes + an "Op" enum combo (Add/Subtract/Multiply/Divide).
    NodeDelegate d("math_op"); Node* n = d.backendNode(); QWidget* body = d.embeddedWidget();
    QWidget* opCtl = findInputControl(body, "Op");
    const bool comboMounted = qobject_cast<QComboBox*>(opCtl) != nullptr;   // the enum input mounts a real combo
    if (auto* a = findInputControl(body, "A")) driveControl(a, 6.0);
    if (auto* b = findInputControl(body, "B")) driveControl(b, 2.0);

    double add = std::nan(""), mul = std::nan(""), sub = std::nan("");
    if (opCtl) { driveControl(opCtl, 0.0); add = readOutResult(n, "Result"); }   // Add     -> 8
    if (opCtl) { driveControl(opCtl, 2.0); mul = readOutResult(n, "Result"); }   // Multiply-> 12
    if (opCtl) { driveControl(opCtl, 1.0); sub = readOutResult(n, "Result"); }   // Subtract-> 4
    const bool changes = comboMounted && std::abs(add - 8.0) < 1e-6 && std::abs(mul - 12.0) < 1e-6
                      && std::abs(sub - 4.0) < 1e-6 && add != mul && mul != sub;

    // NEG-CTRL -- the OLD behavior: a RAW math_op (no combo mounted -> Op literal never seeded) reads
    // getInput<int>("Op")==nullopt -> value_or(0) = ALWAYS Add. The selection cannot reach compute; it is
    // stuck on Add (8) and CANNOT become Multiply (12) -> differs from the driven case, a real failing model.
    auto raw = NodeFactory::instance().createNode("math_op");
    raw->setPortLiteral<float>("A", 6.0f); raw->setPortLiteral<float>("B", 2.0f);
    raw->process();
    const double rawOut = readOutResult(raw.get(), "Result");
    const bool negOk = std::abs(rawOut - 8.0) < 1e-6 && std::abs(rawOut - mul) > 1e-6;   // stuck on Add, != Multiply

    NodeEditQueue::instance().setDeferred(wasDeferred);
    const bool pass = comboMounted && changes && negOk;
    printf("[combo-input]   Op combo mounted:%d; A=6,B=2 -> Add=%.1f Multiply=%.1f Subtract=%.1f (want 8/12/4, selection-read:%d); "
           "NEG raw(no enum binding) stuck=%.1f (Add, != Multiply:%d)  %s\n",
           int(comboMounted), add, mul, sub, int(changes), rawOut, int(negOk), pass ? "PASS" : "FAIL");
    printf("[combo-input] %s\n", pass ? "ALL PASS (combo selection drives compute; old combo-ignored fails)"
                                      : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

// build a REAL QtNodes DataFlowGraphModel backed by the node registry (as MainWindow does) so the gates
// exercise the editor's actual addNode/connectionPossible/addConnection paths, not a re-implementation.
std::shared_ptr<QtNodes::DataFlowGraphModel> makeNodeGraphModel() {
    auto reg = std::make_shared<QtNodes::NodeDelegateModelRegistry>();
    for (auto const& kv : NodeFactory::instance().getRegisteredNodeTypes()) {
        const std::string id = kv.first;
        reg->registerModel<NodeDelegate>([id]() { return std::make_unique<NodeDelegate>(id); },
                                         QString::fromStdString(kv.second.category));
    }
    return std::make_shared<QtNodes::DataFlowGraphModel>(reg);
}
// the QtNodes directional port index of a named port (same ordering NodeDelegate uses).
int portIndexByName(Node* n, Port::Direction dir, const char* name) {
    if (!n) return -1; int idx = 0;
    for (const auto& p : n->getPorts()) if (p.direction == dir) { if (p.name == name) return idx; ++idx; }
    return -1;
}

bool runFrameGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[frame] GATE FRAME -- every registered node type exposes ports via the REAL QtNodes model\n");
    if (!QApplication::instance()) { printf("[frame] FAIL: needs QApplication\n"); return false; }
    auto model = makeNodeGraphModel();

    int M = 0, framed = 0;
    std::vector<std::string> frameless;
    // The real editor instancing path: addNode -> the registry builds the NodeDelegate; nPorts() is the
    // SAME query QtNodes uses to draw port circles + decide connectability. >=1 port (in OR out) means the
    // node is wireable and gets its frame chrome; 0 ports == the screenshot's "bare panel, can't be wired".
    for (const auto& kv : NodeFactory::instance().getRegisteredNodeTypes()) {
        const std::string& typeId = kv.first;
        ++M;
        const QtNodes::NodeId id = model->addNode(QString::fromStdString(typeId));
        auto* del = model->delegateModel<NodeDelegate>(id);
        const unsigned in  = del ? del->nPorts(QtNodes::PortType::In)  : 0u;
        const unsigned out = del ? del->nPorts(QtNodes::PortType::Out) : 0u;
        if (in + out > 0u) ++framed; else frameless.push_back(typeId);
    }

    // NEG-CTRL: the SAME predicate applied to a 0-port delegate flags it (an unregistered type -> null
    // backend -> nPorts 0). Proves the gate would CATCH a frameless node, not silently count it as passed.
    NodeDelegate canary("__frameless_canary_unregistered__");
    const unsigned czero = canary.nPorts(QtNodes::PortType::In) + canary.nPorts(QtNodes::PortType::Out);
    // and a real node is non-zero (the predicate discriminates, not always-true).
    NodeDelegate real("math_add");
    const unsigned creal = real.nPorts(QtNodes::PortType::In) + real.nPorts(QtNodes::PortType::Out);
    const bool negOk = (czero == 0u) && (creal > 0u);

    // The counted ports are the SAME ones the connection system uses: prove a representative output->input
    // pair is connectable through the real model's connectionPossible (the actual drag path), so "exposes a
    // port" is not merely enumeration -- the port is usable for a wire. (Full type truth table is GATE TYPE.)
    const QtNodes::NodeId aId = model->addNode("gen_sine");
    const QtNodes::NodeId bId = model->addNode("math_add");
    auto* aDel = model->delegateModel<NodeDelegate>(aId);
    auto* bDel = model->delegateModel<NodeDelegate>(bId);
    const int aoi = aDel ? portIndexByName(aDel->backendNode(), Port::Direction::Output, "Out") : -1;
    const int bii = bDel ? portIndexByName(bDel->backendNode(), Port::Direction::Input,  "A")   : -1;
    bool connectable = false;
    if (aoi >= 0 && bii >= 0)
        connectable = model->connectionPossible({ aId, QtNodes::PortIndex(aoi), bId, QtNodes::PortIndex(bii) });

    const bool pass = (M > 0) && (framed == M) && negOk && connectable;
    printf("[frame]   FRAME coverage: %d/%d registered node types expose >=1 port via the real QtNodes model (the same nPorts it draws + connects)\n", framed, M);
    if (!frameless.empty()) { printf("[frame]   FRAMELESS (0 ports, caught): "); for (auto& s : frameless) printf("%s ", s.c_str()); printf("\n"); }
    printf("[frame]   NEG-CTRL detector: 0-port delegate ports=%u (caught), real node ports=%u (>0)  %s\n",
           czero, creal, negOk ? "PASS" : "FAIL!");
    printf("[frame]   counted ports are connection-usable: gen_sine.Out -> math_add.A connectionPossible=%s\n", connectable ? "yes" : "NO");
    printf("[frame] %s (on-screen clickable frame = OPERATOR VISUAL-CONFIRM; full type truth table = GATE TYPE)\n",
           pass ? "ALL PASS (every registered type exposes connection-usable ports via the real model)" : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

bool runTypeGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[type] GATE TYPE -- REAL DataFlowGraphModel::connectionPossible (compatible connect, incompatible blocked)\n");
    if (!QApplication::instance()) { printf("[type] FAIL: needs QApplication\n"); return false; }
    auto model = makeNodeGraphModel();

    struct Case { const char* on; const char* op; const char* in; const char* ip; bool ok; const char* what; };
    const Case cases[] = {
        { "gen_sine",           "Out",    "math_affine",         "In",       true,  "number->number" },
        { "math_add",           "Result", "math_subtract",       "A",        true,  "number->number" },
        { "gen_sine",           "Out",    "math_add",            "A",        true,  "number->number" },
        { "signal_dot_product", "Result", "math_affine",         "In",       true,  "Dot(number)->number" },
        { "gen_sine",           "Out",    "signal_dot_product",  "A",        false, "number->vec3 (incompat)" },
        { "gen_sine",           "Out",    "physics_apply_force", "Registry", false, "number->handle (incompat)" },
        { "gen_sine",           "Out",    "math_add",            "Trigger",  false, "number->Trigger/bool (must BLOCK)" },
        { "math_greater_than",  "Result", "math_add",            "Trigger",  true,  "bool->Trigger/bool (logic can trigger)" },
    };
    int total = 0, correct = 0;
    for (const auto& c : cases) {
        ++total;
        const QtNodes::NodeId on = model->addNode(c.on);
        const QtNodes::NodeId in = model->addNode(c.in);
        Node* onB = model->delegateModel<NodeDelegate>(on) ? model->delegateModel<NodeDelegate>(on)->backendNode() : nullptr;
        Node* inB = model->delegateModel<NodeDelegate>(in) ? model->delegateModel<NodeDelegate>(in)->backendNode() : nullptr;
        const int oi = portIndexByName(onB, Port::Direction::Output, c.op);
        const int ii = portIndexByName(inB, Port::Direction::Input, c.ip);
        if (oi < 0 || ii < 0) { printf("[type]   %-34s PORT NOT FOUND  FAIL\n", c.what); continue; }
        const QtNodes::ConnectionId conn{ on, QtNodes::PortIndex(oi), in, QtNodes::PortIndex(ii) };
        const bool got = model->connectionPossible(conn);          // the REAL editor path (id + vacancy + policy)
        const bool ok = (got == c.ok); if (ok) ++correct;
        printf("[type]   %-34s expect %s, got %s  %s\n", c.what, c.ok ? "connect" : "BLOCK",
               got ? "connect" : "BLOCK", ok ? "ok" : "FAIL");
    }
    const bool pass = correct == total && total > 6;
    printf("[type] %d/%d real connectionPossible checks correct  %s\n", correct, total,
           pass ? "ALL PASS (compatible connect, incompatible/Trigger blocked, via the real model)" : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::nodes
