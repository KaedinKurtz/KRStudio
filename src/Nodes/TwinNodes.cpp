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

// ---- PROPERTY node: entity handle + property combo -> value + frequency ----
class PropertyNode : public Node {
public:
    PropertyNode() {
        m_id = "twin_property";
        m_ports.push_back({ "Object",    { "entt::entity", "handle" }, Port::Direction::Input,  this });
        m_ports.push_back({ "Value",     { "double", "unitless" },     Port::Direction::Output, this });
        m_ports.push_back({ "Vector",    { "glm::vec3", "unitless" },  Port::Direction::Output, this });
        m_ports.push_back({ "Frequency", { "double", "Hz" },           Port::Direction::Output, this });
    }
    QWidget* createCustomWidget() override;

    void compute() override {
        // reset outputs first: an early-return must leave NO stale output packet
        // (so a non-firing compute cannot pass on a previous value).
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
        setOutput<double>("Value", e->v[0]);
        setOutput<glm::vec3>("Vector", glm::vec3(float(e->v[0]), float(e->v[1]), float(e->v[2])));
        // stale-aware publish frequency, read at the catalog's real current time
        // (NOT a frozen 0) so the output falls toward 0 when this stream stalls.
        setOutput<double>("Frequency", cat.frequency(m_objId, prop, cat.now()));
        m_hasValue = true;
    }
    const std::vector<std::string>& properties() const { return m_props; }
    bool hasValue() const { return m_hasValue; }
    bool haveObject() const { return m_haveObj; }
    std::uint32_t objectId() const { return m_objId; }
private:
    std::vector<std::string> m_props;
    bool m_hasValue = false, m_haveObj = false;
    std::uint32_t m_objId = 0;
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
        if (!combo->currentText().isEmpty()) setParam<std::string>("prop", combo->currentText().toStdString());
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
        prop.setParam<std::string>("prop", std::string("mass")); prop.process();
        const double valMass = readOutD(prop, "Value");
        const float ecsMass = reg.get<RigidBodyComponent>(A).mass;
        const bool massOk = prop.hasValue() && std::abs(valMass - double(ecsMass)) < 1e-6;

        prop.setParam<std::string>("prop", std::string("position")); prop.process();
        const glm::vec3 valPos = readOutV(prop, "Vector");
        const glm::vec3 ecsPos = reg.get<TransformComponent>(A).translation;
        const bool posOk = prop.hasValue() && glm::length(valPos - ecsPos) < 1e-6f;

        // a vec3 dynamics property through the node (Vector output) vs the ECS.
        prop.setParam<std::string>("prop", std::string("linearVelocity")); prop.process();
        const glm::vec3 valVel = readOutV(prop, "Vector");
        const bool velOk = prop.hasValue() && glm::length(valVel - reg.get<RigidBodyComponent>(A).linearVelocity) < 1e-6f;

        // changing the Object re-populates the Property list for the NEW object.
        ObjectNode obj2; obj2.setScene(&scene); obj2.setParam<int>("objId", int(std::uint32_t(entt::to_integral(B)))); obj2.process();
        PropertyNode prop2; wireObject(obj2, prop2); prop2.setParam<std::string>("prop", std::string("mass")); prop2.process();
        const bool repopOk = prop2.hasValue() && prop2.properties().size() == 7
                          && std::abs(readOutD(prop2, "Value") - double(reg.get<RigidBodyComponent>(B).mass)) < 1e-6
                          && prop2.objectId() == std::uint32_t(entt::to_integral(B));

        // NEG-CTRL: disconnected Object input -> no output.
        PropertyNode prop3; prop3.setParam<std::string>("prop", std::string("mass")); prop3.process();
        const bool discOk = !prop3.hasValue();

        const bool ok = massOk && posOk && velOk && repopOk && discOk;
        printf("[twin]   OBJECT-PROPERTY-NODES: mass=%.6f (ecs %.6f, ok:%d); position ok:%d; linearVelocity ok:%d; "
               "re-populate-on-new-object:%d; disconnected->no-output:%d  %s\n",
               valMass, double(ecsMass), int(massOk), int(posOk), int(velOk), int(repopOk), int(discOk), ok ? "PASS" : "FAIL");
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
        PropertyNode pn; wireObject(on, pn); pn.setParam<std::string>("prop", std::string("mass")); pn.process();
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

} // namespace krs::twin
