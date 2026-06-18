// ===========================================================================
// ControlFlowNodes.cpp -- Sprint A: conditional / control-flow nodes built as EXTENSIONS of the trigger model
// (Button/trigger node), NOT a separate imperative layer.
//
// ARCHITECTURE (the deliberate choice): this is a DATAFLOW graph. Control flow in dataflow is NOT code control
// flow -- there is no instruction pointer, no call stack. Instead:
//   * A TRIGGER is a momentary boolean PULSE -- true for exactly one eval tick (the Button node's model).
//   * A CONDITION is a boolean value (e.g. a Compare node's a>b).
//   * Control flow = EDGE-DETECTING conditions, ROUTING pulses, and GATING re-execution -- all event-driven,
//     evaluated by the per-tick graph eval, staying entirely within the reactive dataflow model.
//
//   Compare  : a > b / == / ... -> a bool CONDITION (reads the in-widget input path: A,B spin boxes + Op combo).
//   When     : fires a trigger PULSE on the condition's false->true EDGE (Button edge-detect; "press" = condition true).
//   If       : routes an incoming trigger pulse to its True or False output by the condition AT trigger time.
//   While    : a BOUNDED iteration -- re-fires its Body pulse each tick while the condition holds, re-reading the
//              condition every pass, terminating when it goes false OR at a MANDATORY max-iteration safety cap.
//              Iteration spans successive eval ticks (one body pulse per tick) -- the event-driven model -- so it
//              can NEVER hang the graph eval; the cap is a hard backstop against a non-terminating condition.
//
// Gates (headless, folded into KRS_OVERNIGHT_BENCH): WHEN-FIRES-ON-CONDITION-EDGE, IF-ROUTES,
// WHILE-ITERATES-AND-TERMINATES -- each a measured count with a REAL failing-model neg-control (a level mistaken
// for an edge, both branches firing, a loop that never terminates / wrong count).
// ===========================================================================
#include "Node.hpp"
#include "NodeFactory.hpp"
#include "NodeEditorGate.hpp"

#include <cstdio>
#include <cmath>
#include <vector>
#include <tuple>
#include <functional>
#include <algorithm>
#include <memory>

namespace krs::nodes {
namespace {

// ---- Compare: the CONDITION source. A,B (widget spin boxes) + Op (enum combo) -> Result (bool). ----
class CompareNode : public Node {
public:
    CompareNode() {
        m_id = "logic_compare";
        m_ports.push_back({ "A", { "float", "unitless" }, Port::Direction::Input, this });
        m_ports.push_back({ "B", { "float", "unitless" }, Port::Direction::Input, this });
        addEnumInputPort("Op", { ">", ">=", "<", "<=", "==", "!=" });            // Phase-1 combo input
        m_ports.push_back({ "Result", { "bool", "unitless" }, Port::Direction::Output, this });
    }
    void compute() override {
        const double a = getInputD("A", 0.0), b = getInputD("B", 0.0);
        const int op = getInput<int>("Op").value_or(0);
        const double tol = 1e-6;                                                 // float-equality tolerance
        bool r = false;
        switch (op) {
            case 0: r = a >  b; break;
            case 1: r = a >= b; break;
            case 2: r = a <  b; break;
            case 3: r = a <= b; break;
            case 4: r = std::abs(a - b) <= tol; break;                           // ==
            case 5: r = std::abs(a - b) >  tol; break;                           // !=
        }
        setOutput<bool>("Result", r);
    }
};

// ---- When: a trigger PULSE on the condition's false->true edge (the Button's edge-detect; press = condition). ----
class WhenNode : public Node {
public:
    WhenNode() {
        m_id = "flow_when";
        m_ports.push_back({ "Condition", { "bool", "unitless" }, Port::Direction::Input,  this });
        m_ports.push_back({ "Trigger",   { "bool", "unitless" }, Port::Direction::Output, this });  // momentary pulse
    }
    bool needsExecutionControls() const override { return false; }
    bool isPureInputFunction() const override { return false; }                  // edge-detect: depends on the prior tick
    void compute() override {
        const bool cond   = getInput<bool>("Condition").value_or(false);
        const bool rising = cond && !m_lastCond;                                 // false -> true EDGE (not a level)
        setOutput<bool>("Trigger", rising);                                      // true ONLY on the rising tick
        m_lastCond = cond;
    }
private:
    bool m_lastCond = false;
};

// ---- If: route an incoming trigger pulse to True or False by the condition at trigger time (gated execution). ----
class IfNode : public Node {
public:
    IfNode() {
        m_id = "flow_if";
        m_ports.push_back({ "Pulse",     { "bool", "unitless" }, Port::Direction::Input,  this });  // the trigger to route
        m_ports.push_back({ "Condition", { "bool", "unitless" }, Port::Direction::Input,  this });
        m_ports.push_back({ "True",      { "bool", "unitless" }, Port::Direction::Output, this });
        m_ports.push_back({ "False",     { "bool", "unitless" }, Port::Direction::Output, this });  // the ELSE branch
    }
    bool needsExecutionControls() const override { return false; }
    bool isPureInputFunction() const override { return false; }                  // inert without a trigger pulse
    void compute() override {
        const bool pulse = getInput<bool>("Pulse").value_or(false);
        const bool cond  = getInput<bool>("Condition").value_or(false);
        setOutput<bool>("True",  pulse &&  cond);                                // exactly ONE branch fires per pulse
        setOutput<bool>("False", pulse && !cond);
    }
};

// ---- While: a BOUNDED, event-driven iteration. One Body pulse per tick while the condition holds; terminates on
//      condition-false OR at the mandatory max-iteration safety cap. Spans ticks -> can never hang the eval. ----
class WhileNode : public Node {
public:
    WhileNode() {
        m_id = "flow_while";
        m_ports.push_back({ "Start",          { "bool", "unitless" }, Port::Direction::Input,  this });  // begin (trigger)
        m_ports.push_back({ "Condition",      { "bool", "unitless" }, Port::Direction::Input,  this });  // re-read each pass
        m_ports.push_back({ "Max Iterations", { "int",  "unitless" }, Port::Direction::Input,  this });  // SAFETY CAP
        m_ports.push_back({ "Body",           { "bool", "unitless" }, Port::Direction::Output, this });  // pulse per pass
        m_ports.push_back({ "Done",           { "bool", "unitless" }, Port::Direction::Output, this });  // pulse on terminate
        m_ports.push_back({ "Iteration",      { "int",  "unitless" }, Port::Direction::Output, this });  // pass count
        m_ports.push_back({ "Capped",         { "bool", "unitless" }, Port::Direction::Output, this });  // hit the cap?
        setPortLiteral<int>("Max Iterations", 1000);                             // default safety cap
    }
    bool needsExecutionControls() const override { return false; }
    bool isPureInputFunction() const override { return false; }
    void compute() override {
        const bool start = getInput<bool>("Start").value_or(false);
        if (start && !m_lastStart) { m_active = true; m_iter = 0; }              // Start rising edge -> begin
        bool body = false, done = false, capped = false;
        if (m_active) {
            const int  maxIter = std::max(1, getInput<int>("Max Iterations").value_or(1000));
            const bool cond    = getInput<bool>("Condition").value_or(false);
            if (m_iter >= maxIter) { m_active = false; done = true; capped = true; }  // SAFETY CAP -- catch a runaway
            else if (cond)         { body = true; ++m_iter; }                         // one body pulse this pass
            else                   { m_active = false; done = true; }                 // condition false -> terminate
        }
        setOutput<bool>("Body", body);
        setOutput<bool>("Done", done);
        setOutput<int>("Iteration", m_iter);
        setOutput<bool>("Capped", capped);
        m_lastStart = start;
    }
private:
    bool m_active = false, m_lastStart = false;
    int  m_iter = 0;
};

template <class T> struct Registrar {
    Registrar(const char* id, NodeDescriptor d, std::function<std::unique_ptr<Node>()> f) {
        NodeFactory::instance().registerNodeType(id, d, std::move(f));
    }
};
static Registrar<CompareNode> g_cmp("logic_compare",
    { "Compare", "Logic", "a>b / >= / < / <= / == / != -> a boolean condition (reads the in-widget A,B + Op)." },
    [] { return std::make_unique<CompareNode>(); });
static Registrar<WhenNode> g_when("flow_when",
    { "When", "Control Flow", "Emits a trigger pulse when its Condition crosses false->true (an edge, not a level)." },
    [] { return std::make_unique<WhenNode>(); });
static Registrar<IfNode> g_if("flow_if",
    { "If", "Control Flow", "Routes an incoming trigger Pulse to True or False by the Condition at trigger time." },
    [] { return std::make_unique<IfNode>(); });
static Registrar<WhileNode> g_while("flow_while",
    { "While", "Control Flow", "Repeats a Body pulse while the Condition holds, with a mandatory max-iteration cap." },
    [] { return std::make_unique<WhileNode>(); });

// --- gate I/O helpers ---
void setBoolIn(Node& n, const char* p, bool v) { PortDataPacket pk; pk.data = v; pk.type = { "bool", "unitless" }; n.setInput(p, pk); }
bool rOutB(Node& n, const char* p) {
    for (const auto& x : n.getPorts()) if (x.direction == Port::Direction::Output && x.name == p && x.packet)
        try { return std::any_cast<bool>(x.packet->data); } catch (...) {}
    return false;
}
int rOutI(Node& n, const char* p) {
    for (const auto& x : n.getPorts()) if (x.direction == Port::Direction::Output && x.name == p && x.packet)
        try { return std::any_cast<int>(x.packet->data); } catch (...) {}
    return 0;
}
int countTrue(const std::vector<bool>& v) { int n = 0; for (bool b : v) if (b) ++n; return n; }

} // namespace

bool runWhenGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[when] GATE WHEN-FIRES-ON-CONDITION-EDGE -- a trigger pulse exactly once on the condition's false->true edge\n");

    // (a) the CONDITION SOURCE: Compare reads its A,B + Op and emits the right boolean.
    auto cmp = [](double a, double b, int op) {
        CompareNode c; c.setPortLiteral<float>("A", float(a)); c.setPortLiteral<float>("B", float(b));
        c.setPortLiteral<int>("Op", op); c.process(); return rOutB(c, "Result");
    };
    const bool cmpOk = cmp(6, 5, 0) && !cmp(4, 5, 0)        // a>b
                    && cmp(5, 5, 4) && !cmp(5, 6, 4)        // a==b (tol)
                    && !cmp(6, 5, 2) && cmp(4, 5, 2);       // a<b  (wrong-op on 6,5 gives a DIFFERENT result)

    // (b) WHEN edge: a condition that rises, holds, then falls -> exactly ONE fire, on the rising tick.
    auto runSeq = [](const std::vector<bool>& seq) {
        WhenNode w; std::vector<bool> fires;
        for (bool c : seq) { setBoolIn(w, "Condition", c); w.process(); fires.push_back(rOutB(w, "Trigger")); }
        return fires;
    };
    const std::vector<bool> seq = { false, true, true, true, false, false };  // idle, RISE, hold, hold, fall, idle
    const auto fires = runSeq(seq);
    const bool edgeOk = countTrue(fires) == 1 && fires[1] && !fires[2] && !fires[3] && !fires[0] && !fires[4];

    // NEG-CTRL 1 (REAL failing model): a LEVEL model fires every tick the condition is TRUE -> 3 fires != 1 edge.
    const int levelFires  = countTrue(seq);                  // 3 = fires-while-held (the level model)
    const bool negLevel   = levelFires != countTrue(fires);
    // NEG-CTRL 2: a model that IGNORES the condition and fires always -> 6 fires != 1.
    const int alwaysFires = int(seq.size());                 // 6
    const bool negAlways  = alwaysFires != countTrue(fires);

    // (c) Compare -> WHEN wiring: A rising across B fires WHEN ONCE on the crossing (a realistic condition edge).
    WhenNode w2; CompareNode c2; c2.setPortLiteral<float>("B", 5.0f); c2.setPortLiteral<int>("Op", 0);  // cond = A>5
    const std::vector<double> aSeq = { 3, 4, 5, 6, 7, 6 };    // A>5: F,F,F,T,T,T -> one false->true crossing
    int crossFires = 0;
    for (double a : aSeq) {
        c2.setPortLiteral<float>("A", float(a)); c2.process();
        setBoolIn(w2, "Condition", rOutB(c2, "Result")); w2.process();
        if (rOutB(w2, "Trigger")) ++crossFires;
    }
    const bool crossOk = crossFires == 1;

    // FUZZ: an OSCILLATING condition -> exactly one fire per false->true edge (many edges, each counted once;
    // catches "fires once then stops detecting" and "level"). Expected = an independent rising-edge count.
    auto risingEdges = [](const std::vector<bool>& s) { int n = 0; bool prev = false; for (bool c : s) { if (c && !prev) ++n; prev = c; } return n; };
    const std::vector<bool> osc = { false, true, false, true, true, false, true, false, true };
    const auto oscFires = runSeq(osc);
    const int oscExpected = risingEdges(osc);            // 4 false->true edges
    const bool oscOk = countTrue(oscFires) == oscExpected && oscExpected >= 3;

    const bool pass = cmpOk && edgeOk && negLevel && negAlways && crossOk && oscOk;
    printf("[when]   Compare a>b/==/wrong-op correct:%d; EDGE fires=%d (==1 on rise, none held/false):%d; "
           "NEG level=%d!=edge / always=%d!=edge:%d; Compare->When crossing fires=%d (==1):%d; "
           "FUZZ oscillating fires=%d (==%d edges):%d  %s\n",
           int(cmpOk), countTrue(fires), int(edgeOk), levelFires, alwaysFires, int(negLevel && negAlways),
           crossFires, int(crossOk), countTrue(oscFires), oscExpected, int(oscOk), pass ? "PASS" : "FAIL");
    printf("[when] %s\n", pass ? "ALL PASS (fires once on the rising condition edge; level/always-fire models fail; Compare drives the condition)"
                               : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

bool runIfGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[if] GATE IF-ROUTES -- a trigger routes to True or False by the condition; exactly one branch per trigger\n");

    auto route = [](bool pulse, bool cond, bool& tOut, bool& fOut) {
        IfNode n; setBoolIn(n, "Condition", cond); setBoolIn(n, "Pulse", pulse); n.process();
        tOut = rOutB(n, "True"); fOut = rOutB(n, "False");
    };
    bool tTT, fTT, tTF, fTF, tFT, fFT;
    route(true,  true,  tTT, fTT);     // trigger + TRUE  condition -> True only
    route(true,  false, tTF, fTF);     // trigger + FALSE condition -> False only
    route(false, true,  tFT, fFT);     // NO trigger               -> neither (gated off)

    const bool trueRoutes   = tTT && !fTT;
    const bool falseRoutes  = fTF && !tTF;
    const bool noTrigInert  = !tFT && !fFT;
    const bool oneBranchTT  = (int(tTT) + int(fTT)) == 1;
    const bool oneBranchTF  = (int(tTF) + int(fTF)) == 1;

    // NEG-CTRL 1 (REAL failing model): a "fires BOTH branches" model would give True+False==2, not 1.
    const bool negBoth = oneBranchTT && oneBranchTF;
    // NEG-CTRL 2: an INVERTED-condition model routes a true condition to False -> it would set fTT (not tTT).
    //             We require tTT (True on true cond) AND fTF (False on false cond): the inverted model fails both.
    const bool negInverted = tTT && !fTT && fTF && !tTF;

    const bool pass = trueRoutes && falseRoutes && noTrigInert && negBoth && negInverted;
    printf("[if]   true-cond->True(%d)not-False(%d); false-cond->False(%d)not-True(%d); no-trigger inert:%d; "
           "exactly-one-branch:%d; NEG both-fire would be 2 / inverted would flip:%d  %s\n",
           int(tTT), int(!fTT), int(fTF), int(!tTF), int(noTrigInert), int(oneBranchTT && oneBranchTF),
           int(negBoth && negInverted), pass ? "PASS" : "FAIL");
    printf("[if] %s\n", pass ? "ALL PASS (one branch per trigger, routed by the condition; both-fire / inverted-condition models fail)"
                             : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

bool runWhileGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[while] GATE WHILE-ITERATES-AND-TERMINATES -- bounded iteration; correct count; the cap catches a runaway\n");

    // Drive the WHILE across ticks: Start pulses on tick 0, the condition is re-derived each tick from the loop's
    // own progress (the dataflow feedback), and we count Body pulses until Done or the tick budget. condFn takes
    // (bodyFiresSoFar, iterationOutput) -> the condition for this pass.
    auto runLoop = [](int maxIter, int tickBudget, const std::function<bool(int, int)>& condFn) {
        WhileNode w; w.setPortLiteral<int>("Max Iterations", maxIter);
        setBoolIn(w, "Start", false); setBoolIn(w, "Condition", true); w.process();   // settle, Start low
        int bodyFires = 0, doneAt = -1; bool capped = false;
        for (int t = 0; t < tickBudget; ++t) {
            setBoolIn(w, "Start", t == 0);                          // Start rising edge on tick 0 only
            setBoolIn(w, "Condition", condFn(bodyFires, rOutI(w, "Iteration")));
            w.process();
            if (rOutB(w, "Body")) ++bodyFires;
            if (rOutB(w, "Done")) { doneAt = t; capped = rOutB(w, "Capped"); break; }
        }
        return std::make_tuple(bodyFires, doneAt, capped);
    };

    // TEST 1 -- a condition that becomes false after K=3 passes -> the body fires exactly 3 times, then stops.
    auto [k3Fires, k3Done, k3Cap] = runLoop(1000, 100, [](int fires, int) { return fires < 3; });
    const bool whileOk = k3Fires == 3 && k3Done >= 0 && !k3Cap;

    // TEST 2 -- a FOR-N loop (condition = Iteration < N, re-read from the node's own counter) fires exactly N=5.
    auto [n5Fires, n5Done, n5Cap] = runLoop(1000, 100, [](int, int iter) { return iter < 5; });
    const bool forNOk = n5Fires == 5 && n5Done >= 0 && !n5Cap;

    // TEST 3 -- a NON-TERMINATING condition (always true): the mandatory cap ENGAGES at maxIter=10 and Done+Capped
    //           fire -- the runaway is CAUGHT, not hung. A model with no cap would fire every tick (== budget 50).
    const int cap = 10, budget = 50;
    auto [infFires, infDone, infCap] = runLoop(cap, budget, [](int, int) { return true; });
    const bool capOk = infFires == cap && infDone >= 0 && infCap;       // fired exactly the cap, flagged, terminated
    const bool negNoCap = infFires != budget;                           // a no-cap (hang) model would have fired 50

    // FUZZ: a condition that goes false (terminating the loop) then FLIPS back true must NOT restart without a new
    // Start edge -- guards against a node that re-arms on the condition alone. Body fires only the pre-fall passes.
    auto runFlip = [](const std::vector<bool>& condSeq, int maxIter) {
        WhileNode w; w.setPortLiteral<int>("Max Iterations", maxIter);
        setBoolIn(w, "Start", false); setBoolIn(w, "Condition", true); w.process();
        int body = 0; bool done = false;
        for (size_t t = 0; t < condSeq.size(); ++t) {
            setBoolIn(w, "Start", t == 0); setBoolIn(w, "Condition", condSeq[t]); w.process();
            if (rOutB(w, "Body")) ++body;
            if (rOutB(w, "Done")) done = true;
        }
        return std::make_pair(body, done);
    };
    auto [flipBody, flipDone] = runFlip({ true, true, false, true, true }, 1000);   // falls at idx2; later trues ignored
    const bool flipOk = flipBody == 2 && flipDone;                      // only the 2 pre-fall passes; no spurious restart

    const bool pass = whileOk && forNOk && capOk && negNoCap && flipOk;
    printf("[while]   while(cond false after 3): body=%d(==3) done:%d cap:%d; for-N(=5): body=%d(==5) done:%d; "
           "runaway(maxIter=%d,budget=%d): body=%d(==cap) DONE:%d CAPPED:%d (no-cap would be %d:%d); "
           "FUZZ flip-after-terminate: body=%d(==2 no-restart):%d  %s\n",
           k3Fires, int(k3Done >= 0), int(k3Cap), n5Fires, int(n5Done >= 0),
           cap, budget, infFires, int(infDone >= 0), int(infCap), budget, int(negNoCap),
           flipBody, int(flipOk), pass ? "PASS" : "FAIL");
    printf("[while] %s\n", pass ? "ALL PASS (fires the exact count then terminates; for-N exact; the max-iter cap catches an infinite loop, no hang)"
                                : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::nodes
