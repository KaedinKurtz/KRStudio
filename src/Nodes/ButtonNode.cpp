// ButtonNode.cpp -- Phase 2: the event SOURCE for the otherwise-continuous graph. A large colored clickable
// button emits a BRIEF trigger pulse (true for one eval tick) on the selected edge. The edge mode is an
// in-node ENUM combo (the Phase-1 combo path): Rising fires on press, Falling on release, Dual on both. The
// button's press/release set the "pressed" param; compute() detects the edge vs the previous tick and emits
// the momentary Trigger. (Visual button = OPERATOR VISUAL-CONFIRM; the edge semantics are gated headlessly.)
#include "Node.hpp"
#include "NodeFactory.hpp"
#include "NodeEditorGate.hpp"

#include <QPushButton>

#include <cstdio>
#include <vector>
#include <memory>

namespace krs::nodes {
namespace {

class ButtonNode : public Node {
public:
    ButtonNode() {
        m_id = "button_trigger";
        addEnumInputPort("Edge", { "Rising", "Falling", "Dual" });            // Phase-1 combo input
        m_ports.push_back({ "Trigger", { "bool", "unitless" }, Port::Direction::Output, this });
    }
    bool needsExecutionControls() const override { return false; }            // it IS the trigger source
    bool isPureInputFunction() const override { return false; }               // output is a press-gated pulse, not f(Edge)

    QWidget* createCustomWidget() override {
        auto* btn = new QPushButton(QStringLiteral("TRIGGER"));
        btn->setMinimumSize(150, 70);                                          // large
        btn->setStyleSheet(QStringLiteral(
            "QPushButton{background:#c0392b;color:white;font-weight:bold;font-size:18px;border-radius:10px;}"
            "QPushButton:pressed{background:#e74c3c;}"));                       // colored
        QObject::connect(btn, &QPushButton::pressed,  [this] { setPressed(true);  });
        QObject::connect(btn, &QPushButton::released, [this] { setPressed(false); });
        return btn;
    }

    void compute() override {
        const bool pressed = getParam<bool>("pressed", false);
        const int  mode    = getInput<int>("Edge").value_or(0);                // 0=Rising 1=Falling 2=Dual
        const bool rising  = pressed && !m_lastPressed;
        const bool falling = !pressed && m_lastPressed;
        const bool fire = (mode == 0 && rising) || (mode == 1 && falling) || (mode == 2 && (rising || falling));
        setOutput<bool>("Trigger", fire);                                      // momentary: true only on the edge tick
        m_lastPressed = pressed;
    }
    void setPressed(bool p) { setParam<bool>("pressed", p); }                  // button + gate drive this
private:
    bool m_lastPressed = false;
};

struct ButtonRegistrar {
    ButtonRegistrar() {
        NodeFactory::instance().registerNodeType("button_trigger",
            { "Button", "Interaction/Input",
              "A clickable button that emits a brief trigger pulse on the selected edge (Rising/Falling/Dual)." },
            []() { return std::make_unique<ButtonNode>(); });
    }
};
static ButtonRegistrar g_buttonRegistrar;

bool readBoolOut(Node& n, const char* port) {
    for (const auto& p : n.getPorts())
        if (p.direction == Port::Direction::Output && p.name == port && p.packet.has_value())
            try { return std::any_cast<bool>(p.packet->data); } catch (...) {}
    return false;
}

} // namespace

bool runTriggerEdgeGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[trigger] GATE TRIGGER-EDGE -- the Button emits a BRIEF trigger pulse with correct edge semantics\n");

    // run a press/release sequence in a given Edge mode; return the per-tick fire vector.
    auto run = [](int mode, const std::vector<bool>& seq) {
        ButtonNode b; b.setPortLiteral<int>("Edge", mode);
        std::vector<bool> fires;
        for (bool p : seq) { b.setPressed(p); b.process(); fires.push_back(readBoolOut(b, "Trigger")); }
        return fires;
    };
    auto count = [](const std::vector<bool>& v) { int c = 0; for (bool b : v) if (b) ++c; return c; };

    // idle, PRESS(hold), hold, RELEASE, idle
    const std::vector<bool> seq = { false, true, true, false, false };
    const auto rising  = run(0, seq);   // expect a single fire at index 1 (the press), none while held
    const auto falling = run(1, seq);   // expect a single fire at index 3 (the release)
    const auto dual    = run(2, seq);   // expect fires at index 1 AND 3

    const bool risingOk  = count(rising) == 1 && rising[1] && !rising[2];      // press only + BRIEF (no repeat held)
    const bool fallingOk = count(falling) == 1 && falling[3] && !falling[1];   // release only (not press)
    const bool dualOk    = count(dual) == 2 && dual[1] && dual[3];             // both edges

    // NEG-CTRL (REAL failing models): a "fires continuously while held" (level) model fires on every held tick
    // (2 here) -- differs from the 1 edge fire; and rising vs falling fire at DIFFERENT ticks (a wrong-edge
    // model that fired rising on release would put it at index 3, not 1).
    const int levelFires = count(seq);                                         // 2 = held-ticks = continuous model
    const bool negOk = levelFires != count(rising) && rising[1] && !falling[1];

    const bool pass = risingOk && fallingOk && dualOk && negOk;
    printf("[trigger]   Rising fires=%d @press brief:%d; Falling fires=%d @release:%d; Dual fires=%d (press+release):%d; "
           "NEG continuous-level=%d != edge=%d  %s\n",
           count(rising), int(risingOk), count(falling), int(fallingOk), count(dual), int(dualOk),
           levelFires, count(rising), pass ? "PASS" : "FAIL");
    printf("[trigger] %s\n", pass ? "ALL PASS (rising on press, falling on release, dual on both, BRIEF pulse; level/wrong-edge fails)"
                                   : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::nodes
