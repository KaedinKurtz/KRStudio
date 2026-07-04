// ===========================================================================
// EnvironmentNodes.cpp -- see EnvironmentNodes.hpp. Node-graph control of the
// scene environment: an Environment node (sun/IBL/exposure/skybox/fog -> ctx) and
// a Light node (a target light entity's LightComponent + emissive glow).
// ===========================================================================
#include "Node.hpp"
#include "NodeFactory.hpp"
#include "Scene.hpp"
#include "EnvironmentNodes.hpp"
#include "components.hpp"    // EnvironmentSettings, SceneProperties, LightComponent, MaterialComponent, TagComponent

#include <cstdio>
#include <cmath>
#include <memory>
#include <string>

namespace krs::envnode {

namespace {

// ---------------------------------------------------------------------------
// Environment node: writes the global environment ctx from its inputs.
// Unconnected inputs default to the CURRENT ctx value, so wiring one port drives
// only that knob and leaves the rest as-authored.
// ---------------------------------------------------------------------------
class EnvironmentNode : public Node {
public:
    EnvironmentNode() {
        m_id = "environment_settings";
        m_ports.push_back({ "Sun Intensity", { "double",    "unitless" }, Port::Direction::Input, this });
        m_ports.push_back({ "Sun Color",     { "glm::vec3", "linear"   }, Port::Direction::Input, this });
        m_ports.push_back({ "Sun Direction", { "glm::vec3", "unitless" }, Port::Direction::Input, this });
        m_ports.push_back({ "IBL Intensity", { "double",    "unitless" }, Port::Direction::Input, this });
        m_ports.push_back({ "Exposure EV",   { "double",    "EV"       }, Port::Direction::Input, this });
        m_ports.push_back({ "Skybox",        { "bool",      "unitless" }, Port::Direction::Input, this });
        m_ports.push_back({ "Fog",           { "bool",      "unitless" }, Port::Direction::Input, this });
        // literal defaults so the in-node spinboxes show real values.
        setPortLiteral<float>("Sun Intensity", 1.0f);
        setPortLiteral<float>("IBL Intensity", 1.0f);
        setPortLiteral<float>("Exposure EV",   0.0f);
    }
    void compute() override {
        if (!m_scene) return;
        auto& reg = m_scene->getRegistry();
        auto* env = reg.ctx().find<EnvironmentSettings>();
        if (!env) env = &reg.ctx().emplace<EnvironmentSettings>();
        auto* props = reg.ctx().find<SceneProperties>();
        if (!props) props = &reg.ctx().emplace<SceneProperties>();

        env->sunIntensity = float(getInputD("Sun Intensity", env->sunIntensity));
        env->iblIntensity = float(getInputD("IBL Intensity", env->iblIntensity));
        env->exposureEV   = float(getInputD("Exposure EV",   env->exposureEV));
        if (auto c = getInput<glm::vec3>("Sun Color"))     env->sunColor = *c;
        if (auto d = getInput<glm::vec3>("Sun Direction")) env->sunDirection = *d;
        if (auto s = getInput<bool>("Skybox"))             env->drawSkybox = *s;
        if (auto f = getInput<bool>("Fog"))                props->fogEnabled = *f;

        env->nodeDriven = true;   // tell the app to push these into the live renderer this pass
    }
};

// ---------------------------------------------------------------------------
// Light node: drives one light's LightComponent (+ emissive glow). Target = a
// wired entt::entity, or a light named by the "light" param (TagComponent match).
// ---------------------------------------------------------------------------
class LightNode : public Node {
public:
    LightNode() {
        m_id = "light_control";
        m_ports.push_back({ "Light",     { "entt::entity", "handle"   }, Port::Direction::Input, this }); // target
        m_ports.push_back({ "Color",     { "glm::vec3",    "linear"   }, Port::Direction::Input, this });
        m_ports.push_back({ "Intensity", { "double",       "unitless" }, Port::Direction::Input, this });
        m_ports.push_back({ "Range",     { "double",       "m"        }, Port::Direction::Input, this });
        m_ports.push_back({ "Enable",    { "bool",         "unitless" }, Port::Direction::Input, this });
        setPortLiteral<float>("Intensity", 1.0f);
        setPortLiteral<float>("Range",     0.0f);
    }
    void compute() override {
        if (!m_scene) return;
        auto& reg = m_scene->getRegistry();

        // Resolve the target: a wired entity wins; else a light named by the "light" param.
        entt::entity e = entt::null;
        if (auto obj = getInput<entt::entity>("Light"); obj && reg.valid(*obj)) e = *obj;
        if (e == entt::null) {
            const std::string name = getParam<std::string>("light", std::string());
            if (!name.empty()) {
                for (auto ent : reg.view<LightComponent, TagComponent>())
                    if (reg.get<TagComponent>(ent).tag == name) { e = ent; break; }
            }
        }
        if (e == entt::null || !reg.valid(e)) return;   // no target -> touch nothing (NEG-CTRL)

        auto& lc = reg.get_or_emplace<LightComponent>(e);
        lc.color     = getInput<glm::vec3>("Color").value_or(lc.color);
        lc.intensity = float(getInputD("Intensity", lc.intensity));
        lc.range     = float(getInputD("Range",     lc.range));
        if (auto en = getInput<bool>("Enable")) lc.enabled = *en;

        // Keep the visible bulb/panel glowing the commanded colour (matches spawnLightEmitter).
        if (auto* mat = reg.try_get<MaterialComponent>(e)) {
            mat->albedoColor   = lc.color;
            mat->emissiveColor = lc.color;
        }
    }
};

struct EnvNodeRegistrar {
    EnvNodeRegistrar() {
        NodeFactory::instance().registerNodeType("environment_settings",
            { "Environment", "Scene/Environment",
              "Drive the scene environment: sun intensity/color/direction, IBL, exposure EV, skybox, fog." },
            []() { return std::make_unique<EnvironmentNode>(); });
        NodeFactory::instance().registerNodeType("light_control",
            { "Light", "Scene/Environment",
              "Drive a light's colour / intensity / range / enable (target = wired entity or the \"light\" name param)." },
            []() { return std::make_unique<LightNode>(); });
    }
};
static EnvNodeRegistrar g_envNodeRegistrar;

// --- gate helpers -------------------------------------------------------------
void feedD(Node& n, const std::string& port, double v) {
    PortDataPacket pk; pk.data = v; pk.type = { "double", "unitless" }; n.setInput(port, pk);
}
void feedV(Node& n, const std::string& port, const glm::vec3& v) {
    PortDataPacket pk; pk.data = v; pk.type = { "glm::vec3", "linear" }; n.setInput(port, pk);
}
void feedB(Node& n, const std::string& port, bool v) {
    PortDataPacket pk; pk.data = v; pk.type = { "bool", "unitless" }; n.setInput(port, pk);
}
void feedEntity(Node& n, entt::entity e) {
    PortDataPacket pk; pk.data = e; pk.type = { "entt::entity", "handle" }; n.setInput("Light", pk);
}

} // namespace

// ================================================================================================
// GATE ENVNODE -- environment + light node compute writes the scene, headless.
// ================================================================================================
bool runEnvironmentNodesGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[envnode] GATE ENVNODE -- Environment node drives sun/IBL/exposure/skybox/fog ctx; Light node drives a light\n");
    bool allOk = true;

    // ---- ENVIRONMENT node ----
    {
        Scene scene; auto& reg = scene.getRegistry();
        // Pre-condition: no environment ctx yet (proves the write is non-vacuous).
        const bool preAbsent = (reg.ctx().find<EnvironmentSettings>() == nullptr);

        EnvironmentNode en; en.setScene(&scene);
        feedD(en, "Sun Intensity", 3.0); feedV(en, "Sun Color", glm::vec3(1.0f, 0.5f, 0.2f));
        feedV(en, "Sun Direction", glm::vec3(0.3f, -1.0f, 0.2f)); feedD(en, "IBL Intensity", 2.0);
        feedD(en, "Exposure EV", 1.0); feedB(en, "Skybox", false); feedB(en, "Fog", true);
        en.process();

        const auto* env = reg.ctx().find<EnvironmentSettings>();
        const auto* props = reg.ctx().find<SceneProperties>();
        const bool wrote = env && props
            && std::abs(env->sunIntensity - 3.0f) < 1e-5 && std::abs(env->iblIntensity - 2.0f) < 1e-5
            && std::abs(env->exposureEV - 1.0f) < 1e-5 && !env->drawSkybox
            && glm::length(env->sunColor - glm::vec3(1.0f, 0.5f, 0.2f)) < 1e-5
            && glm::length(env->sunDirection - glm::vec3(0.3f, -1.0f, 0.2f)) < 1e-5
            && props->fogEnabled && env->nodeDriven;

        // Unconnected-input no-op: a second node with only IBL wired leaves sun as default.
        Scene s2; EnvironmentNode en2; en2.setScene(&s2);
        feedD(en2, "IBL Intensity", 5.0); en2.process();
        const auto* env2 = s2.getRegistry().ctx().find<EnvironmentSettings>();
        const bool partial = env2 && std::abs(env2->iblIntensity - 5.0f) < 1e-5
                          && std::abs(env2->sunIntensity - 1.0f) < 1e-5;   // sun kept its default

        const bool ok = preAbsent && wrote && partial;
        printf("[envnode]   ENVIRONMENT: ctx-absent-before=%d wrote(sun/ibl/ev/skybox/color/dir/fog/nodeDriven)=%d "
               "unconnected-keeps-default=%d  %s\n",
               int(preAbsent), int(wrote), int(partial), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- LIGHT node (wired entity + by-name) ----
    {
        Scene scene; auto& reg = scene.getRegistry();
        // a light entity: LightComponent + name + an emissive material to glow.
        entt::entity e = reg.create();
        reg.emplace<LightComponent>(e);
        reg.emplace<TagComponent>(e).tag = "KeyLight";
        reg.emplace<MaterialComponent>(e);

        LightNode ln; ln.setScene(&scene);
        feedEntity(ln, e); feedV(ln, "Color", glm::vec3(0.0f, 1.0f, 0.0f));
        feedD(ln, "Intensity", 7.0); feedD(ln, "Range", 4.0); feedB(ln, "Enable", false);
        ln.process();

        const auto& lc = reg.get<LightComponent>(e);
        const auto& mat = reg.get<MaterialComponent>(e);
        const bool wiredOk = glm::length(lc.color - glm::vec3(0, 1, 0)) < 1e-5
            && std::abs(lc.intensity - 7.0f) < 1e-5 && std::abs(lc.range - 4.0f) < 1e-5 && !lc.enabled
            && glm::length(mat.emissiveColor - glm::vec3(0, 1, 0)) < 1e-5;

        // by-NAME target: no entity wired, "light" param names the same light; drive it red + on.
        LightNode ln2; ln2.setScene(&scene);
        ln2.setParam<std::string>("light", std::string("KeyLight"));
        feedV(ln2, "Color", glm::vec3(1.0f, 0.0f, 0.0f)); feedD(ln2, "Intensity", 2.0); feedB(ln2, "Enable", true);
        ln2.process();
        const bool nameOk = glm::length(lc.color - glm::vec3(1, 0, 0)) < 1e-5
            && std::abs(lc.intensity - 2.0f) < 1e-5 && lc.enabled;

        // NEG-CTRL: a Light node with NO target (no wire, no name) creates no light anywhere.
        Scene s3; auto& r3 = s3.getRegistry();
        const entt::entity bare = r3.create(); (void)bare;   // a bare entity that must NOT gain a LightComponent
        LightNode ln3; ln3.setScene(&s3);
        feedV(ln3, "Color", glm::vec3(1, 1, 1)); feedD(ln3, "Intensity", 9.0); ln3.process();
        int lights = 0; for (auto ent : r3.view<LightComponent>()) { (void)ent; ++lights; }
        const bool negOk = (lights == 0);

        const bool ok = wiredOk && nameOk && negOk;
        printf("[envnode]   LIGHT: wired-entity(color/intensity/range/enable/glow)=%d by-name-target=%d "
               "no-target->no-phantom-light=%d  %s\n",
               int(wiredOk), int(nameOk), int(negOk), negOk ? (ok ? "PASS" : "FAIL") : "VACUOUS!");
        allOk = allOk && ok;
    }

    printf("[envnode] %s\n", allOk ? "ALL PASS (Environment node writes sun/IBL/exposure/skybox/fog ctx + nodeDriven; "
                                     "Light node drives a wired + a named light's LightComponent + emissive glow; no-target no-ops)"
                                    : "FAILURES PRESENT");
    fflush(stdout);
    return allOk;
}

} // namespace krs::envnode
