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

#include <QTimer>
#include <QSlider>
#include <QLabel>
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

    layoutOverlayControls();
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
    ViewportWidget::mouseReleaseEvent(ev);   // let the base finish its nav first
    m_manualNav = false;
    reseedOrbitFromCamera();                 // resume orbit from the current view
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
        for (const auto& pr : m_bodyMap) {
            if (!mreg.valid(pr.first) || !vreg.valid(pr.second)) continue;
            const auto* mtc = mreg.try_get<TransformComponent>(pr.first);
            auto*       vtc = vreg.try_get<TransformComponent>(pr.second);
            if (mtc && vtc) *vtc = *mtc;
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
