// ===========================================================================
// Avoidance-field sprint, Phase 1 — generalized Object / Property nodes
// (krs::twin). An OBJECT node = a combo of scene bodies -> an entity handle; a
// PROPERTY node = an entity handle + a combo of THAT object's published catalog
// properties -> the live value + the publish-frequency (for stale gating). The
// combo selection is a node PARAM (the headless-gateable SSOT); the QComboBox is
// a thin binding over it that repopulates from the live scene/catalog on popup.
// ===========================================================================
#include "Node.hpp"
#include "NodeFactory.hpp"
#include "PropertyCatalog.hpp"
#include "Scene.hpp"

#include <cstdio>
#include <cmath>
#include <memory>
#include <algorithm>
#include <QComboBox>
#include <QString>
#include <QMenu>
#include <QAction>

namespace krs::twin {

namespace {

// ---- OBJECT node: combo of scene bodies -> entt::entity handle ----
class ObjectNode : public Node {
public:
    ObjectNode() {
        m_id = "twin_object";
        m_ports.push_back({ "Object", { "entt::entity", "handle" }, Port::Direction::Output, this });
    }
    bool needsExecutionControls() const override { return false; }
    QWidget* createCustomWidget() override;

    void compute() override {
        if (!m_scene) return;
        auto& reg = m_scene->getRegistry();
        // keep the catalog fresh from the live scene so the Property node reads current introspection+values.
        publishSceneState(catalog(), reg, m_time, m_dt, m_accel);
        m_time += m_dt;
        const int sel = getParam<int>("objId", -1);
        if (sel < 0) return;                                  // no selection -> no output
        const entt::entity e = entt::entity(std::uint32_t(sel));
        if (!reg.valid(e)) return;                            // stale/invalid -> no output
        setOutput<entt::entity>("Object", e);
    }
private:
    double m_time = 0.0, m_dt = 1.0 / 60.0;
    AccelTracker m_accel;
};

// A QComboBox that repopulates its body list from the live scene each time it
// opens (the scene is injected AFTER the widget is built, so build-time is empty).
class ObjectCombo : public QComboBox {
public:
    explicit ObjectCombo(ObjectNode* n) : m_node(n) {}
    // Embedded in a QGraphicsProxyWidget, the native combo dropdown mis-positions / is unclickable under
    // the node view's transform. Show a QMenu at the combo's screen position instead -- mapToGlobal goes
    // through the proxy + view, so it lands right under the box, and QMenu runs its own robust popup.
    void showPopup() override {
        repopulate();
        if (count() == 0) return;
        auto* menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->setMinimumWidth(width());
        for (int i = 0; i < count(); ++i) {
            QAction* a = menu->addAction(itemText(i));
            QObject::connect(a, &QAction::triggered, this, [this, i] { setCurrentIndex(i); });
        }
        menu->popup(mapToGlobal(QPoint(0, height())));   // non-blocking; appears beneath the combo
    }
    void repopulate() {
        const int keep = currentData().isValid() ? currentData().toInt() : -1;
        clear();
        if (Scene* s = m_node->scene()) {
            auto& reg = s->getRegistry();
            for (auto e : reg.view<TagComponent>())
                addItem(QString::fromStdString(reg.get<TagComponent>(e).tag),
                        int(std::uint32_t(entt::to_integral(e))));
        }
        for (int i = 0; i < count(); ++i) if (itemData(i).toInt() == keep) { setCurrentIndex(i); break; }
    }
private:
    ObjectNode* m_node;
};

QWidget* ObjectNode::createCustomWidget() {
    auto* combo = new ObjectCombo(this);
    // body tags ("Velocity Probe Orb", "Basin.Wall+X", ...) are long -- give the combo room so the
    // name is not clipped, and let it size to its longest entry.
    combo->setMinimumWidth(170);
    combo->setMinimumContentsLength(22);
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->repopulate();
    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this, combo](int) {
        if (combo->currentData().isValid()) setParam<int>("objId", combo->currentData().toInt());
    });
    return combo;
}

// ---- PROPERTY node: entity handle + property combo. The OUTPUT ports ADAPT to the selected property's
//      component type -- X/Y/Z for a vector quantity (position, linear/angular velocity + acceleration),
//      Roll/Pitch/Yaw (deg) for orientation, a single Value for a scalar (mass) -- always plus the
//      stale-aware publish Frequency. (Per-component scalars are what a numeric readout/gauge can consume:
//      the old single "Value" only carried v[0]=X, which is ~0 for a body falling along -Y.)
class PropertyNode : public Node {
public:
    PropertyNode() {
        m_id = "twin_property";                // base ctor already added the "Trigger" input
        m_ports.push_back({ "Object", { "entt::entity", "handle" }, Port::Direction::Input, this });
        setOutputPorts(PropType::Vec3);        // default layout until a property is chosen (most props are vec3)
    }
    QWidget* createCustomWidget() override;

    // a property's component layout (the catalog is the SSOT: mass=Scalar, orientation=Quat, else Vec3).
    static PropType typeOfProperty(const std::string& p) {
        if (p == "mass") return PropType::Scalar;
        if (p == "orientation") return PropType::Quat;
        return PropType::Vec3;
    }

    // (re)build only the OUTPUT ports for a component type, leaving the inputs (Trigger, Object) AND their
    // live packets/connections intact -- so changing the property does not drop the wired object.
    void setOutputPorts(PropType t) {
        m_ports.erase(std::remove_if(m_ports.begin(), m_ports.end(),
            [](const Port& p) { return p.direction == Port::Direction::Output; }), m_ports.end());
        auto out = [&](const char* n, const char* u) { m_ports.push_back({ n, { "double", u }, Port::Direction::Output, this }); };
        switch (t) {
        case PropType::Scalar: out("Value", "unitless"); break;
        case PropType::Quat:   out("Roll", "deg"); out("Pitch", "deg"); out("Yaw", "deg");
                               // ALSO expose the orientation as a first-class quaternion (no euler precision loss),
                               // so it can bake straight into a Transform / feed IK without an RPY round-trip.
                               m_ports.push_back({ "Quaternion", { "glm::quat", "quat" }, Port::Direction::Output, this }); break;
        default:               out("X", "unitless"); out("Y", "unitless"); out("Z", "unitless"); break;
        }
        out("Frequency", "Hz");
        m_outType = t;
    }

    // Select a property: store it + reconfigure the output ports IF the layout changed. changePorts brackets
    // the mutation in the QtNodes port signals (live) or applies it directly (headless gate).
    void selectProperty(const std::string& prop) {
        setParam<std::string>("prop", prop);
        const PropType t = typeOfProperty(prop);
        if (t != m_outType) changePorts([this, t] { setOutputPorts(t); });
    }
    bool selectNamedOption(const std::string& opt) override { selectProperty(opt); return true; }

    void compute() override {
        // reset outputs first: an early-return must leave NO stale output packet.
        for (auto& p : m_ports) if (p.direction == Port::Direction::Output) p.packet.reset();
        m_props.clear(); m_hasValue = false; m_objId = 0; m_haveObj = false;
        const auto objOpt = getInput<entt::entity>("Object");
        if (!objOpt) return;                                  // disconnected -> no output
        m_objId = std::uint32_t(entt::to_integral(*objOpt)); m_haveObj = true;
        const auto& cat = catalog();
        if (!cat.hasObject(m_objId)) return;
        m_props = cat.propertiesOf(m_objId);                  // property combo options (per object)
        const std::string prop = getParam<std::string>("prop", m_props.empty() ? std::string() : m_props.front());
        const PropertyEntry* e = cat.get(m_objId, prop);
        if (!e) return;                                       // invalid property for this object -> no output
        // emit the outputs matching the CURRENT port layout (kept in sync with the selection via selectProperty).
        if (m_outType == PropType::Scalar) {
            setOutput<double>("Value", e->v[0]);
        } else if (m_outType == PropType::Quat) {
            double r, p, y; quatToRPY(e->v[0], e->v[1], e->v[2], e->v[3], r, p, y);
            const double k = 180.0 / 3.14159265358979323846;
            setOutput<double>("Roll", r * k); setOutput<double>("Pitch", p * k); setOutput<double>("Yaw", y * k);
            // the catalog stores orientation as (w,x,y,z); emit it as a raw quaternion too.
            setOutput<glm::quat>("Quaternion", glm::quat(float(e->v[0]), float(e->v[1]), float(e->v[2]), float(e->v[3])));
        } else {
            setOutput<double>("X", e->v[0]); setOutput<double>("Y", e->v[1]); setOutput<double>("Z", e->v[2]);
        }
        // stale-aware publish frequency, read at the catalog's real current time (falls toward 0 on a stall).
        setOutput<double>("Frequency", cat.frequency(m_objId, prop, cat.now()));
        m_hasValue = true;
    }
    const std::vector<std::string>& properties() const { return m_props; }
    bool hasValue() const { return m_hasValue; }
    bool haveObject() const { return m_haveObj; }
    std::uint32_t objectId() const { return m_objId; }
    PropType outType() const { return m_outType; }
private:
    // quaternion (w,x,y,z) -> roll(x)/pitch(y)/yaw(z) radians, the standard Tait-Bryan ZYX extraction.
    static void quatToRPY(double w, double x, double y, double z, double& roll, double& pitch, double& yaw) {
        const double sr = 2.0 * (w * x + y * z), cr = 1.0 - 2.0 * (x * x + y * y);
        roll = std::atan2(sr, cr);
        const double sp = 2.0 * (w * y - z * x);
        pitch = (std::abs(sp) >= 1.0) ? std::copysign(1.57079632679489661923, sp) : std::asin(sp);
        const double sy = 2.0 * (w * z + x * y), cy = 1.0 - 2.0 * (y * y + z * z);
        yaw = std::atan2(sy, cy);
    }
    std::vector<std::string> m_props;
    bool m_hasValue = false, m_haveObj = false;
    std::uint32_t m_objId = 0;
    PropType m_outType = PropType::Vec3;
};

class PropertyCombo : public QComboBox {
public:
    explicit PropertyCombo(PropertyNode* n) : m_node(n) {}
    void showPopup() override {                          // QMenu popup (see ObjectCombo for the why)
        repopulate();
        if (count() == 0) return;
        auto* menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->setMinimumWidth(width());
        for (int i = 0; i < count(); ++i) {
            QAction* a = menu->addAction(itemText(i));
            QObject::connect(a, &QAction::triggered, this, [this, i] { setCurrentIndex(i); });
        }
        menu->popup(mapToGlobal(QPoint(0, height())));
    }
    void repopulate() {
        const QString keep = currentText();
        clear();
        if (m_node->haveObject())
            for (const auto& p : m_node->properties()) addItem(QString::fromStdString(p));
        const int idx = findText(keep); if (idx >= 0) setCurrentIndex(idx);
    }
private:
    PropertyNode* m_node;
};

QWidget* PropertyNode::createCustomWidget() {
    auto* combo = new PropertyCombo(this);
    combo->setMinimumWidth(150);
    combo->setMinimumContentsLength(18);
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->repopulate();
    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this, combo](int) {
        if (!combo->currentText().isEmpty()) selectProperty(combo->currentText().toStdString());  // reconfigures output ports
    });
    return combo;
}

struct TwinRegistrar {
    TwinRegistrar() {
        NodeFactory::instance().registerNodeType("twin_object",
            { "Object", "Twin", "Selects a scene body; outputs its entity handle." },
            []() { return std::make_unique<ObjectNode>(); });
        NodeFactory::instance().registerNodeType("twin_property",
            { "Property", "Twin", "Reads a published property of the wired object: live value + publish frequency." },
            []() { return std::make_unique<PropertyNode>(); });
    }
};
static TwinRegistrar g_twinRegistrar;

// ---- gate helpers ----
double readOutD(Node& n, const std::string& port) {
    for (const auto& p : n.getPorts())
        if (p.name == port && p.direction == Port::Direction::Output && p.packet)
            try { return std::any_cast<double>(p.packet->data); } catch (...) {}
    return std::nan("");
}
glm::vec3 readOutV(Node& n, const std::string& port) {
    for (const auto& p : n.getPorts())
        if (p.name == port && p.direction == Port::Direction::Output && p.packet)
            try { return std::any_cast<glm::vec3>(p.packet->data); } catch (...) {}
    return glm::vec3(1e9f);
}
// the Property node now exposes vector quantities as X/Y/Z scalar gates -- reassemble them.
glm::vec3 readOutXYZ(Node& n) {
    return glm::vec3(float(readOutD(n, "X")), float(readOutD(n, "Y")), float(readOutD(n, "Z")));
}
void wireObject(Node& src, Node& dst) {
    for (const auto& p : src.getPorts())
        if (p.name == "Object" && p.direction == Port::Direction::Output && p.packet) { dst.setInput("Object", *p.packet); return; }
}

} // namespace

bool runTwinGate() {
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[twin] GATE TWIN -- ECS->catalog publishing + Object/Property nodes + stale-aware frequency\n");
    catalog().clear();
    bool allOk = true;

    Scene scene; auto& reg = scene.getRegistry();
    auto mk = [&](const char* name, glm::vec3 pos, glm::vec3 vel, float mass) {
        entt::entity e = reg.create();
        reg.emplace<TagComponent>(e).tag = name;
        reg.emplace<TransformComponent>(e).translation = pos;
        auto& rb = reg.emplace<RigidBodyComponent>(e); rb.mass = mass; rb.linearVelocity = vel;
        return e;
    };
    const entt::entity A = mk("alpha", glm::vec3(1, 2, 3),  glm::vec3(0.5f, 0, 0), 4.0f);
    const entt::entity B = mk("beta",  glm::vec3(-1, 0, 2), glm::vec3(0, 0, 0),    2.0f);
    (void)mk("gamma", glm::vec3(0, 5, 0), glm::vec3(0, 0, -1.0f), 1.0f);
    // non-trivial orientation + angular velocity on alpha so every component of
    // every property is value-checked (a swapped/mis-ordered publish is caught).
    reg.get<TransformComponent>(A).rotation = glm::normalize(glm::quat(0.1f, 0.2f, 0.3f, 0.4f));
    reg.get<RigidBodyComponent>(A).angularVelocity = glm::vec3(0.7f, -0.3f, 0.2f);

    AccelTracker acc; publishSceneState(catalog(), reg, 0.0, 1.0 / 60.0, acc);
    const std::uint32_t idA = std::uint32_t(entt::to_integral(A));

    // ---- PUBLISH-CATALOG (names + ALL-7 values + accel finite-diff + neg-ctrls) ----
    {
        auto props = catalog().propertiesOf(idA);
        auto expected = canonicalProperties();
        std::sort(props.begin(), props.end()); std::sort(expected.begin(), expected.end());
        const bool listOk = (props == expected);

        // VALUE coverage: every published property matches the live ECS (not just
        // its name) -- closes the "right names, wrong values" pass-while-broken hole.
        const auto& rbA = reg.get<RigidBodyComponent>(A);
        const auto& xfA = reg.get<TransformComponent>(A);
        auto v3ok = [&](const char* p, glm::vec3 ref) {
            const auto* e = catalog().get(idA, p);
            return e && std::abs(e->v[0]-ref.x)<1e-6 && std::abs(e->v[1]-ref.y)<1e-6 && std::abs(e->v[2]-ref.z)<1e-6;
        };
        const auto* eo = catalog().get(idA, "orientation");
        const bool quatOk = eo && std::abs(eo->v[0]-xfA.rotation.w)<1e-6 && std::abs(eo->v[1]-xfA.rotation.x)<1e-6
                              && std::abs(eo->v[2]-xfA.rotation.y)<1e-6 && std::abs(eo->v[3]-xfA.rotation.z)<1e-6;
        const auto* em = catalog().get(idA, "mass");
        const bool valuesOk = v3ok("position", xfA.translation) && quatOk
                           && v3ok("linearVelocity", rbA.linearVelocity)
                           && v3ok("angularVelocity", rbA.angularVelocity)
                           && (em && std::abs(em->v[0]-double(rbA.mass))<1e-6)
                           && v3ok("linearAcceleration", glm::vec3(0))     // 0 on first observation
                           && v3ok("angularAcceleration", glm::vec3(0));

        const bool missOk = !catalog().hasObject(0xDEADBEEFu) && catalog().propertiesOf(0xDEADBEEFu).empty();
        const bool phantomOk = catalog().get(idA, "fooBar") == nullptr;

        // two-step ACCELERATION finite-difference: change velocity, re-publish at t=dt.
        const double dt = 1.0 / 240.0;
        reg.get<RigidBodyComponent>(A).linearVelocity += glm::vec3(2.0f, 0, 0);   // dv = +2 m/s
        publishSceneState(catalog(), reg, dt, dt, acc);
        const auto* ea = catalog().get(idA, "linearAcceleration");
        const bool accelOk = ea && std::abs(ea->v[0] - 2.0/dt) < 1e-3*(2.0/dt)
                                && std::abs(ea->v[1]) < 1e-3 && std::abs(ea->v[2]) < 1e-3;

        const bool ok = listOk && valuesOk && missOk && phantomOk && accelOk && catalog().objectCount() == 3;
        printf("[twin]   PUBLISH-CATALOG: %zu props (==7:%d); all-7 values match ECS:%d; accel dv/dt=%.1f (want %.1f):%d; "
               "non-existent empty:%d; phantom absent:%d; objects=%d  %s\n",
               props.size(), int(listOk), int(valuesOk), ea?ea->v[0]:-1.0, 2.0/dt, int(accelOk),
               int(missOk), int(phantomOk), catalog().objectCount(), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- OBJECT-PROPERTY-NODES ----
    {
        ObjectNode obj; obj.setScene(&scene); obj.setParam<int>("objId", int(idA)); obj.process();
        PropertyNode prop; wireObject(obj, prop);
        // mass is a SCALAR property -> exactly one "Value" output gate (and no X/Y/Z).
        prop.selectProperty("mass"); prop.process();
        const double valMass = readOutD(prop, "Value");
        const float ecsMass = reg.get<RigidBodyComponent>(A).mass;
        const bool scalarPorts = prop.outType() == PropType::Scalar
                              && std::isnan(readOutD(prop, "X")) && std::isnan(readOutD(prop, "Y"));   // no xyz for a scalar
        const bool massOk = prop.hasValue() && std::abs(valMass - double(ecsMass)) < 1e-6 && scalarPorts;

        // position is a VECTOR property -> X/Y/Z scalar gates that reassemble to the ECS vec3.
        prop.selectProperty("position"); prop.process();
        const glm::vec3 valPos = readOutXYZ(prop);
        const glm::vec3 ecsPos = reg.get<TransformComponent>(A).translation;
        const bool posOk = prop.hasValue() && prop.outType() == PropType::Vec3 && glm::length(valPos - ecsPos) < 1e-6f;

        // linearVelocity -> X/Y/Z; critically each component (esp. Y) is exposed, so a body falling along
        // -Y drives a NONZERO scalar gate (the operator bug: the old single "Value" only carried X ~= 0).
        prop.selectProperty("linearVelocity"); prop.process();
        const glm::vec3 valVel = readOutXYZ(prop);
        const glm::vec3 ecsVel = reg.get<RigidBodyComponent>(A).linearVelocity;
        const bool velOk = prop.hasValue() && glm::length(valVel - ecsVel) < 1e-6f
                        && std::abs(readOutD(prop, "Y") - double(ecsVel.y)) < 1e-6;   // Y gate carries vy exactly

        // orientation is a QUAT property -> Roll/Pitch/Yaw gates (no X/Y/Z, no scalar Value).
        prop.selectProperty("orientation"); prop.process();
        const bool rpyPorts = prop.outType() == PropType::Quat
                           && !std::isnan(readOutD(prop, "Roll")) && !std::isnan(readOutD(prop, "Pitch"))
                           && !std::isnan(readOutD(prop, "Yaw")) && std::isnan(readOutD(prop, "X"));

        // changing the Object re-populates the Property list for the NEW object.
        ObjectNode obj2; obj2.setScene(&scene); obj2.setParam<int>("objId", int(std::uint32_t(entt::to_integral(B)))); obj2.process();
        PropertyNode prop2; wireObject(obj2, prop2); prop2.selectProperty("mass"); prop2.process();
        const bool repopOk = prop2.hasValue() && prop2.properties().size() == 7
                          && std::abs(readOutD(prop2, "Value") - double(reg.get<RigidBodyComponent>(B).mass)) < 1e-6
                          && prop2.objectId() == std::uint32_t(entt::to_integral(B));

        // NEG-CTRL: disconnected Object input -> no output.
        PropertyNode prop3; prop3.selectProperty("mass"); prop3.process();
        const bool discOk = !prop3.hasValue();

        const bool ok = massOk && posOk && velOk && rpyPorts && repopOk && discOk;
        printf("[twin]   OBJECT-PROPERTY-NODES: mass=%.6f (ecs %.6f, scalar-port-only:%d); position xyz ok:%d; "
               "linearVelocity xyz ok (Y gate carries vy):%d; orientation rpy-ports:%d; re-populate:%d; disconnected->no-output:%d  %s\n",
               valMass, double(ecsMass), int(massOk), int(posOk), int(velOk), int(rpyPorts), int(repopOk), int(discOk), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- FREQUENCY-OUTPUT (stale guard) ----
    {
        PropertyCatalog c;
        const double rate = 200.0, dtp = 1.0 / rate;
        double tp = 0.0, val[1] = { 0 };
        for (int k = 0; k < 8; ++k) { val[0] = k; c.publish(7u, "obj", "mass", PropType::Scalar, val, tp); tp += dtp; }
        const double tLast = tp - dtp;
        const double reportedFresh = c.frequency(7u, "mass", tLast + dtp);   // ~200
        const double reportedStalled = c.frequency(7u, "mass", tLast + 1.0); // after 1 s of silence -> ~1
        const double measuredFrozen = c.get(7u, "mass")->measuredHz();       // the naive value -> frozen ~200
        const bool freshOk = std::abs(reportedFresh - rate) < 0.05 * rate;
        const bool staleOk = reportedStalled < 0.1 * rate;
        const bool negOk = std::abs(measuredFrozen - rate) < 0.05 * rate;    // naive freezes (the bad model)

        // NODE-LEVEL: the PropertyNode's Frequency OUTPUT must fall on a stall too
        // (exercises the live node path; catches a frozen-now node-frequency bug).
        catalog().clear();
        Scene sf; auto& rf = sf.getRegistry();
        const entt::entity X = rf.create(); rf.emplace<TagComponent>(X).tag = "x";
        rf.emplace<TransformComponent>(X); rf.emplace<RigidBodyComponent>(X).mass = 1.0f;
        ObjectNode on; on.setScene(&sf); on.setParam<int>("objId", int(std::uint32_t(entt::to_integral(X))));
        for (int k = 0; k < 8; ++k) on.process();                            // publishes at the node's cadence
        PropertyNode pn; wireObject(on, pn); pn.selectProperty("mass"); pn.process();
        const double nodeFreshHz = readOutD(pn, "Frequency");
        catalog().setNow(catalog().now() + 1.0);                             // 1 s passes, X not re-published
        pn.process();
        const double nodeStaleHz = readOutD(pn, "Frequency");
        const bool nodeOk = nodeFreshHz > 5.0 && nodeStaleHz < 0.1 * nodeFreshHz;

        const bool ok = freshOk && staleOk && negOk && nodeOk;
        printf("[twin]   FREQUENCY-OUTPUT: fresh=%.2f Hz (~%.0f, ok:%d); 1s stall=%.2f Hz (->0, ok:%d); "
               "NEG naive frozen=%.2f (ok:%d); NODE fresh=%.2f stalled=%.2f (drops:%d)  %s\n",
               reportedFresh, rate, int(freshOk), reportedStalled, int(staleOk), measuredFrozen, int(negOk),
               nodeFreshHz, nodeStaleHz, int(nodeOk), ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    printf("[twin] %s\n", allOk ? "ALL PASS (catalog introspection; Object/Property value fidelity; stale-aware frequency)"
                                 : "FAILURES PRESENT");
    fflush(stdout);
    return allOk;
}

// GATE QUATERNION-OUTPUT (KRS_QUATOUT_SELFTEST): the rigid-body Property node's NEW quaternion output matches the
// body's actual orientation; baking position+that-quat into a Transform round-trips losslessly. NEG = a stale quat.
bool runQuaternionOutputGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[quatout] GATE QUATERNION-OUTPUT -- rigid-body Property node emits orientation as a quaternion + bakes a Transform\n");
    catalog().clear();

    Scene scene; auto& reg = scene.getRegistry();
    const entt::entity A = reg.create();
    reg.emplace<TagComponent>(A).tag = "arm_flange";
    const glm::vec3 knownPos(0.3f, -0.4f, 1.2f);
    const glm::quat knownRot = glm::normalize(glm::quat(0.2f, 0.5f, -0.3f, 0.1f));   // (w,x,y,z)
    auto& xf = reg.emplace<TransformComponent>(A); xf.translation = knownPos; xf.rotation = knownRot;
    reg.emplace<RigidBodyComponent>(A).mass = 2.0f;
    AccelTracker acc; publishSceneState(catalog(), reg, 0.0, 1.0 / 60.0, acc);

    ObjectNode obj; obj.setScene(&scene); obj.setParam<int>("objId", int(std::uint32_t(entt::to_integral(A)))); obj.process();
    PropertyNode prop; wireObject(obj, prop); prop.selectProperty("orientation"); prop.process();

    auto readQuat = [](Node& n, const char* port) -> std::optional<glm::quat> {
        for (const auto& p : n.getPorts())
            if (p.name == port && p.direction == Port::Direction::Output && p.packet)
                try { return std::any_cast<glm::quat>(p.packet->data); } catch (...) {}
        return std::nullopt;
    };
    // q and -q are the SAME rotation -> compare via |dot| ~ 1.
    auto sameRot = [](const glm::quat& a, const glm::quat& b) { return std::abs(glm::dot(glm::normalize(a), glm::normalize(b))) > 0.99999f; };

    const auto qOpt = readQuat(prop, "Quaternion");
    const bool quatMatchesBody = qOpt && sameRot(*qOpt, knownRot);
    const glm::quat qOut = qOpt.value_or(glm::quat(1, 0, 0, 0));

    // bake position + that quat into a Transform via the REAL transform_bake node -> round-trip read-back.
    krs::RigidTransform baked{}; bool haveBaked = false;
    if (auto bake = NodeFactory::instance().createNode("transform_bake")) {
        { PortDataPacket pk; pk.data = knownPos; pk.type = { "glm::vec3", "m" };    bake->setInput("Position", pk); }
        { PortDataPacket pk; pk.data = qOut;     pk.type = { "glm::quat", "quat" }; bake->setInput("Orientation", pk); }
        bake->process();
        for (const auto& p : bake->getPorts())
            if (p.name == "Transform" && p.direction == Port::Direction::Output && p.packet)
                try { baked = std::any_cast<krs::RigidTransform>(p.packet->data); haveBaked = true; } catch (...) {}
    }
    const bool roundTripOk = haveBaked && glm::length(baked.position - knownPos) < 1e-5f && sameRot(baked.rotation, knownRot);

    // NEG-CTRL: a STALE/wrong quaternion (identity) does NOT match the body's actual orientation.
    const bool negOk = !sameRot(glm::quat(1, 0, 0, 0), knownRot);

    const bool pass = quatMatchesBody && roundTripOk && negOk;
    printf("[quatout]   Property 'orientation' quat matches body:%d; bake pos+quat->Transform round-trips:%d; "
           "NEG stale identity-quat mismatches:%d  %s\n",
           int(quatMatchesBody), int(roundTripOk), int(negOk), pass ? "PASS" : "FAIL");
    printf("[quatout] %s\n", pass ? "ALL PASS (rigid-body quaternion output matches actual orientation; bakes+reads back losslessly; stale quat fails)"
                                  : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::twin
