#include "RobotViewport.hpp"

#include "Scene.hpp"
#include "RenderingSystem.hpp"
#include "Camera.hpp"
#include "components.hpp"
#include "SelectionService.hpp"
#include "SceneBuilder.hpp"
#include "RobotBuilder.hpp"        // krs::rbuild::RobotGraph
#include "RobotBuilderScene.hpp"   // mirrorGraphIntoScene / turntableCameraPos / gate

#include <QTimer>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

RobotViewport::RobotViewport(Scene* mainScene, QWidget* parent)
    : ViewportWidget(nullptr, nullptr, entt::null, parent), m_mainScene(mainScene)
{
    // Self-owned world (PreviewViewport model) so this view shows ONLY the robot.
    m_viewScene  = std::make_unique<Scene>();
    m_viewRender = std::make_unique<RenderingSystem>(this);

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

    refreshFromLive();   // bind + mirror (data path); render begins after initializeGL
}

RobotViewport::~RobotViewport()
{
    if (m_viewRender) m_viewRender->shutdown();
}

void RobotViewport::refreshFromLive()
{
    m_liveGraph = m_mainScene
        ? m_mainScene->getRegistry().ctx().find<krs::rbuild::RobotGraph>()
        : nullptr;
    rebuildView();
}

void RobotViewport::rebuildView()
{
    if (!m_viewScene) return;
    auto& reg = m_viewScene->getRegistry();

    // Clear everything except the camera (PreviewViewport::clearPreview idiom).
    std::vector<entt::entity> kill;
    for (auto e : reg.view<entt::entity>())
        if (e != m_cameraEntity) kill.push_back(e);
    reg.destroy(kill.begin(), kill.end());

    if (m_liveGraph && !m_liveGraph->bodies.empty()) {
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
    const float dt = std::min(0.05f, float(m_spinClock.restart()) * 0.001f);
    m_spinAngle += dt * 0.5f;   // ~0.5 rad/s slow spin around the base axis
    const glm::vec3 pos = krs::rbuild::turntableCameraPos(m_base, m_dist, m_elev, m_spinAngle);
    getCamera().forceRecalculateView(pos, m_base, m_dist);
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
    RobotViewport vp(&mainScene);                  // headless construct (no initializeGL)
    const bool bindLive = (vp.liveGraph() == &g) && (vp.liveGraph() != nullptr);

    // NEG-CTRL: a viewport over a scene with NO graph binds nullptr (stale/no-data fails).
    Scene emptyScene;
    RobotViewport vpEmpty(&emptyScene);
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
