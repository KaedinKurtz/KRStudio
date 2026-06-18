// ===========================================================================
// Phase 4 -- VELOCITY-PROBE ORB node (krs::orb). Bound 1:1 to a transparent glass
// sphere in the scene (spawned by the nodeCreated hook). Each frame the GL-side
// orb-probe system writes the average velocity of the fluid particles inside the
// sphere into the orb's OrbBindingComponent; this node relays that to its outputs
// and pushes its Radius back to the orb so the operator can resize the probe from
// the graph. The orb<->node binding is keyed by the QtNodes NodeId (param
// "orbNodeId", set by the create hook); the velocity query + binding logic live in
// krs::orb (OrbProbe) and are proven by GATE ORB-VELOCITY / GATE ORB-LIFECYCLE.
// ===========================================================================
#include "Node.hpp"
#include "NodeFactory.hpp"
#include "Scene.hpp"
#include "OrbProbe.hpp"
#include "components.hpp"

#include <glm/glm.hpp>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <memory>

namespace krs::orb {
namespace {

class VelocityProbeOrbNode : public Node {
public:
    VelocityProbeOrbNode() {
        m_id = "velocity_probe_orb";
        m_ports.push_back({ "Radius",   { "double", "m" },     Port::Direction::Input,  this }); // resize the probe
        m_ports.push_back({ "Velocity", { "glm::vec3", "m/s" }, Port::Direction::Output, this });
        m_ports.push_back({ "Speed",    { "double", "m/s" },    Port::Direction::Output, this });
        m_ports.push_back({ "Count",    { "int", "unitless" },  Port::Direction::Output, this });
    }

    void compute() override {
        if (!m_scene) return;
        auto& reg = m_scene->getRegistry();
        const std::uint64_t nodeId = std::uint64_t(getParam<long long>("orbNodeId", 0));
        if (nodeId == 0) return;                                  // not yet bound by the create hook
        const entt::entity e = findOrbForNode(reg, nodeId);
        if (e == entt::null || !reg.valid(e)) return;             // orb gone (or not spawned yet)

        // OWNERSHIP RECONCILIATION for the probe radius (== TransformComponent.scale.x). Three writers must NOT
        // fight: a wired Radius, the in-node Radius widget, and the transform gizmo. The old code wrote
        // xf->scale = radius EVERY tick, which clobbered any gizmo scale on the next eval. Precedence:
        //   1. Radius WIRED   -> the graph owns the size (a connection overrides everything).
        //   2. else widget literal CHANGED since our last read -> the operator typed a radius -> apply it.
        //   3. else           -> the gizmo (or steady state) owns it: READ the live scale back so a gizmo
        //                        resize persists and feeds the query volume + outputs.
        auto* xf = reg.try_get<TransformComponent>(e);
        const double storedRadius = getParam<double>("radius", 0.5);
        const double scaleRadius  = xf ? double(xf->scale.x) : storedRadius;

        bool radiusWired = false;                                 // a live CONNECTION packet (not just the literal)?
        for (const auto& p : m_ports)
            if (p.direction == Port::Direction::Input && p.name == "Radius" && p.packet.has_value()) { radiusWired = true; break; }

        const double widgetRadius = literalD("Radius", scaleRadius);   // in-node spin box (ignores wires)
        const bool   widgetChanged = (m_lastWidgetRadius >= 0.0) && std::abs(widgetRadius - m_lastWidgetRadius) > 1e-6;

        double radius;
        if (radiusWired)        radius = std::clamp(getInputD("Radius", scaleRadius), 0.01, 5.0);  // wire wins
        else if (widgetChanged) radius = std::clamp(widgetRadius, 0.01, 5.0);                      // operator typed
        else                    radius = std::clamp(scaleRadius, 0.01, 5.0);                       // gizmo / steady

        if (xf) xf->scale = glm::vec3(float(radius));             // reconciled + uniform (re-squares a per-axis drag)
        setParam<double>("radius", radius);                       // scale.x; the world/query radius is 0.5*this
        m_lastWidgetRadius = widgetRadius;                        // remember the literal as observed this tick

        // Relay the GL-side measured velocity to the outputs. ob.radius (the query radius = 0.5*scale) is owned
        // by OrbProbeSystem each GL frame -- the node only reconciles the transform scale, not the query field.
        const auto& ob = reg.get<OrbBindingComponent>(e);
        setOutput<glm::vec3>("Velocity", ob.measuredVelocity);
        setOutput<double>("Speed", double(glm::length(ob.measuredVelocity)));
        setOutput<int>("Count", ob.containedCount);
    }

private:
    double m_lastWidgetRadius = -1.0;   // last-observed in-node Radius literal (-1 = first run: read the scale)
};

struct VelocityProbeOrbRegistrar {
    VelocityProbeOrbRegistrar() {
        NodeFactory::instance().registerNodeType("velocity_probe_orb",
            { "Velocity Probe Orb", "Twin",
              "A transparent glass sphere probe: outputs the average velocity of the fluid particles inside its volume." },
            []() { return std::make_unique<VelocityProbeOrbNode>(); });
    }
};
static VelocityProbeOrbRegistrar g_velocityProbeOrbRegistrar;

} // namespace

// GATE ORB-OWNERSHIP (KRS_ORBOWN_SELFTEST): the probe radius (== TransformComponent.scale.x) reconciles the
// three writers -- a wired Radius, the in-node Radius widget, and the transform GIZMO -- so the node never
// clobbers a gizmo resize. Drives the REAL compute() on a real entt registry (no GL). The neg-control is the
// OLD behavior (compute wrote xf->scale = param every tick), which would RESET a gizmo scale back to the param.
bool runOrbOwnershipGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[orbown] GATE ORB-OWNERSHIP -- probe radius reconciles wire/gizmo/widget; the node no longer clobbers the gizmo\n");

    Scene scene;
    auto& reg = scene.getRegistry();
    const std::uint64_t nodeId = 42;
    const entt::entity e = reg.create();
    decorateProbeOrb(reg, e, nodeId, glm::vec3(0.4f, 0.6f, 0.9f), glm::vec3(0.0f), 0.5f);   // scale = 0.5

    VelocityProbeOrbNode node;
    node.setScene(&scene);
    node.setParam<long long>("orbNodeId", (long long)nodeId);
    node.setParam<double>("radius", 0.5);
    node.setPortLiteral<double>("Radius", 0.5);   // a stable widget literal (else literalD defaults to scale -> ambiguous)

    auto scaleX = [&] { return double(reg.get<TransformComponent>(e).scale.x); };

    // STEADY: a settled scale must not drift when nothing changed.
    node.process();
    const bool steadyOk = std::abs(scaleX() - 0.5) < 1e-9;

    // GIZMO: simulate a gizmo scale write; the node must READ it back, not overwrite it with the param.
    reg.get<TransformComponent>(e).scale = glm::vec3(2.0f);
    node.process();
    const double afterGizmo = scaleX();
    const bool gizmoOk = std::abs(afterGizmo - 2.0) < 1e-6 && std::abs(node.getParam<double>("radius", 0.0) - 2.0) < 1e-6;
    // NEG-CTRL: the OLD code wrote xf->scale = param (0.5) every tick -> it would RESET the gizmo's 2.0 to 0.5.
    const double oldClobberWouldBe = 0.5;
    const bool gizmoNeg = std::abs(oldClobberWouldBe - afterGizmo) > 0.1;   // the fix genuinely changed behavior

    // WIDGET: the operator types a radius into the in-node spin box (the literal) -> it applies (overrides scale).
    node.setPortLiteral<double>("Radius", 0.75);
    node.process();
    const bool widgetOk = std::abs(scaleX() - 0.75) < 1e-6;

    // WIRE: a connection on Radius overrides both the gizmo and the widget.
    { PortDataPacket pk; pk.data = 1.25; pk.type = { "double", "m" }; node.setInput("Radius", pk); }
    node.process();
    const bool wireOk = std::abs(scaleX() - 1.25) < 1e-6;

    // WIRE RELEASED: the gizmo regains control -- a subsequent gizmo scale is read back (no snap-to-wire).
    node.clearInputPacket("Radius");
    reg.get<TransformComponent>(e).scale = glm::vec3(1.8f);
    node.process();
    const bool wireReleaseOk = std::abs(scaleX() - 1.8) < 1e-6;

    const bool pass = steadyOk && gizmoOk && gizmoNeg && widgetOk && wireOk && wireReleaseOk;
    printf("[orbown]   steady-hold:%d; GIZMO 2.0 persists:%d (param=%.3f) [old-clobber->0.5 differs:%d]; "
           "WIDGET 0.75 applies:%d; WIRE 1.25 overrides:%d; wire-released->gizmo 1.8 read-back:%d  %s\n",
           int(steadyOk), int(gizmoOk), node.getParam<double>("radius", 0.0), int(gizmoNeg),
           int(widgetOk), int(wireOk), int(wireReleaseOk), pass ? "PASS" : "FAIL");
    printf("[orbown] %s\n", pass ? "ALL PASS (gizmo scale persists; wire overrides; widget applies; node no longer clobbers the gizmo)"
                                  : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::orb
