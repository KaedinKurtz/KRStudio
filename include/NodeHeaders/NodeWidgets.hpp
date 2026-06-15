#pragma once
// NodeWidgets.hpp -- Phase 1 in-node mini-UI framework. A node declares a list of ControlSpec (a dial,
// slider, spinbox or readout, each bound to a node PARAM by name). buildControlWidget() turns that list
// into a compact, FIXED-SIZE Qt widget whose every control writes node->setParam(param, value) then
// node->process() -- so the widget's value provably drives the node output (gate NODE-UI). The widget is
// sized to estimateFootprint(), the SSOT the sizing gate asserts is BOUNDED -- so the gated number IS the
// rendered widget's bounds (not a proxy). estimateFootprint is pure C++ (no Qt) so it gates headlessly.

#include <string>
#include <vector>
#include <algorithm>

class Node;
class QWidget;

namespace krs::nodeui {

struct ControlSpec {
    enum Kind { Slider, Dial, SpinBox, Readout };
    Kind kind = Slider;
    std::string label;      // shown next to the control
    std::string param;      // the node param this control writes (read by compute())
    double min = 0.0, max = 1.0, step = 0.01, def = 0.0;
};

struct Footprint { int w = 0; int h = 0; };

inline int controlRowH(ControlSpec::Kind k) {
    switch (k) { case ControlSpec::Dial: return 96; case ControlSpec::Readout: return 22; default: return 30; }
}
inline int controlNatW(const ControlSpec& c) {
    const int labelW = int(c.label.size()) * 7 + 10;
    const int ctlW = (c.kind == ControlSpec::Dial) ? 92 : 124;
    return labelW + ctlW;
}

// Hard caps -- a node body never renders wider/taller than this no matter how many controls it declares.
constexpr int kMaxW = 220, kMaxH = 300;

// Continuous value <-> integer slider/dial tick (1000 steps). Exposed so a test can drive the REAL dial
// binding (setValue(toTick(...)) fires the connected lambda through fromTick()).
inline int toTick(double min, double max, double v) {
    const double t = (max > min) ? (v - min) / (max - min) : 0.0;
    const double c = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    return int(c * 1000.0 + 0.5);
}
inline double fromTick(double min, double max, int tick) { return min + (double(tick) / 1000.0) * (max - min); }

// The footprint the widget builder sizes itself to AND the gate asserts is bounded. Capped.
inline Footprint estimateFootprint(const std::vector<ControlSpec>& controls) {
    int w = 60, h = 6;
    for (const auto& c : controls) { w = std::max(w, controlNatW(c)); h += controlRowH(c.kind) + 4; }
    return { std::min(w, kMaxW), std::min(h, kMaxH) };
}

// The PRE-FIX (uncapped) footprint -- grows without bound as controls/labels grow. The sizing gate's
// negative control: for a content-heavy node this EXCEEDS the cap, proving the cap is load-bearing.
inline Footprint naiveFootprint(const std::vector<ControlSpec>& controls) {
    int w = 60, h = 6;
    for (const auto& c : controls) { w = std::max(w, controlNatW(c)); h += controlRowH(c.kind) + 4; }
    return { w, h };   // NO cap
}

// Build a compact, fixed-size widget hosting the controls; each writes node->setParam(param, value) then
// node->process(). Sized to estimateFootprint(controls). Requires Qt (returns a QWidget for embedding).
QWidget* buildControlWidget(Node* node, const std::vector<ControlSpec>& controls);

} // namespace krs::nodeui
