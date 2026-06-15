// NodeLibraryGate.cpp -- Phase 1 GATE NODE-UI + Phase 3 GATE NODE-LIB. NODE-UI now drives the REAL Qt
// dial binding end-to-end (setValue on the dial -> fromTick -> setParam -> process -> output), so a
// mis-wired widget fails. NODE-LIB asserts every existing library node's output vs closed-form AND
// requires a finite output was actually produced -- a node whose compute() never calls setOutput (the
// "instantiates but does nothing" regression) now FAILS instead of being masked by a dropped NaN.

#include "Node.hpp"
#include "NodeFactory.hpp"
#include "NodeWidgets.hpp"

#include <QWidget>
#include <QApplication>
#include <QAbstractSlider>

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

void feedF(Node& n, const char* port, float v) { PortDataPacket pk; pk.data = v; pk.type = { "", "" }; n.setInput(port, pk); }
void feedAny(Node& n, const char* port, const std::any& a) { PortDataPacket pk; pk.data = a; pk.type = { "", "" }; n.setInput(port, pk); }
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
// drive the REAL dial/slider bound to `param` on a freshly-built widget -> fires valueChanged -> the
// connected lambda -> fromTick -> setParam -> process. Returns the param value the node ended up with.
double driveDial(QWidget* w, const char* param, double min, double max, double value) {
    for (QAbstractSlider* s : w->findChildren<QAbstractSlider*>())
        if (s->property("krs_param").toString() == QString(param)) { s->setValue(krs::nodeui::toTick(min, max, value)); }
    return 0.0;
}
} // namespace

// =================================================================================================
// GATE NODE-UI (Phase 1)
// =================================================================================================
bool runNodeUiGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[node-ui] GATE NODE-UI -- the REAL in-node dial drives the node output (end-to-end) + bounded footprint\n");

    // ---- NU1: drive the actual QDial bound to "freq" -> param -> output (the full widget chain) ----
    double bindFreqErr = 9e9, bindOutErr = 9e9; bool builtWidget = false;
    if (QApplication::instance()) {
        if (auto g = NodeFactory::instance().createNode("gen_sine")) {
            QWidget* w = g->createCustomWidget();            // builds dials + wires the binding lambdas
            if (w) {
                builtWidget = true;
                const double target = 5.0;                   // drive the Freq dial to 5 Hz THROUGH the widget
                driveDial(w, "freq", 0.0, 10.0, target);     // fires valueChanged -> setParam("freq", fromTick)
                const double gotFreq = g->getParam<double>("freq", -99.0);
                g->setParam<double>("amp", 1.0); g->setParam<double>("phase", 0.0);
                feedF(*g, "t", 0.13f); g->process();
                bindFreqErr = std::abs(gotFreq - target);                 // dial reached the param
                bindOutErr  = std::abs(outD(*g, "Out") - std::sin(TAU * gotFreq * 0.13));  // output uses it
                delete w;
            }
        }
    }
    const bool nu1ok = builtWidget && bindFreqErr < 0.02 && bindOutErr < 1e-6;

    // ---- NU1 NEG-CTRL (disconnected widget): driving node A's dial must NOT change node B's output ----
    bool ndisc = false;
    if (QApplication::instance()) {
        auto a = NodeFactory::instance().createNode("gen_sine");
        auto b = NodeFactory::instance().createNode("gen_sine");
        if (a && b) {
            feedF(*b, "t", 0.2f); b->process(); const double bBefore = outD(*b, "Out");
            QWidget* wa = a->createCustomWidget();
            if (wa) { driveDial(wa, "amp", 0.0, 5.0, 4.0); delete wa; }     // move A's amp dial only
            feedF(*b, "t", 0.2f); b->process(); const double bAfter = outD(*b, "Out");
            ndisc = std::abs(bAfter - bBefore) < 1e-12 && std::abs(a->getParam<double>("amp", -1) - 4.0) < 0.02;
        }
    }

    // ---- NU2: in-node widget footprint stays bounded (the real Qt widget size <= gated estimate) ----
    using krs::nodeui::ControlSpec;
    auto mk = [](int n) { std::vector<ControlSpec> v;
        for (int i = 0; i < n; ++i) v.push_back({ ControlSpec::Slider, "Label", "p", 0, 1, 0.01, 0 }); return v; };
    int maxW = 0, maxH = 0; bool bounded = true;
    for (int n : { 1, 2, 3, 6, 12 }) { const auto fp = krs::nodeui::estimateFootprint(mk(n));
        maxW = std::max(maxW, fp.w); maxH = std::max(maxH, fp.h);
        if (fp.w > krs::nodeui::kMaxW || fp.h > krs::nodeui::kMaxH) bounded = false; }
    const auto naive = krs::nodeui::naiveFootprint(mk(12));
    const bool capBites = naive.h > krs::nodeui::kMaxH;
    bool realBounded = true; int realW = 0, realH = 0;
    if (QApplication::instance())
        if (auto node = NodeFactory::instance().createNode("gen_sine"))
            if (QWidget* w = node->createCustomWidget()) {
                realW = w->width(); realH = w->height();
                realBounded = realW > 0 && realH > 0 && realW <= krs::nodeui::kMaxW && realH <= krs::nodeui::kMaxH;
                delete w;
            }
    const bool nu2ok = bounded && capBites && realBounded;

    const bool pass = nu1ok && ndisc && nu2ok;
    printf("[node-ui]   NU1 REAL dial->param->output: freq dial err=%.2e, output err=%.2e (widget built=%s)  %s\n",
           bindFreqErr, bindOutErr, builtWidget ? "yes" : "NO", nu1ok ? "PASS" : "FAIL");
    printf("[node-ui]   NEG-CTRL drive A's dial -> B output unchanged  %s\n", ndisc ? "PASS" : "FAIL!");
    printf("[node-ui]   NU2 footprint: max %dx%d (cap %dx%d), real gen widget %dx%d %s ; uncapped h=%d>cap %s\n",
           maxW, maxH, krs::nodeui::kMaxW, krs::nodeui::kMaxH, realW, realH, nu2ok ? "PASS" : "FAIL",
           naive.h, capBites ? "REJECTS" : "VACUOUS!");
    printf("[node-ui] %s\n", pass ? "ALL PASS (real dial drives output; A's dial inert on B; bounded footprint)"
                                   : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

// =================================================================================================
// GATE NODE-LIB (Phase 3) -- existing library nodes' outputs vs closed-form (NaN-poisoned: a node that
// produces no output FAILS)
// =================================================================================================
bool runNodeLibraryGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[node-lib] GATE NODE-LIB -- existing math/signal/logic nodes vs closed-form (do-nothing node FAILS)\n");

    auto MK = [](const char* id) { return NodeFactory::instance().createNode(id); };
    double worst = 0.0; int checks = 0, fails = 0; int sawOutput = 0;
    // accumulate error, POISONING to 9e9 on a non-finite (missing) output so a do-nothing node fails.
    auto acc = [](double& e, double got, double ref) { if (!std::isfinite(got)) e = 9e9; else e = std::max(e, std::abs(got - ref)); };
    auto track = [&](const char* what, double err, double tol) {
        worst = std::max(worst, err); ++checks; if (std::isfinite(err) && err < 9e8) ++sawOutput;
        const bool ok = err < tol; if (!ok) ++fails;
        printf("[node-lib]   %-18s max err=%.3e (tol %.0e)  %s\n", what, err, tol, ok ? "ok" : "FAIL"); };

    auto bin = [&](const char* id, const char* pa, const char* pb, const char* outp, auto ref, double tol) {
        if (auto n = MK(id)) { double e = 0;
            for (double a = -2.5; a <= 2.5; a += 0.5) for (double b = -2.0; b <= 2.0; b += 0.5) {
                feedF(*n, pa, float(a)); feedF(*n, pb, float(b)); n->process(); acc(e, outD(*n, outp), ref(a, b)); }
            track(id, e, tol); } };
    auto un = [&](const char* id, const char* pin, auto ref, double lo, double hi, double tol) {
        if (auto n = MK(id)) { double e = 0;
            for (double v = lo; v <= hi; v += (hi - lo) / 20.0) { feedF(*n, pin, float(v)); n->process(); acc(e, outD(*n, "Result"), ref(v)); }
            track(id, e, tol); } };

    bin("math_add", "A", "B", "Result", [](double a, double b) { return a + b; }, 1e-5);
    bin("math_subtract", "A", "B", "Result", [](double a, double b) { return a - b; }, 1e-5);
    bin("math_multiply", "A", "B", "Result", [](double a, double b) { return a * b; }, 1e-5);
    if (auto n = MK("math_divide")) { double e = 0; for (double a = -2.5; a <= 2.5; a += 0.5)
        for (double b = -2.0; b <= 2.0; b += 0.5) { if (std::abs(b) < 1e-6) continue;
            feedF(*n, "Numerator", float(a)); feedF(*n, "Denominator", float(b)); n->process(); acc(e, outD(*n, "Result"), a / b); } track("math_divide", e, 1e-4); }
    if (auto n = MK("math_modulo")) { double e = 0; for (double a = -2.5; a <= 2.5; a += 0.5)
        for (double b = 0.5; b <= 2.0; b += 0.5) { feedF(*n, "A", float(a)); feedF(*n, "B", float(b)); n->process(); acc(e, outD(*n, "Result"), std::fmod(a, b)); } track("math_modulo", e, 1e-4); }
    if (auto n = MK("math_pow")) { double e = 0; for (double a = 0.2; a <= 3.0; a += 0.4)
        for (double b = -2.0; b <= 3.0; b += 0.5) { feedF(*n, "Base", float(a)); feedF(*n, "Exponent", float(b)); n->process(); acc(e, outD(*n, "Result"), std::pow(a, b)); } track("math_pow", e, 1e-3); }
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
        feedF(*n, "Input", float(v)); feedF(*n, "Min", 0.f); feedF(*n, "Max", 1.f); n->process(); acc(e, outD(*n, "Result"), std::min(1.0, std::max(0.0, v))); } track("math_clamp", e, 1e-6); }
    if (auto n = MK("math_lerp")) { double e = 0; for (double t = 0; t <= 1; t += 0.1) {
        feedF(*n, "A", 2.f); feedF(*n, "B", 8.f); feedF(*n, "Alpha", float(t)); n->process(); acc(e, outD(*n, "Result"), 2 + 6 * t); } track("math_lerp", e, 1e-5); }
    if (auto n = MK("math_equals")) { double e = 0;
        feedF(*n, "A", 1.5f); feedF(*n, "B", 1.5f); feedF(*n, "Tolerance", 1e-3f); n->process(); acc(e, outD(*n, "Result"), 1.0);
        feedF(*n, "A", 1.5f); feedF(*n, "B", 1.7f); feedF(*n, "Tolerance", 1e-3f); n->process(); acc(e, outD(*n, "Result"), 0.0); track("math_equals", e, 0.5); }

    auto gen = [&](const char* id, auto ref) {
        if (auto n = MK(id)) { n->setParam<double>("freq", 1.3); n->setParam<double>("amp", 1.7); n->setParam<double>("phase", 0.3); double e = 0;
            for (double t = 0; t <= 2.0; t += 0.011) { feedF(*n, "t", float(t)); n->process();
                acc(e, outD(*n, "Out"), 1.7 * ref(TAU * 1.3 * t + 0.3)); } track(id, e, 1e-6); } };
    gen("gen_sine", [](double x) { return std::sin(x); });
    gen("gen_square", [](double x) { return std::sin(x) >= 0 ? 1.0 : -1.0; });
    gen("gen_triangle", [](double x) { double p = x / TAU; p -= std::floor(p); return 4.0 * std::abs(p - 0.5) - 1.0; });
    gen("gen_saw", [](double x) { double p = x / TAU; p -= std::floor(p); return 2.0 * p - 1.0; });
    if (auto n = MK("signal_waveform")) { double e = 0; const double f = 2.0, a = 1.5;
        for (double t = 0; t <= 1.5; t += 0.017) { feedF(*n, "Time", float(t)); feedF(*n, "Frequency", float(f)); feedF(*n, "Amplitude", float(a)); n->process();
            acc(e, outD(*n, "Output"), std::sin(t * f * 2.0 * 3.14159f) * a); } track("signal_waveform", e, 1e-3); }

    // NEG-CTRL (wrong-typed-input-rejected): a math_add fed a STRING for A produces NO output.
    bool wrongTypeRejected = false;
    if (auto n = MK("math_add")) { feedAny(*n, "A", std::string("not_a_float")); feedF(*n, "B", 2.0f); n->process();
        wrongTypeRejected = !hasOut(*n, "Result"); }
    ++checks; if (!wrongTypeRejected) ++fails;
    printf("[node-lib]   %-18s %s\n", "wrong-type neg-ctrl", wrongTypeRejected ? "REJECTED (no output)  ok" : "PRODUCED OUTPUT  FAIL");

    // every positive check must have observed a finite output (a do-nothing node would drop sawOutput).
    const int posChecks = checks - 1;   // excludes the wrong-type neg-ctrl
    const bool allProduced = sawOutput == posChecks;
    const bool pass = fails == 0 && posChecks > 20 && allProduced;
    printf("[node-lib] %d checks, %d FAIL, %d/%d produced a finite output, worst err=%.3e  %s\n",
           checks, fails, sawOutput, posChecks, worst,
           pass ? "ALL PASS (nodes match closed-form; every node produced output; wrong-type rejected)" : "FAILURES PRESENT");
    fflush(stdout);
    return pass;
}

} // namespace krs::nodes
