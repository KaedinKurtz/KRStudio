// NodeLibraryGate.cpp -- Phase 1 GATE NODE-UI + Phase 3 GATE NODE-LIB. NODE-UI: an in-node dial's PARAM
// provably drives the node OUTPUT (set param -> process -> output matches analytic), with a disconnected-
// widget neg-control + bounded widget footprint. NODE-LIB: the EXISTING math/signal/logic library nodes'
// outputs are asserted against closed-form references over sampled inputs (<tol) -- the behavioral
// tightening of the loose "107 types instantiate" gate (a node is done only when its output matches a
// reference). Wrong-typed-input is the neg-control (a node fed garbage produces NO output).

#include "Node.hpp"
#include "NodeFactory.hpp"
#include "NodeWidgets.hpp"

#include <QWidget>
#include <QApplication>

#include <cstdio>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <any>

namespace krs::nodes {

namespace {
constexpr double TAU = 6.283185307179586;

void feedF(Node& n, const char* port, float v) {                 // existing nodes read getInput<float>
    PortDataPacket pk; pk.data = v; pk.type = { "", "" }; n.setInput(port, pk);
}
void feedAny(Node& n, const char* port, const std::any& a) {
    PortDataPacket pk; pk.data = a; pk.type = { "", "" }; n.setInput(port, pk);
}
// read an output port as a double, accepting float/double/bool/int.
double outD(Node& n, const char* port) {
    for (const auto& p : n.getPorts())
        if (p.direction == Port::Direction::Output && p.name == port && p.packet.has_value()) {
            try { return std::any_cast<double>(p.packet->data); } catch (...) {}
            try { return double(std::any_cast<float>(p.packet->data)); } catch (...) {}
            try { return std::any_cast<bool>(p.packet->data) ? 1.0 : 0.0; } catch (...) {}
            try { return double(std::any_cast<int>(p.packet->data)); } catch (...) {}
        }
    return std::nan("");
}
bool hasOut(Node& n, const char* port) {
    for (const auto& p : n.getPorts())
        if (p.direction == Port::Direction::Output && p.name == port) return p.packet.has_value();
    return false;
}
} // namespace

// =================================================================================================
// GATE NODE-UI (Phase 1)
// =================================================================================================
bool runNodeUiGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[node-ui] GATE NODE-UI -- in-node dial param drives output + bounded node footprint\n");

    // ---- NU1: the param a dial writes drives the OUTPUT (gen_sine: out = amp*sin(2pi*freq*t+phase)) ----
    auto g = NodeFactory::instance().createNode("gen_sine");
    double nu1err = 0.0; int nu1n = 0;
    if (g) {
        for (double freq : { 0.5, 1.5, 3.0 })
            for (double amp : { 0.5, 2.0 })
                for (double phase : { 0.0, 0.8 }) {
                    g->setParam<double>("freq", freq);          // <- exactly what the dial writes
                    g->setParam<double>("amp", amp);
                    g->setParam<double>("phase", phase);
                    for (double t = 0.0; t <= 1.0; t += 0.07) {
                        feedF(*g, "t", float(t)); g->process();
                        const double ref = amp * std::sin(TAU * freq * t + phase);
                        nu1err = std::max(nu1err, std::abs(outD(*g, "Out") - ref)); ++nu1n;
                    }
                }
    }
    const bool nu1ok = g && nu1n > 0 && nu1err < 1e-6;

    // NU1 NEG-CTRL (disconnected widget): a fresh gen uses DEFAULT params (freq=1,amp=1,phase=0); and
    // changing ANOTHER gen's params must not change this one.
    auto gA = NodeFactory::instance().createNode("gen_sine");
    auto gB = NodeFactory::instance().createNode("gen_sine");
    bool ndisc = false;
    if (gA && gB) {
        gB->setParam<double>("amp", 99.0);                       // move B's dial only
        feedF(*gA, "t", 0.25f); gA->process();
        ndisc = std::abs(outD(*gA, "Out") - std::sin(TAU * 0.25)) < 1e-6;  // A on defaults (amp1,freq1,ph0)
    }

    // ---- NU2: in-node widget footprint stays bounded (the real Qt widget size <= gated estimate) ----
    using krs::nodeui::ControlSpec;
    auto mk = [](int n) { std::vector<ControlSpec> v;
        for (int i = 0; i < n; ++i) v.push_back({ ControlSpec::Slider, "Label", "p", 0, 1, 0.01, 0 }); return v; };
    int maxW = 0, maxH = 0; bool bounded = true;
    for (int n : { 1, 2, 3, 6, 12 }) { const auto fp = krs::nodeui::estimateFootprint(mk(n));
        maxW = std::max(maxW, fp.w); maxH = std::max(maxH, fp.h);
        if (fp.w > krs::nodeui::kMaxW || fp.h > krs::nodeui::kMaxH) bounded = false; }
    const auto naive = krs::nodeui::naiveFootprint(mk(12));       // NEG-CTRL: uncapped exceeds the cap
    const bool capBites = naive.h > krs::nodeui::kMaxH;
    bool realBounded = true; int realW = 0, realH = 0;
    if (QApplication::instance()) {
        if (auto node = NodeFactory::instance().createNode("gen_sine"))
            if (QWidget* w = node->createCustomWidget()) {
                realW = w->width(); realH = w->height();
                realBounded = realW > 0 && realH > 0 && realW <= krs::nodeui::kMaxW && realH <= krs::nodeui::kMaxH;
                delete w;
            }
    }
    const bool nu2ok = bounded && capBites && realBounded;

    const bool pass = nu1ok && ndisc && nu2ok;
    printf("[node-ui]   NU1 dial-param drives output: max err=%.2e over %d samples  %s ; disconnected widget inert %s\n",
           nu1err, nu1n, nu1ok ? "PASS" : "FAIL", ndisc ? "PASS" : "FAIL!");
    printf("[node-ui]   NU2 footprint: max %dx%d (cap %dx%d), real gen widget %dx%d %s ; NEG-CTRL uncapped h=%d>cap %s\n",
           maxW, maxH, krs::nodeui::kMaxW, krs::nodeui::kMaxH, realW, realH, nu2ok ? "PASS" : "FAIL",
           naive.h, capBites ? "REJECTS" : "VACUOUS!");
    printf("[node-ui] %s\n", pass ? "ALL PASS (dial param drives output; disconnected inert; bounded footprint)"
                                   : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

// =================================================================================================
// GATE NODE-LIB (Phase 3) -- existing library nodes' outputs vs closed-form
// =================================================================================================
bool runNodeLibraryGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[node-lib] GATE NODE-LIB -- existing math/signal/logic nodes vs closed-form references (<tol)\n");

    auto MK = [](const char* id) { return NodeFactory::instance().createNode(id); };
    double worst = 0.0; int checks = 0, fails = 0;
    auto track = [&](const char* what, double err, double tol) {
        worst = std::max(worst, err); ++checks; const bool ok = err < tol; if (!ok) ++fails;
        printf("[node-lib]   %-18s max err=%.3e (tol %.0e)  %s\n", what, err, tol, ok ? "ok" : "FAIL"); };

    // binary op tester: feed two float ports, read Result, compare to ref(a,b).
    auto bin = [&](const char* id, const char* pa, const char* pb, const char* outp, auto ref, double tol) {
        if (auto n = MK(id)) { double e = 0;
            for (double a = -2.5; a <= 2.5; a += 0.5) for (double b = -2.0; b <= 2.0; b += 0.5) {
                feedF(*n, pa, float(a)); feedF(*n, pb, float(b)); n->process();
                e = std::max(e, std::abs(outD(*n, outp) - ref(a, b))); }
            track(id, e, tol); } };
    auto un = [&](const char* id, const char* pin, auto ref, double lo, double hi, double tol) {
        if (auto n = MK(id)) { double e = 0;
            for (double v = lo; v <= hi; v += (hi - lo) / 20.0) { feedF(*n, pin, float(v)); n->process();
                e = std::max(e, std::abs(outD(*n, "Result") - ref(v))); }
            track(id, e, tol); } };

    bin("math_add", "A", "B", "Result", [](double a, double b) { return a + b; }, 1e-5);
    bin("math_subtract", "A", "B", "Result", [](double a, double b) { return a - b; }, 1e-5);
    bin("math_multiply", "A", "B", "Result", [](double a, double b) { return a * b; }, 1e-5);
    // divide/modulo skip a ~0 denominator (no output), and pow needs a positive base for fractional
    // exponents -- gate each over a domain that stays defined.
    if (auto n = MK("math_divide")) { double e = 0; for (double a = -2.5; a <= 2.5; a += 0.5)
        for (double b = -2.0; b <= 2.0; b += 0.5) { if (std::abs(b) < 1e-6) continue;
            feedF(*n, "Numerator", float(a)); feedF(*n, "Denominator", float(b)); n->process();
            e = std::max(e, std::abs(outD(*n, "Result") - a / b)); } track("math_divide", e, 1e-4); }
    if (auto n = MK("math_modulo")) { double e = 0; for (double a = -2.5; a <= 2.5; a += 0.5)
        for (double b = 0.5; b <= 2.0; b += 0.5) { feedF(*n, "A", float(a)); feedF(*n, "B", float(b)); n->process();
            e = std::max(e, std::abs(outD(*n, "Result") - std::fmod(a, b))); } track("math_modulo", e, 1e-4); }
    if (auto n = MK("math_pow")) { double e = 0; for (double a = 0.2; a <= 3.0; a += 0.4)
        for (double b = -2.0; b <= 3.0; b += 0.5) { feedF(*n, "Base", float(a)); feedF(*n, "Exponent", float(b)); n->process();
            e = std::max(e, std::abs(outD(*n, "Result") - std::pow(a, b))); } track("math_pow", e, 1e-3); }
    bin("math_min", "A", "B", "Result", [](double a, double b) { return std::min(a, b); }, 1e-6);
    bin("math_max", "A", "B", "Result", [](double a, double b) { return std::max(a, b); }, 1e-6);
    bin("math_atan2", "Y", "X", "Result (rad)", [](double a, double b) { return std::atan2(a, b); }, 1e-5);
    bin("math_greater_than", "A", "B", "Result", [](double a, double b) { return a > b ? 1.0 : 0.0; }, 0.5);
    bin("math_less_than", "A", "B", "Result", [](double a, double b) { return a < b ? 1.0 : 0.0; }, 0.5);
    un("math_abs", "Input", [](double v) { return std::abs(v); }, -3, 3, 1e-6);
    un("math_sqrt", "Input", [](double v) { return std::sqrt(v); }, 0, 9, 1e-4);
    un("math_log", "Input", [](double v) { return std::log(v); }, 0.1, 9, 1e-4);
    un("math_sin", "Input (rad)", [](double v) { return std::sin(v); }, -3, 3, 1e-6);
    un("math_cos", "Input (rad)", [](double v) { return std::cos(v); }, -3, 3, 1e-6);

    if (auto n = MK("math_clamp")) { double e = 0; for (double v = -2; v <= 3; v += 0.2) {
        feedF(*n, "Input", float(v)); feedF(*n, "Min", 0.f); feedF(*n, "Max", 1.f); n->process();
        e = std::max(e, std::abs(outD(*n, "Result") - std::min(1.0, std::max(0.0, v)))); } track("math_clamp", e, 1e-6); }
    if (auto n = MK("math_lerp")) { double e = 0; for (double t = 0; t <= 1; t += 0.1) {
        feedF(*n, "A", 2.f); feedF(*n, "B", 8.f); feedF(*n, "Alpha", float(t)); n->process();
        e = std::max(e, std::abs(outD(*n, "Result") - (2 + 6 * t))); } track("math_lerp", e, 1e-5); }
    if (auto n = MK("math_equals")) { double e = 0;
        feedF(*n, "A", 1.5f); feedF(*n, "B", 1.5f); feedF(*n, "Tolerance", 1e-3f); n->process();
        e = std::abs(outD(*n, "Result") - 1.0);
        feedF(*n, "A", 1.5f); feedF(*n, "B", 1.7f); feedF(*n, "Tolerance", 1e-3f); n->process();
        e = std::max(e, std::abs(outD(*n, "Result") - 0.0)); track("math_equals", e, 0.5); }

    // new param-driven generators vs analytic
    auto gen = [&](const char* id, auto ref) {
        if (auto n = MK(id)) { n->setParam<double>("freq", 1.3); n->setParam<double>("amp", 1.7);
            n->setParam<double>("phase", 0.3); double e = 0;
            for (double t = 0; t <= 2.0; t += 0.011) { feedF(*n, "t", float(t)); n->process();
                const double x = TAU * 1.3 * t + 0.3; e = std::max(e, std::abs(outD(*n, "Out") - 1.7 * ref(x))); }
            track(id, e, 1e-6); } };
    gen("gen_sine", [](double x) { return std::sin(x); });
    gen("gen_square", [](double x) { return std::sin(x) >= 0 ? 1.0 : -1.0; });
    gen("gen_triangle", [](double x) { double p = x / TAU; p -= std::floor(p); return 4.0 * std::abs(p - 0.5) - 1.0; });
    gen("gen_saw", [](double x) { double p = x / TAU; p -= std::floor(p); return 2.0 * p - 1.0; });

    // signal_waveform (existing): default type is Sine, formula sin(t*freq*2pi)*amp (NO phase). Feed ports.
    if (auto n = MK("signal_waveform")) { double e = 0; const double f = 2.0, a = 1.5;
        for (double t = 0; t <= 1.5; t += 0.017) { feedF(*n, "Time", float(t)); feedF(*n, "Frequency", float(f));
            feedF(*n, "Amplitude", float(a)); n->process();
            e = std::max(e, std::abs(outD(*n, "Output") - std::sin(t * f * 2.0 * 3.14159f) * a)); }
        track("signal_waveform", e, 1e-3); }

    // NEG-CTRL (wrong-typed-input-rejected): feed math_add a STRING for A -> getInput<float> fails ->
    // compute() guards (if(a&&b)) -> NO output produced. A node that "did something" with garbage would FAIL.
    bool wrongTypeRejected = false;
    if (auto n = MK("math_add")) {
        feedAny(*n, "A", std::string("not_a_float")); feedF(*n, "B", 2.0f); n->process();
        wrongTypeRejected = !hasOut(*n, "Result");   // output never set
    }
    ++checks; if (!wrongTypeRejected) ++fails;
    printf("[node-lib]   %-18s %s\n", "wrong-type neg-ctrl", wrongTypeRejected ? "REJECTED (no output)  ok" : "PRODUCED OUTPUT  FAIL");

    const bool pass = fails == 0 && checks > 20;
    printf("[node-lib] %d node checks, %d FAIL, worst err=%.3e  %s\n", checks, fails, worst,
           pass ? "ALL PASS (library nodes match closed-form; wrong-type rejected)" : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::nodes
