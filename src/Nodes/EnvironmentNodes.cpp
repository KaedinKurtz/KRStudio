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

#include <QWidget>
#include <QCheckBox>
#include <QVBoxLayout>

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
        // Auto Sun: when true, the sun ports are IGNORED and the skybox-derived sun (the renderer's
        // Texture2D::analyzeHdrSun pipeline: brightest texel -> direction, surrounding region ->
        // colour) is RESTORED + kept authoritative -- "the sun follows the sky". Toggle/wire false
        // to drive the sun manually through the three ports above; re-enabling recovers the sky sun.
        m_ports.push_back({ "Auto Sun",      { "bool",      "unitless" }, Port::Direction::Input, this });
        // BOOL literals seeded TRUE in the ctor: the delegate auto-mounts a checkbox per bool port
        // and SEEDS the literal from it at mount -- an unset literal reads false, which is what used
        // to kill the skybox on drop-in. (The change-detection below means even these seeds apply
        // nothing until the USER toggles or wires the port.)
        setPortLiteral<bool>("Skybox",   true);
        setPortLiteral<bool>("Fog",      true);
        setPortLiteral<bool>("Auto Sun", true);
        // Numeric literals are NOT seeded here -- they seed from the CURRENT environment on first
        // compute, so dropping this node changes NOTHING until a knob is actually edited.
    }

    void compute() override {
        if (!m_scene) return;
        auto& reg = m_scene->getRegistry();
        auto* env = reg.ctx().find<EnvironmentSettings>();
        if (!env) env = &reg.ctx().emplace<EnvironmentSettings>();
        auto* props = reg.ctx().find<SceneProperties>();
        if (!props) props = &reg.ctx().emplace<SceneProperties>();

        // FIRST compute: seed numeric literals from the CURRENT settings (the app mirrors the live
        // renderer into the ctx while no node drives it) + snapshot the bool literals, so the node
        // starts as a pure no-op that SHOWS the current look instead of overwriting it.
        if (!m_seeded) {
            setPortLiteral<double>("Sun Intensity", double(env->sunIntensity));
            setPortLiteral<double>("IBL Intensity", double(env->iblIntensity));
            setPortLiteral<double>("Exposure EV",   double(env->exposureEV));
            m_lastSkybox = getInput<bool>("Skybox").value_or(true);
            m_lastFog    = getInput<bool>("Fog").value_or(true);
            m_seeded = true;
        }

        env->sunIntensity = float(getInputD("Sun Intensity", env->sunIntensity));
        env->iblIntensity = float(getInputD("IBL Intensity", env->iblIntensity));
        env->exposureEV   = float(getInputD("Exposure EV",   env->exposureEV));

        const bool autoSun = getInput<bool>("Auto Sun").value_or(true);
        if (autoSun) {
            // RESTORE + hold the skybox-derived sun (recovers after a spell of manual control).
            if (env->hasDerivedSun) {
                env->sunDirection = env->sunDerivedDirection;
                env->sunColor     = env->sunDerivedColor;
            }
        } else {                   // manual sun: the ports drive colour/direction
            if (auto c = getInput<glm::vec3>("Sun Color"))     env->sunColor = *c;
            if (auto d = getInput<glm::vec3>("Sun Direction")) env->sunDirection = *d;
        }

        // Skybox/Fog: CHANGE-DETECTION on the literal (an unwired checkbox applies only when the
        // user actually toggles it -- the mount-seed value never stomps the scene) ; a WIRED port
        // applies every pass (a wire is an explicit command).
        const bool skybox = getInput<bool>("Skybox").value_or(m_lastSkybox);
        if (portWired("Skybox") || skybox != m_lastSkybox) env->drawSkybox = skybox;
        m_lastSkybox = skybox;
        const bool fog = getInput<bool>("Fog").value_or(m_lastFog);
        if (portWired("Fog") || fog != m_lastFog) props->fogEnabled = fog;
        m_lastFog = fog;

        env->nodeDriven = true;   // tell the app to push these into the live renderer this pass
    }
private:
    bool portWired(const char* name) const {   // a live CONNECTION delivers a packet (literal = widget)
        for (const auto& p : getPorts())
            if (p.direction == Port::Direction::Input && p.name == name) return p.packet.has_value();
        return false;
    }
    bool m_seeded = false;        // literals seeded from the live environment on first compute
    bool m_lastSkybox = true;     // change detection for the unwired checkbox literals
    bool m_lastFog = true;
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
        feedB(en, "Auto Sun", false);   // manual-sun mode: the sun ports drive colour/direction
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

        // SEED-FROM-CURRENT: a node dropped into a scene whose environment is already customized
        // must be a NO-OP on first compute (the viewport-nuke fix): the pre-set values survive.
        Scene s3; auto& r3 = s3.getRegistry();
        { auto& e3 = r3.ctx().emplace<EnvironmentSettings>(); e3.iblIntensity = 2.5f; e3.exposureEV = 1.25f; e3.sunIntensity = 4.0f; }
        EnvironmentNode en3; en3.setScene(&s3); en3.process();             // NO inputs wired at all
        const auto* env3 = r3.ctx().find<EnvironmentSettings>();
        const bool seedOk = env3 && std::abs(env3->iblIntensity - 2.5f) < 1e-5
                         && std::abs(env3->exposureEV - 1.25f) < 1e-5
                         && std::abs(env3->sunIntensity - 4.0f) < 1e-5 && env3->nodeDriven;

        // AUTO SUN (default true): wired sun ports are IGNORED -- the skybox-derived sun persists.
        Scene s4; auto& r4 = s4.getRegistry();
        { auto& e4 = r4.ctx().emplace<EnvironmentSettings>(); e4.sunColor = glm::vec3(0.9f, 0.8f, 0.7f);
          e4.sunDirection = glm::vec3(0.1f, -1.0f, 0.0f); }
        EnvironmentNode en4; en4.setScene(&s4);
        feedV(en4, "Sun Color", glm::vec3(0, 1, 0)); feedV(en4, "Sun Direction", glm::vec3(1, 0, 0));
        en4.process();                                                     // Auto Sun defaults TRUE
        const auto* env4 = r4.ctx().find<EnvironmentSettings>();
        const bool autoSunOk = env4 && glm::length(env4->sunColor - glm::vec3(0.9f, 0.8f, 0.7f)) < 1e-5
                            && glm::length(env4->sunDirection - glm::vec3(0.1f, -1.0f, 0.0f)) < 1e-5;

        // AUTO SUN RECOVERY: manual control moved the sun; re-enabling Auto Sun RESTORES the
        // skybox-derived values (the cache the renderer mirrors into the ctx).
        Scene s5; auto& r5 = s5.getRegistry();
        { auto& e5 = r5.ctx().emplace<EnvironmentSettings>();
          e5.hasDerivedSun = true;
          e5.sunDerivedDirection = glm::vec3(0.2f, -0.9f, 0.1f);
          e5.sunDerivedColor     = glm::vec3(1.0f, 0.95f, 0.8f); }
        EnvironmentNode en5; en5.setScene(&s5);
        feedB(en5, "Auto Sun", false);                                     // manual spell...
        feedV(en5, "Sun Direction", glm::vec3(1, 0, 0)); feedV(en5, "Sun Color", glm::vec3(0, 0, 1));
        en5.process();
        const auto* env5 = r5.ctx().find<EnvironmentSettings>();
        const bool manualTook = env5 && glm::length(env5->sunDirection - glm::vec3(1, 0, 0)) < 1e-5;
        feedB(en5, "Auto Sun", true);                                      // ...then recover
        en5.process();
        const bool recoverOk = manualTook
            && glm::length(env5->sunDirection - glm::vec3(0.2f, -0.9f, 0.1f)) < 1e-5
            && glm::length(env5->sunColor - glm::vec3(1.0f, 0.95f, 0.8f)) < 1e-5;

        // SKYBOX drop-in survival: the ctor seeds the bool literal TRUE and change-detection means
        // an untouched checkbox never applies -- a scene with the skybox OFF keeps it off.
        Scene s6; auto& r6 = s6.getRegistry();
        { auto& e6 = r6.ctx().emplace<EnvironmentSettings>(); e6.drawSkybox = false; }
        EnvironmentNode en6; en6.setScene(&s6); en6.process();             // nothing wired/touched
        const bool dropInOk = !r6.ctx().find<EnvironmentSettings>()->drawSkybox;   // stays OFF

        const bool ok = preAbsent && wrote && partial && seedOk && autoSunOk && recoverOk && dropInOk;
        printf("[envnode]   ENVIRONMENT: ctx-absent-before=%d wrote(manual-sun/ibl/ev/skybox/fog/nodeDriven)=%d "
               "unconnected-keeps-default=%d seed-from-current=%d auto-sun-ignores-wires=%d auto-sun-RECOVERS=%d "
               "skybox-off-survives-drop-in=%d  %s\n",
               int(preAbsent), int(wrote), int(partial), int(seedOk), int(autoSunOk), int(recoverOk),
               int(dropInOk), ok ? "PASS" : "FAIL");
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
