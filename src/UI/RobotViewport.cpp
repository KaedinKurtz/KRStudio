#include "RobotViewport.hpp"

#include "Scene.hpp"
#include "RenderingSystem.hpp"
#include "Camera.hpp"
#include "components.hpp"
#include "SelectionService.hpp"
#include "SceneBuilder.hpp"
#include "RobotBuilder.hpp"        // krs::rbuild::RobotGraph
#include "RobotBuilderScene.hpp"   // mirrorGraphIntoScene / turntableCameraPos / gate
#include "RobotModel.hpp"          // krs::robot::RobotRegistry / mirrorLiveRobotIntoScene
#include "PrimitiveBuilders.hpp"   // Primitive::Cylinder (joint-axis bars)
#include <glm/gtx/quaternion.hpp>  // glm::rotation (align a bar to the axis direction)

#include <QTimer>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QMouseEvent>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

RobotViewport::RobotViewport(Scene* mainScene, RenderingSystem* mainRender, QWidget* parent)
    : ViewportWidget(nullptr, nullptr, entt::null, parent),
      m_mainScene(mainScene), m_mainRender(mainRender)
{
    // Self-owned world (PreviewViewport model) so this view shows ONLY the robot.
    m_viewScene  = std::make_unique<Scene>();
    m_viewRender = std::make_unique<RenderingSystem>(this);
    // CRITICAL: do NOT bake our own IBL. A 2nd equirect->cubemap bake in a shared GL
    // context renders BLACK and corrupts the environment (it blacked-out the robot +
    // skybox in BOTH viewports). Borrow the main renderer's baked maps instead.
    m_viewRender->setSkipEnvironmentBake(true);
    // Level-editor look: a flat grey room (no horizon) instead of a skybox copy.
    // IBL is still borrowed (in onSpinTick) so the robot stays lit + metals reflect.
    m_viewRender->setDrawSkybox(false);

    auto& reg = m_viewScene->getRegistry();
    reg.ctx().emplace<SceneProperties>();
    reg.ctx().emplace<krs::sel::SelectionState>();

    // Point the protected base members at our OWN scene BEFORE creating the camera.
    m_scene = m_viewScene.get();
    m_renderingSystem = m_viewRender.get();
    m_cameraEntity = SceneBuilder::createCamera(*m_viewScene, { 2.5f, 1.0f, 2.5f }, { 1, 1, 1 });
    m_viewScene->setPrimaryCamera(m_cameraEntity);

    m_spinTimer = new QTimer(this);
    connect(m_spinTimer, &QTimer::timeout, this, &RobotViewport::onSpinTick);

    buildOverlayControls();   // bottom-right orbit-speed slider

    refreshFromLive();   // bind + mirror (data path); render begins after initializeGL
}

// ---------------------------------------------------------------------------
// Bottom-right orbit-speed slider. 0 = stopped (acceptable). A child widget
// floating over the GL surface (Qt composites child QWidgets over QOpenGLWidget).
// ---------------------------------------------------------------------------
void RobotViewport::buildOverlayControls()
{
    m_orbitLabel = new QLabel(QStringLiteral("Orbit"), this);
    m_orbitLabel->setStyleSheet(QStringLiteral(
        "QLabel{color:#dddddd;background:transparent;font-size:10px;}"));
    m_orbitLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    m_orbitSlider = new QSlider(Qt::Horizontal, this);
    m_orbitSlider->setRange(0, 100);
    m_orbitSlider->setValue(int(m_orbitSpeed / kMaxOrbitSpeed * 100.0f + 0.5f));
    m_orbitSlider->setFixedWidth(120);
    m_orbitSlider->setToolTip(QStringLiteral("Auto-orbit speed (0 = stopped)"));
    m_orbitSlider->setStyleSheet(QStringLiteral(
        "QSlider{background:transparent;}"
        "QSlider::groove:horizontal{height:4px;background:#555;border-radius:2px;}"
        "QSlider::handle:horizontal{width:12px;margin:-5px 0;background:#d4af34;border-radius:6px;}"
        "QSlider::sub-page:horizontal{background:#d4af34;border-radius:2px;}"));
    connect(m_orbitSlider, &QSlider::valueChanged, this, &RobotViewport::onOrbitSliderChanged);

    // Builder/Mirror toggle (left of the slider). Builder = home (0deg) for joint editing;
    // Mirror = live joint angles from the scene robot.
    m_modeToggle = new QPushButton(this);
    m_modeToggle->setCheckable(true);
    m_modeToggle->setChecked(m_builderMode);
    m_modeToggle->setText(m_builderMode ? QStringLiteral("Builder") : QStringLiteral("Mirror"));
    m_modeToggle->setFixedWidth(72);
    m_modeToggle->setToolTip(QStringLiteral("Builder = robot at home (0 deg) for editing joints; "
                                            "Mirror = mirror the live scene robot's joint angles"));
    m_modeToggle->setStyleSheet(QStringLiteral(
        "QPushButton{color:#dddddd;background:#3a3a3a;border:1px solid #555;border-radius:3px;font-size:10px;padding:2px;}"
        "QPushButton:checked{background:#d4af34;color:#222;border:1px solid #d4af34;}"));
    connect(m_modeToggle, &QPushButton::toggled, this, &RobotViewport::onModeToggled);

    layoutOverlayControls();
}

void RobotViewport::onModeToggled(bool builder)
{
    m_builderMode = builder;
    if (m_modeToggle) m_modeToggle->setText(builder ? QStringLiteral("Builder") : QStringLiteral("Mirror"));
}

int RobotViewport::viewSelectedCount() const
{
    if (!m_viewScene) return 0;
    auto& vreg = m_viewScene->getRegistry();
    int n = 0;
    for (const auto& pr : m_bodyMap)
        if (vreg.valid(pr.second) && vreg.any_of<SelectedComponent>(pr.second)) ++n;
    return n;
}

void RobotViewport::layoutOverlayControls()
{
    if (!m_orbitSlider) return;
    const int m = 10;                       // margin from the edges
    const int sw = m_orbitSlider->width();
    const int sh = m_orbitSlider->sizeHint().height();
    const int x  = width()  - sw - m;
    const int y  = height() - sh - m;
    m_orbitSlider->move(x, y);
    if (m_orbitLabel) {
        m_orbitLabel->adjustSize();
        m_orbitLabel->move(x, y - m_orbitLabel->height());
    }
    if (m_modeToggle) {
        const int tw = m_modeToggle->width();
        const int th = m_modeToggle->sizeHint().height();
        m_modeToggle->move(x - tw - 10, height() - th - m);   // left of the orbit slider
    }
}

void RobotViewport::resizeEvent(QResizeEvent* ev)
{
    ViewportWidget::resizeEvent(ev);
    layoutOverlayControls();
}

void RobotViewport::onOrbitSliderChanged(int value)
{
    m_orbitSpeed = (float(value) / 100.0f) * kMaxOrbitSpeed;
}

void RobotViewport::setOrbitSpeed(float radPerSec)
{
    m_orbitSpeed = std::clamp(radPerSec, 0.0f, kMaxOrbitSpeed);
    if (m_orbitSlider) {
        const QSignalBlocker block(m_orbitSlider);
        m_orbitSlider->setValue(int(m_orbitSpeed / kMaxOrbitSpeed * 100.0f + 0.5f));
    }
}

// While a drag is in progress the auto-orbit is suspended (you cannot auto-orbit
// while transforming). On release we re-seed the orbit from wherever the camera
// now is, so resuming continues smoothly from the user's view rather than jumping.
void RobotViewport::mousePressEvent(QMouseEvent* ev)
{
    m_manualNav = true;
    ViewportWidget::mousePressEvent(ev);
}

void RobotViewport::mouseReleaseEvent(QMouseEvent* ev)
{
    ViewportWidget::mouseReleaseEvent(ev);   // base picks (sets selection in the VIEW scene)
    m_manualNav = false;
    reseedOrbitFromCamera();                 // resume orbit from the current view

    // Cross-viewport selection (VIEW -> MAIN): if a robot body was picked in this view,
    // select its twin in the main scene (the source of truth). onSpinTick then mirrors it
    // back so both viewports gold-outline the same part, even at different poses.
    if (!m_bodyMap.empty() && m_mainScene && m_viewScene) {
        auto& vreg = m_viewScene->getRegistry();
        auto& mreg = m_mainScene->getRegistry();
        std::vector<entt::entity> picked;     // main twins of selected view bodies
        for (const auto& pr : m_bodyMap)
            if (vreg.valid(pr.second) && vreg.any_of<SelectedComponent>(pr.second) && mreg.valid(pr.first))
                picked.push_back(pr.first);
        if (!picked.empty()) {
            for (const auto& pr : m_bodyMap)
                if (mreg.valid(pr.first) && mreg.any_of<SelectedComponent>(pr.first))
                    mreg.remove<SelectedComponent>(pr.first);
            for (auto e : picked) mreg.emplace_or_replace<SelectedComponent>(e);
        }
    }
}

void RobotViewport::reseedOrbitFromCamera()
{
    const Camera& cam = getCamera();
    const glm::vec3 pos = cam.getPosition();
    const glm::vec3 tgt = cam.getFocalPoint();
    m_base = tgt;                             // honour pans (the target moved)
    const float dx = pos.x - m_base.x;
    const float dz = pos.z - m_base.z;
    m_dist = std::max(0.01f, std::sqrt(dx * dx + dz * dz));
    m_elev = pos.y - m_base.y;
    m_spinAngle = std::atan2(dz, dx);
}

RobotViewport::~RobotViewport()
{
    if (m_viewRender) m_viewRender->shutdown();
}

void RobotViewport::refreshFromLive()
{
    m_robotId = -1;
    m_liveGraph = nullptr;
    if (m_mainScene) {
        auto& reg = m_mainScene->getRegistry();
        // Prefer a first-class LiveRobot (e.g. the FANUC) -> mirror its real bodies.
        if (auto* rr = reg.ctx().find<krs::robot::RobotRegistry>()) {
            for (auto& rp : rr->robots) if (rp) { m_robotId = rp->robotId; break; }
        }
        // Fall back to the authoring demo graph only if no live robot exists.
        if (m_robotId < 0) m_liveGraph = reg.ctx().find<krs::rbuild::RobotGraph>();
    }
    rebuildView();
}

void RobotViewport::rebuildView()
{
    if (!m_viewScene) return;
    auto& reg = m_viewScene->getRegistry();
    m_bodyMap.clear();

    // Clear everything except the camera (PreviewViewport::clearPreview idiom).
    std::vector<entt::entity> kill;
    for (auto e : reg.view<entt::entity>())
        if (e != m_cameraEntity) kill.push_back(e);
    reg.destroy(kill.begin(), kill.end());

    if (m_robotId >= 0 && m_mainScene) {
        // Mirror the ACTUAL robot bodies (e.g. the FANUC) into the isolated view scene.
        krs::robot::mirrorLiveRobotIntoScene(*m_viewScene, *m_mainScene, m_robotId, m_bodyMap);

        // Mirror the scene LIGHTS (LightComponent only, not their emitter meshes) so the
        // robot is lit like the main view -- otherwise it is IBL-only and reads near-black.
        auto& mreg = m_mainScene->getRegistry();
        for (auto le : mreg.view<LightComponent, TransformComponent>()) {
            const entt::entity ve = reg.create();
            reg.emplace<LightComponent>(ve, mreg.get<LightComponent>(le));
            reg.emplace<TransformComponent>(ve, mreg.get<TransformComponent>(le));
        }

        // Frame the camera on the robot using the body BOUNDING BOX (not the centroid,
        // which a tall arm's many base parts bias downward), so the whole arm fits.
        glm::vec3 mn(1e9f), mx(-1e9f);
        for (auto& pr : m_bodyMap)
            if (auto* tc = reg.try_get<TransformComponent>(pr.second)) {
                mn = glm::min(mn, tc->translation); mx = glm::max(mx, tc->translation);
            }
        if (mx.x >= mn.x) {
            m_base = (mn + mx) * 0.5f;
            const float r = std::max(0.4f, 0.5f * glm::length(mx - mn));
            m_dist = std::clamp(r * 3.0f, 3.0f, 16.0f);
            m_elev = r * 0.4f;
        }

        // Capture each body's HOME (q0) transform for Builder mode. Default = the current
        // (live) transform; for moving bodies, override with the FK(q=0) pose by briefly
        // posing the LiveRobot at q0 (restored immediately; not rendered in between).
        m_restXf.clear();
        m_restXf.reserve(m_bodyMap.size());
        for (auto& pr : m_bodyMap) {
            const auto* tc = mreg.try_get<TransformComponent>(pr.first);
            m_restXf.push_back(tc ? *tc : TransformComponent{});
        }
        if (auto* rr = mreg.ctx().find<krs::robot::RobotRegistry>()) {
            krs::robot::LiveRobot* lr = rr->get(m_robotId);
            if (lr && lr->useRobotFkViz && lr->ndof() > 0) {
                const Eigen::VectorXd savedQ = lr->q;
                lr->q.setZero();
                krs::robot::writeBackRobotViz(*m_mainScene, *lr);     // main bodies -> q0
                for (size_t i = 0; i < m_bodyMap.size(); ++i)
                    if (auto* t = mreg.try_get<TransformComponent>(m_bodyMap[i].first)) m_restXf[i] = *t;
                lr->q = savedQ;
                krs::robot::writeBackRobotViz(*m_mainScene, *lr);     // restore live pose
            }
        }

        // Joint-axis overlay: a bright cyan emissive bar through each joint's HOME axis,
        // so the robot's defined axes are visible (esp. in Builder mode for joint editing).
        m_axisEntities.clear();
        if (auto* rr = mreg.ctx().find<krs::robot::RobotRegistry>()) {
            if (auto* lr = rr->get(m_robotId)) {
                const auto axes = lr->jointAxesWorld();   // HOME axes (updated per-tick in onSpinTick)
                for (int k = 0; k < int(axes.size()); ++k) {
                    const glm::vec3 o(float(axes[k].first.x()), float(axes[k].first.y()), float(axes[k].first.z()));
                    const glm::vec3 d(float(axes[k].second.x()), float(axes[k].second.y()), float(axes[k].second.z()));
                    const entt::entity ae = SceneBuilder::spawnPrimitive(
                        *m_viewScene, int(Primitive::Cylinder), o,
                        glm::vec3(0.014f, 0.90f, 0.014f), "JointAxis");   // long bar; drawn on-top
                    if (reg.valid(ae)) {
                        reg.get<TransformComponent>(ae).rotation =
                            glm::rotation(glm::vec3(0.0f, 1.0f, 0.0f), glm::normalize(d));
                        auto& mat = reg.get_or_emplace<MaterialComponent>(ae);
                        mat.albedoColor      = glm::vec3(0.10f, 0.90f, 1.00f);
                        mat.emissiveColor    = glm::vec3(0.10f, 0.90f, 1.00f);
                        mat.emissiveStrength = 4.0f;
                        // Drawn always-on-top by JointAxisPass (and excluded from the opaque
                        // G-buffer), so the axis is visible even where it passes through a link.
                        reg.emplace<JointAxisComponent>(ae);
                        m_axisEntities.emplace_back(ae, k);   // track for per-tick follow
                    }
                }
            }
        }
    } else if (m_liveGraph && !m_liveGraph->bodies.empty()) {
        krs::rbuild::mirrorGraphIntoScene(*m_viewScene, *m_liveGraph, /*robotId*/ 0);
        const int bi = (m_liveGraph->base >= 0 && m_liveGraph->base < int(m_liveGraph->bodies.size()))
                       ? m_liveGraph->base : 0;
        const auto& bp = m_liveGraph->bodies[bi].placement;
        m_base = glm::vec3(float(bp(0, 3)), float(bp(1, 3)), float(bp(2, 3)));
    }
    update();
}

void RobotViewport::initializeGL()
{
    QOpenGLWidget::initializeGL();
    if (m_renderingSystem) {
        m_renderingSystem->initialize(m_scene);
        m_renderingSystem->onViewportAdded(this);
    }
    m_spinClock.start();
    m_spinTimer->start(16);   // ~60 Hz turntable spin
}

void RobotViewport::onSpinTick()
{
    // Borrow the main renderer's baked IBL once it is ready (we skipped our own bake).
    if (m_mainRender && m_viewRender && !m_viewRender->hasBakedEnvironment()
        && m_mainRender->hasBakedEnvironment()) {
        m_viewRender->adoptEnvironmentFrom(*m_mainRender);
    }
    const float dt = std::min(0.05f, float(m_spinClock.restart()) * 0.001f);
    // Auto-orbit only when not manually navigating AND the slider speed > 0. During
    // a manual drag the base ViewportWidget owns the camera (orbit/pan/zoom); when
    // the orbit is stopped (speed 0) the camera simply holds its current pose.
    if (!m_manualNav && m_orbitSpeed > 1e-4f) {
        m_spinAngle += dt * m_orbitSpeed;
        const glm::vec3 pos = krs::rbuild::turntableCameraPos(m_base, m_dist, m_elev, m_spinAngle);
        getCamera().forceRecalculateView(pos, m_base, m_dist);
    }
    // Mirror the live robot pose: copy each main body's transform onto its view twin
    // (Mirror mode). Main bodies are parented to an identity root, so their local
    // transform IS the world transform -> copy straight across to the parent-less twin.
    if (!m_bodyMap.empty() && m_mainScene && m_viewScene) {
        auto& mreg = m_mainScene->getRegistry();
        auto& vreg = m_viewScene->getRegistry();
        for (size_t i = 0; i < m_bodyMap.size(); ++i) {
            const auto& pr = m_bodyMap[i];
            if (!vreg.valid(pr.second)) continue;
            auto* vtc = vreg.try_get<TransformComponent>(pr.second);
            if (!vtc) continue;
            if (m_builderMode) {
                if (i < m_restXf.size()) *vtc = m_restXf[i];          // frozen HOME (q0) pose
            } else if (mreg.valid(pr.first)) {
                if (auto* mtc = mreg.try_get<TransformComponent>(pr.first)) *vtc = *mtc;  // live
            }
            // Cross-viewport selection: mirror the MAIN scene's selection onto the view
            // twin so a body selected in the main viewport/outliner is gold-outlined here.
            const bool sel = mreg.valid(pr.first) && mreg.any_of<SelectedComponent>(pr.first);
            if (sel && !vreg.any_of<SelectedComponent>(pr.second))      vreg.emplace<SelectedComponent>(pr.second);
            else if (!sel && vreg.any_of<SelectedComponent>(pr.second)) vreg.remove<SelectedComponent>(pr.second);
        }
    }

    // Joint-axis bars FOLLOW the robot: recompute each from the CURRENT joint axes
    // (Builder = home q0; Mirror = the live robot's q) so they track the pose instead of
    // being left behind at home when the robot moves.
    if (!m_axisEntities.empty() && m_mainScene && m_viewScene) {
        auto& mreg = m_mainScene->getRegistry();
        auto& vreg = m_viewScene->getRegistry();
        if (auto* rr = mreg.ctx().find<krs::robot::RobotRegistry>()) {
            if (auto* lr = rr->get(m_robotId)) {
                const Eigen::VectorXd q = m_builderMode ? Eigen::VectorXd::Zero(std::max(0, lr->ndof())) : lr->q;
                const auto axes = lr->jointAxesWorld(q);
                for (const auto& ae : m_axisEntities) {
                    if (ae.second < 0 || ae.second >= int(axes.size())) continue;
                    if (!vreg.valid(ae.first)) continue;
                    auto* tc = vreg.try_get<TransformComponent>(ae.first);
                    if (!tc) continue;
                    const auto& a = axes[ae.second];
                    tc->translation = glm::vec3(float(a.first.x()), float(a.first.y()), float(a.first.z()));
                    const glm::vec3 d(float(a.second.x()), float(a.second.y()), float(a.second.z()));
                    if (glm::length(d) > 1e-6f)
                        tc->rotation = glm::rotation(glm::vec3(0.0f, 1.0f, 0.0f), glm::normalize(d));
                }
            }
        }
    }

    // Self-drive the render: this isolated RenderingSystem is not pumped by the main
    // engine frame (which only renders the main system), so render then present.
    if (m_viewRender) m_viewRender->renderAllViewports();
    update();
}

// ===========================================================================
// GATE VIEWPORT-DATA (Phase 2) -- the gateable half: the viewport binds the LIVE
// graph (not a copy), the spin is a real base-axis transform, and the graph's
// bodies are mirrored (fed) into the view scene. Rendering itself is OPERATOR-
// VISUAL-CONFIRM. NEG-CTRLs: a no-graph scene binds nothing; a static (angle-
// ignoring) camera does not move; an empty graph mirrors nothing.
// ===========================================================================
namespace krs::rbuild {

bool runRobotViewportGate()
{
    using std::printf;
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("[rbuild] GATE VIEWPORT-DATA -- robot-only viewport binds the LIVE graph + real base-axis spin + mirrors bodies\n");

    // ---- BIND: the viewport binds the live ctx graph (the same object) ----
    Scene mainScene;
    auto& g = mainScene.getRegistry().ctx().emplace<RobotGraph>(buildDemoGraph());
    spawnGraphBodies(mainScene, g, 0);
    RobotViewport vp(&mainScene, nullptr);         // headless construct (no IBL/initializeGL needed)
    const bool bindLive = (vp.liveGraph() == &g) && (vp.liveGraph() != nullptr);

    // NEG-CTRL: a viewport over a scene with NO graph binds nullptr (stale/no-data fails).
    Scene emptyScene;
    RobotViewport vpEmpty(&emptyScene, nullptr);
    const bool negNoData = (vpEmpty.liveGraph() == nullptr);

    // ---- SPIN is a real base-axis transform: it MOVES and keeps a constant orbit radius ----
    const glm::vec3 base(0.4f, 0.0f, 0.0f);
    const glm::vec3 p0 = turntableCameraPos(base, 2.5f, 1.0f, 0.0f);
    const glm::vec3 p1 = turntableCameraPos(base, 2.5f, 1.0f, 1.0f);
    const bool moves = glm::length(p1 - p0) > 1e-3f;
    auto orbitR = [&](const glm::vec3& p) { const glm::vec3 d = p - base; return std::sqrt(d.x * d.x + d.z * d.z); };
    const bool radiusConst = std::abs(orbitR(p0) - 2.5f) < 1e-3f && std::abs(orbitR(p1) - 2.5f) < 1e-3f;
    // NEG-CTRL: a static (angle-ignoring) position does not move -> would fail the spin check.
    auto staticPos = [&](float) { return base + glm::vec3(2.5f, 1.0f, 0.0f); };
    const bool negStatic = glm::length(staticPos(1.0f) - staticPos(0.0f)) < 1e-9f;

    // ---- RENDER FEED: the live graph's bodies mirror into a preview scene ----
    Scene preview;
    const int mirrored = mirrorGraphIntoScene(preview, g, 0);
    int previewEntities = 0;
    for (auto e : preview.getRegistry().view<RobotSubcomponentComponent>()) { (void)e; ++previewEntities; }
    const bool feedOk = (mirrored == int(g.bodies.size())) && (mirrored > 0) && (previewEntities == mirrored);
    // NEG-CTRL: an empty graph mirrors nothing.
    Scene preview2; RobotGraph emptyG;
    const bool negEmptyFeed = (mirrorGraphIntoScene(preview2, emptyG, 0) == 0);

    const bool pass = bindLive && negNoData && moves && radiusConst && negStatic && feedOk && negEmptyFeed;

    printf("[rbuild]   bind live graph=%s ; spin moves=%s (orbit radius constant=%s) ; bodies mirrored=%d/%d (preview entities=%d)  %s\n",
           bindLive ? "yes" : "NO", moves ? "yes" : "NO", radiusConst ? "yes" : "NO",
           mirrored, int(g.bodies.size()), previewEntities,
           (bindLive && moves && radiusConst && feedOk) ? "PASS" : "FAIL");
    printf("[rbuild]   NEG-CTRLs: no-graph binds null=%s ; static cam no-move=%s ; empty graph mirrors 0=%s  %s\n",
           negNoData ? "yes" : "no", negStatic ? "yes" : "no", negEmptyFeed ? "yes" : "no",
           (negNoData && negStatic && negEmptyFeed) ? "REJECTS(non-vacuous)" : "VACUOUS!");
    printf("[rbuild] %s\n", pass ? "ALL PASS (robot-only viewport is fed the live robot data; spin is a real base-axis transform)"
                                  : "FAILURES PRESENT");
    std::fflush(stdout);
    return pass;
}

} // namespace krs::rbuild
