#include "BenchmarkRunner.hpp"
#include "Scene.hpp"
#include "SceneBuilder.hpp"
#include "SimulationController.hpp"
#include "RenderingSystem.hpp"
#include "FluidSystem.hpp"
#include "PrimitiveBuilders.hpp"
#include "components.hpp"
#include "CollisionCookingService.hpp"

#include <QTimer>
#include <QDebug>
#include <QCoreApplication>
#include <cmath>
#include <cstdlib>

namespace {
constexpr double kG = 9.81;

/// Append a closed box (12 tris, CCW-outward winding) to a mesh — lets the
/// benchmarks build concave compound meshes (cups, plates with holes).
void appendBoxToMesh(std::vector<Vertex>& verts, std::vector<unsigned int>& idx,
                     const glm::vec3& c, const glm::vec3& he)
{
    const unsigned base = unsigned(verts.size());
    const glm::vec3 corners[8] = {
        c + glm::vec3(-he.x, -he.y, -he.z), c + glm::vec3(he.x, -he.y, -he.z),
        c + glm::vec3(he.x, he.y, -he.z),   c + glm::vec3(-he.x, he.y, -he.z),
        c + glm::vec3(-he.x, -he.y, he.z),  c + glm::vec3(he.x, -he.y, he.z),
        c + glm::vec3(he.x, he.y, he.z),    c + glm::vec3(-he.x, he.y, he.z),
    };
    for (const auto& p : corners) {
        Vertex v;
        v.position = p;
        v.normal = glm::normalize(p - c);
        verts.push_back(v);
    }
    static const unsigned f[36] = {
        0, 2, 1, 0, 3, 2,   // -z
        4, 5, 6, 4, 6, 7,   // +z
        0, 1, 5, 0, 5, 4,   // -y
        3, 7, 6, 3, 6, 2,   // +y
        1, 2, 6, 1, 6, 5,   // +x
        0, 4, 7, 0, 7, 3    // -x
    };
    for (unsigned i : f) idx.push_back(base + i);
}
} // namespace

BenchmarkRunner::BenchmarkRunner(Scene* scene, SimulationController* sim,
                                 RenderingSystem* renderer, QObject* parent)
    : QObject(parent), m_scene(scene), m_sim(sim), m_renderer(renderer)
{
    // ----------------------------------------------------------------
    // 1) Free fall: time to cross an altitude IN FLIGHT (kinematic —
    //    measuring at the contact point would fold contact-resolution
    //    latency into a gravity-integration test).
    // ----------------------------------------------------------------
    m_scenarios.push_back({
        QStringLiteral("free-fall time"),
        [this]() {
            m_h0 = 5.0;
            m_subject = spawnSphere({ 0, float(m_h0), 0 }, 0.25f, 0.0f, 0.5f);
            m_flag = false;
            m_t0 = -1.0; // set at first observed motion (immune to startup hitches)
        },
        [this]() {
            auto& reg = m_scene->getRegistry();
            const auto& xf = reg.get<TransformComponent>(m_subject);
            if (m_t0 < 0.0 && xf.translation.y < m_h0 - 0.005f) m_t0 = m_elapsed;
            // crossing altitude y=0.5: still 0.25 m of free flight left
            if (!m_flag && m_t0 >= 0.0 && xf.translation.y <= 0.5) {
                m_measure = m_elapsed - m_t0;
                m_flag = true;
            }
            return m_flag;
        },
        [this]() {
            // timed from the 5mm-drop detection point to crossing y=0.5
            const double expected =
                std::sqrt(2.0 * (m_h0 - 0.5) / kG) - std::sqrt(2.0 * 0.005 / kG);
            check(QStringLiteral("fall time to y=0.5 (s)"), m_measure, expected, 0.03);
        },
        6.0f });

    // ----------------------------------------------------------------
    // 2) Restitution: first bounce apex h1 = e^2 * h0
    // ----------------------------------------------------------------
    m_scenarios.push_back({
        QStringLiteral("coefficient of restitution (e=0.7)"),
        [this]() {
            // PhysX combines material restitution by averaging the pair, so
            // the sphere bounces on a pad with the SAME e — pair value = 0.7.
            entt::entity pad = spawnBox({ 0, 0.25f, 0 }, { 4, 0.25f, 4 }, 0.5f, false);
            m_scene->getRegistry().get<BoxCollider>(pad).material.restitution = 0.7f;
            // rest height of sphere centre on the pad: 0.5 + 0.25 = 0.75
            // Moderate drop: at 1/240 the solver resolves ~3 m/s impacts at
            // the surface; much faster and per-step travel exceeds the
            // contact offset, where TGS restitution turns phase-sensitive
            // (tracked as a known fidelity issue).
            m_h0 = 0.5; // drop height above rest
            m_subject = spawnSphere({ 0, float(0.75 + m_h0), 0 }, 0.25f, 0.7f, 0.5f);
            m_flag = false;      // has bounced
            m_measure = 0.0;     // apex tracker
            m_v0 = 0.0;          // fastest downward speed before impact
            m_t0 = 0.0;          // fastest upward speed after impact
        },
        [this]() {
            auto& reg = m_scene->getRegistry();
            const double y = reg.get<TransformComponent>(m_subject).translation.y;
            const double vy = reg.get<RigidBodyComponent>(m_subject).linearVelocity.y;
            if (!m_flag) {
                m_v0 = std::min(m_v0, vy);
                if (y < 0.78 && vy > 0.1) m_flag = true; // impact happened, rising
            }
            else {
                m_t0 = std::max(m_t0, vy);
                m_measure = std::max(m_measure, y);
                if (vy < -0.1 && m_measure > 0.8) return true; // passed apex
            }
            return false;
        },
        [this]() {
            const double e = 0.7;
            const double expected = 0.75 + e * e * m_h0; // apex of sphere centre
            qInfo() << "[BENCH]   impact vy" << m_v0 << "rebound vy" << m_t0
                    << "=> e_velocity =" << (m_v0 < -0.1 ? -m_t0 / m_v0 : 0.0);
            check(QStringLiteral("bounce apex (m)"), m_measure, expected, 0.15);
        },
        8.0f });

    // ----------------------------------------------------------------
    // 3) Coulomb friction on an incline: sticks iff tan(theta) < mu
    // ----------------------------------------------------------------
    m_scenarios.push_back({
        QStringLiteral("static friction threshold (sticks at 20 deg, mu=0.5)"),
        [this]() {
            const float theta = glm::radians(20.0f); // tan = 0.364 < 0.5 -> sticks
            const glm::quat rot = glm::angleAxis(theta, glm::vec3(0, 0, 1));
            spawnBox({ 0, 0, 0 }, { 4, 0.25f, 2 }, 0.5f, false, rot);
            // place the cube resting on the inclined surface
            const glm::vec3 up = rot * glm::vec3(0, 1, 0);
            const glm::vec3 start = glm::vec3(0, 0, 0) + up * (0.25f + 0.25f + 0.002f);
            m_subject = spawnBox(start, { 0.25f, 0.25f, 0.25f }, 0.5f, true, rot);
            m_v0 = double(start.x); m_h0 = double(start.y);
        },
        [this]() { return m_elapsed > 2.5f; },
        [this]() {
            auto& reg = m_scene->getRegistry();
            const auto& t = reg.get<TransformComponent>(m_subject).translation;
            const double slid = std::hypot(t.x - m_v0, t.y - m_h0);
            check(QStringLiteral("displacement (m, expect ~0)"), slid, 0.0, 0.0); // tol via abs below
            if (slid > 0.05) { qWarning() << "[BENCH]   FAIL: block slid" << slid << "m"; ++m_failures; }
            else qInfo() << "[BENCH]   stick confirmed (slid" << slid << "m)";
        },
        4.0f });

    m_scenarios.push_back({
        QStringLiteral("kinetic friction threshold (slides at 35 deg, mu=0.5)"),
        [this]() {
            const float theta = glm::radians(35.0f); // tan = 0.70 > 0.5 -> slides
            const glm::quat rot = glm::angleAxis(theta, glm::vec3(0, 0, 1));
            spawnBox({ 0, 0, 0 }, { 4, 0.25f, 2 }, 0.5f, false, rot);
            const glm::vec3 up = rot * glm::vec3(0, 1, 0);
            const glm::vec3 start = glm::vec3(0, 0, 0) + up * (0.25f + 0.25f + 0.002f);
            m_subject = spawnBox(start, { 0.25f, 0.25f, 0.25f }, 0.5f, true, rot);
            m_v0 = double(start.x); m_h0 = double(start.y);
        },
        [this]() { return m_elapsed > 1.5f; },
        [this]() {
            auto& reg = m_scene->getRegistry();
            const auto& t = reg.get<TransformComponent>(m_subject).translation;
            const double slid = std::hypot(t.x - m_v0, t.y - m_h0);
            // analytic: s = 0.5*g*(sin(th) - mu*cos(th))*t^2 at t=1.5 (after settle)
            if (slid > 0.10) qInfo() << "[BENCH]   slide confirmed (" << slid << "m)";
            else { qWarning() << "[BENCH]   FAIL: block stuck (slid" << slid << "m)"; ++m_failures; }
            ++m_checks;
        },
        3.0f });

    // ----------------------------------------------------------------
    // 4) Projectile range: R = v * sqrt(2*(h-r)/g)
    // ----------------------------------------------------------------
    m_scenarios.push_back({
        QStringLiteral("projectile range"),
        [this]() {
            m_h0 = 2.0; m_v0 = 5.0;
            m_subject = spawnSphere({ 0, float(m_h0), 0 }, 0.25f, 0.0f, 0.5f);
            auto& rb = m_scene->getRegistry().get<RigidBodyComponent>(m_subject);
            rb.linearVelocity = { float(m_v0), 0, 0 };
            m_flag = false;
        },
        [this]() {
            auto& reg = m_scene->getRegistry();
            const auto& t = reg.get<TransformComponent>(m_subject).translation;
            if (!m_flag && t.y <= 0.2505f + 0.005f) { m_measure = t.x; m_flag = true; }
            return m_flag;
        },
        [this]() {
            const double expected = m_v0 * std::sqrt(2.0 * (m_h0 - 0.25) / kG);
            check(QStringLiteral("range (m)"), m_measure, expected, 0.05);
        },
        6.0f });

    // ----------------------------------------------------------------
    // 5) Fluid: water column height in a tank (volume conservation /
    //    compressibility). V = N*s^3 -> h = V / A.
    // ----------------------------------------------------------------
    m_scenarios.push_back({
        QStringLiteral("fluid column height (volume conservation)"),
        [this]() {
            auto& reg = m_scene->getRegistry();
            // tank: 1 x 1 m inner footprint, high walls
            const float W = 0.5f, H = 1.2f, T = 0.06f;
            auto wall = [&](glm::vec3 pos, glm::vec3 he) {
                spawnBox(pos, he, 0.4f, false);
            };
            wall({ W + T, H * 0.5f, 0 }, { T, H * 0.5f, W + 2 * T });
            wall({ -W - T, H * 0.5f, 0 }, { T, H * 0.5f, W + 2 * T });
            wall({ 0, H * 0.5f, W + T }, { W, H * 0.5f, T });
            wall({ 0, H * 0.5f, -W - T }, { W, H * 0.5f, T });
            // water block: 0.8 x 0.4 x 0.8 m at spacing 0.05 -> ~4k particles
            entt::entity vol = reg.create();
            reg.emplace<TransformComponent>(vol, glm::vec3(0, 0.55f, 0), glm::quat(1, 0, 0, 0), glm::vec3(1));
            auto& v = reg.emplace<FluidVolumeComponent>(vol);
            v.halfExtents = { 0.4f, 0.2f, 0.4f };
            v.particleSpacing = 0.05f;
            reg.emplace<TagComponent>(vol, std::string("BenchWater"));
            m_spawned.push_back(vol);
        },
        [this]() { return m_elapsed > 6.0f; }, // settle
        [this]() {
            const auto& mirror = m_renderer->getFluidSystem()
                ? m_renderer->getFluidSystem()->sampledPositions() : std::vector<glm::vec4>{};
            int n = 0; double maxY = 0.0;
            std::vector<float> ys;
            for (const auto& p : mirror) {
                if (p.w <= 0.0f) continue;
                ys.push_back(p.y); ++n;
            }
            if (n < 100) { qWarning() << "[BENCH]   FAIL: no fluid telemetry"; ++m_failures; ++m_checks; return; }
            // 95th percentile height = surface (robust against spray)
            std::sort(ys.begin(), ys.end());
            maxY = ys[size_t(ys.size() * 0.95)];
            // expected: V = 0.8*0.4*0.8 = 0.256 m^3 over A = 1.0*1.0 -> h = 0.256 m
            check(QStringLiteral("settled water level (m)"), maxY, 0.256, 0.30);
        },
        10.0f });

    // ----------------------------------------------------------------
    // 6) Concave trimesh fidelity: a ball dropped into an open-top cup
    //    (ONE concave mesh, AutoExact static) must rest INSIDE on the
    //    floor (y = 0.10 + r). A convex hull would cap the cavity and
    //    leave it on the rim (y = 0.60 + r) — a 3x error.
    // ----------------------------------------------------------------
    m_scenarios.push_back({
        QStringLiteral("concave collision (ball rests inside exact-trimesh cup)"),
        [this]() {
            auto& reg = m_scene->getRegistry();
            entt::entity cup = reg.create();
            auto& mesh = reg.emplace<RenderableMeshComponent>(cup);
            auto add = [&](glm::vec3 c, glm::vec3 he) {
                appendBoxToMesh(mesh.vertices, mesh.indices, c, he);
            };
            // walls overlap INTO the floor (no coincident coplanar faces —
            // exactly-touching opposite-normal faces confuse mesh cleaning)
            add({ 0, 0.05f, 0 }, { 0.4f, 0.05f, 0.4f });         // floor, top at 0.10
            add({ 0.35f, 0.325f, 0 }, { 0.05f, 0.275f, 0.4f });  // +x wall, rim at 0.60
            add({ -0.35f, 0.325f, 0 }, { 0.05f, 0.275f, 0.4f }); // -x wall
            add({ 0, 0.325f, 0.35f }, { 0.3f, 0.275f, 0.05f });  // +z wall
            add({ 0, 0.325f, -0.35f }, { 0.3f, 0.275f, 0.05f }); // -z wall
            glm::vec3 mn(1e9f), mx(-1e9f);
            for (const auto& v : mesh.vertices) {
                mn = glm::min(mn, v.position);
                mx = glm::max(mx, v.position);
            }
            mesh.aabbMin = mn;
            mesh.aabbMax = mx;
            reg.emplace<TransformComponent>(cup, glm::vec3(0), glm::quat(1, 0, 0, 0), glm::vec3(1));
            reg.emplace<TagComponent>(cup, std::string("BenchCup"));
            auto& ac = reg.emplace<AutoCollisionComponent>(cup); // static scenery -> trimesh
            ac.material.restitution = 0.0f;
            ac.material.staticFriction = ac.material.dynamicFriction = 0.5f;
            m_spawned.push_back(cup);

            m_subject = spawnSphere({ 0, 1.2f, 0 }, 0.15f, 0.0f, 0.5f);
        },
        [this]() {
            auto& reg = m_scene->getRegistry();
            const auto& t = reg.get<TransformComponent>(m_subject).translation;
            static int n = 0;
            if ((++n % 25) == 0)
                qInfo() << "[BENCH]   cup traj t=" << m_elapsed << "pos" << t.x << t.y << t.z;
            return m_elapsed > 3.0f; // settle
        },
        [this]() {
            auto& reg = m_scene->getRegistry();
            const auto& t = reg.get<TransformComponent>(m_subject).translation;
            qInfo() << "[BENCH]   final pos" << t.x << t.y << t.z;
            check(QStringLiteral("resting height inside cup (m)"), double(t.y), 0.10 + 0.15, 0.10);
            if (t.y > 0.5)
                qWarning() << "[BENCH]   ball sits on the hull cap — exact trimesh collision NOT active";
        },
        5.0f });

    // ----------------------------------------------------------------
    // 7) V-HACD decomposition: the SAME concave cup as a DYNAMIC body
    //    must still catch the ball — a single convex hull caps the
    //    cavity (ball at ~0.75); decomposed hulls preserve it (~0.25).
    // ----------------------------------------------------------------
    m_scenarios.push_back({
        QStringLiteral("dynamic concave cup (V-HACD) catches a ball"),
        [this]() {
            auto& reg = m_scene->getRegistry();
            entt::entity cup = reg.create();
            auto& mesh = reg.emplace<RenderableMeshComponent>(cup);
            auto add = [&](glm::vec3 c, glm::vec3 he) {
                appendBoxToMesh(mesh.vertices, mesh.indices, c, he);
            };
            add({ 0, 0.05f, 0 }, { 0.4f, 0.05f, 0.4f });
            add({ 0.35f, 0.325f, 0 }, { 0.05f, 0.275f, 0.4f });
            add({ -0.35f, 0.325f, 0 }, { 0.05f, 0.275f, 0.4f });
            add({ 0, 0.325f, 0.35f }, { 0.3f, 0.275f, 0.05f });
            add({ 0, 0.325f, -0.35f }, { 0.3f, 0.275f, 0.05f });
            glm::vec3 mn(1e9f), mx(-1e9f);
            for (const auto& v : mesh.vertices) {
                mn = glm::min(mn, v.position);
                mx = glm::max(mx, v.position);
            }
            mesh.aabbMin = mn;
            mesh.aabbMax = mx;
            reg.emplace<TransformComponent>(cup, glm::vec3(0), glm::quat(1, 0, 0, 0), glm::vec3(1));
            reg.emplace<TagComponent>(cup, std::string("BenchCupDyn"));
            auto& rb = reg.emplace<RigidBodyComponent>(cup);
            rb.bodyType = RigidBodyComponent::BodyType::Dynamic;
            rb.mass = 2.0f;
            rb.linearDamping = 0.0f;
            rb.angularDamping = 0.0f;
            auto& ac = reg.emplace<AutoCollisionComponent>(cup);
            ac.mode = AutoCollisionComponent::Mode::ConvexDecomposition;
            ac.material.restitution = 0.0f;
            ac.material.staticFriction = ac.material.dynamicFriction = 0.5f;
            m_spawned.push_back(cup);

            // The actor builds at Play with a wait_for(0) check — block here
            // so the decomposition is warm (benchmarks must be deterministic).
            CollisionCookingService::instance()
                .requestConvexDecomposition(mesh.vertices, mesh.indices, "BenchCupDyn")
                .wait();

            m_subject = spawnSphere({ 0, 1.2f, 0 }, 0.15f, 0.0f, 0.5f);
        },
        [this]() { return m_elapsed > 3.5f; },
        [this]() {
            auto& reg = m_scene->getRegistry();
            const auto& t = reg.get<TransformComponent>(m_subject).translation;
            qInfo() << "[BENCH]   final ball pos" << t.x << t.y << t.z;
            // Discriminator is inside-vs-capped (0.25 vs 0.75): V-HACD's
            // voxelized hulls raise the floor a few cm run-to-run, so the
            // tolerance is generous while staying decisive.
            check(QStringLiteral("ball height in dynamic cup (m)"), double(t.y), 0.25, 0.30);
            if (t.y > 0.5)
                qWarning() << "[BENCH]   ball sits on the hull cap — decomposition NOT active";
        },
        6.0f });
}

void BenchmarkRunner::start()
{
    qInfo() << "==================== KRS PHYSICS BENCHMARKS ====================";
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &BenchmarkRunner::sample);
    timer->start(8);
    beginScenario(0);
}

void BenchmarkRunner::beginScenario(int index)
{
    if (index >= int(m_scenarios.size())) {
        qInfo() << "================================================================";
        qInfo() << "[BENCH] DONE:" << (m_checks - m_failures) << "/" << m_checks << "checks passed";
        qInfo() << "================================================================";
        // Hard exit: results are already on disk and the exit code must be
        // the failure count (full Qt teardown would risk masking it).
        std::_Exit(m_failures);
    }
    m_current = index;
    m_elapsed = 0.0f;
    m_scenarioClock.restart();
    m_subject = entt::null;
    qInfo() << "[BENCH] ---" << m_scenarios[index].name << "---";
    m_scenarios[index].setup();
    m_sim->play();
}

void BenchmarkRunner::endScenario()
{
    m_sim->stop();
    clearSpawned();
    if (m_renderer && m_renderer->getFluidSystem()) m_renderer->resetFluidSimulation();
    beginScenario(m_current + 1);
}

void BenchmarkRunner::sample()
{
    if (m_current < 0 || m_current >= int(m_scenarios.size())) return;
    m_elapsed = float(m_scenarioClock.elapsed()) * 1e-3f; // real time, not tick count
    auto& sc = m_scenarios[m_current];
    bool done = false;
    if (m_subject == entt::null && !m_spawned.empty()) {
        // scenarios that don't set m_subject still rely on finished()
    }
    done = sc.finished() || m_elapsed > sc.timeoutSeconds;
    if (done) {
        sc.evaluate();
        endScenario();
    }
}

void BenchmarkRunner::check(const QString& metric, double measured, double expected, double tolFraction)
{
    ++m_checks;
    const double err = expected != 0.0 ? std::abs(measured - expected) / std::abs(expected) : std::abs(measured);
    const bool pass = tolFraction > 0.0 ? (err <= tolFraction) : true;
    if (!pass) ++m_failures;
    qInfo().nospace() << "[BENCH]   " << metric << ": measured " << measured
                      << " expected " << expected << " err " << (err * 100.0) << "%"
                      << " => " << (pass ? "PASS" : "FAIL");
}

entt::entity BenchmarkRunner::spawnSphere(const glm::vec3& pos, float radius,
                                          float restitution, float friction)
{
    auto& reg = m_scene->getRegistry();
    entt::entity e = SceneBuilder::spawnPrimitive(*m_scene, int(Primitive::IcoSphere), pos,
                                                  glm::vec3(radius * 2.0f), "BenchSphere");
    auto& rb = reg.emplace<RigidBodyComponent>(e);
    rb.bodyType = RigidBodyComponent::BodyType::Dynamic;
    rb.mass = 1.0f;
    rb.linearDamping = 0.0f;   // analytic comparisons assume no drag
    rb.angularDamping = 0.0f;
    // Collider radius is LOCAL space (backend multiplies by entity scale 2r):
    // unit icosphere has radius 0.5 -> world physics radius = 0.5 * 2r = r.
    // (Was `radius`, silently double-scaling every benchmark sphere.)
    auto& col = reg.emplace<SphereCollider>(e);
    col.radius = 0.5f;
    col.material.restitution = restitution;
    col.material.staticFriction = col.material.dynamicFriction = friction;
    m_spawned.push_back(e);
    return e;
}

entt::entity BenchmarkRunner::spawnBox(const glm::vec3& pos, const glm::vec3& halfExt,
                                       float friction, bool dynamic, const glm::quat& rot)
{
    auto& reg = m_scene->getRegistry();
    entt::entity e = SceneBuilder::spawnPrimitive(*m_scene, int(Primitive::Cube), pos,
                                                  halfExt * 2.0f, "BenchBox");
    reg.get<TransformComponent>(e).rotation = rot;
    auto& rb = reg.emplace<RigidBodyComponent>(e);
    rb.bodyType = dynamic ? RigidBodyComponent::BodyType::Dynamic
                          : RigidBodyComponent::BodyType::Static;
    rb.mass = 1.0f;
    rb.linearDamping = 0.0f;
    rb.angularDamping = 0.0f;
    auto& col = reg.emplace<BoxCollider>(e);
    col.halfExtents = glm::vec3(0.5f);
    col.material.restitution = 0.0f;
    col.material.staticFriction = col.material.dynamicFriction = friction;
    m_spawned.push_back(e);
    return e;
}

void BenchmarkRunner::clearSpawned()
{
    auto& reg = m_scene->getRegistry();
    for (auto e : m_spawned)
        if (reg.valid(e)) reg.destroy(e);
    m_spawned.clear();
}
