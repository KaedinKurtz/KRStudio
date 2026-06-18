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

        // Radius (wired port wins over the stored param) -> resize the probe sphere.
        double radius = getInputD("Radius", getParam<double>("radius", 0.25));
        radius = std::clamp(radius, 0.01, 5.0);
        setParam<double>("radius", radius);
        if (auto* xf = reg.try_get<TransformComponent>(e)) xf->scale = glm::vec3(float(radius));

        // Relay the GL-side measured velocity to the outputs.
        const auto& ob = reg.get<OrbBindingComponent>(e);
        setOutput<glm::vec3>("Velocity", ob.measuredVelocity);
        setOutput<double>("Speed", double(glm::length(ob.measuredVelocity)));
        setOutput<int>("Count", ob.containedCount);
    }
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
} // namespace krs::orb
