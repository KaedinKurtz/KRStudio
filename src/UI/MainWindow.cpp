#include "MainWindow.hpp"
#include "StaticToolbar.hpp"
#include "PropertiesPanel.hpp"
#include "Scene.hpp"
#include "ViewportWidget.hpp"
#include "components.hpp"
#include "SelectionService.hpp"   // krs::sel::SelectionState (ctx singleton) 
#include "Camera.hpp"
#include "SceneBuilder.hpp"
#include "GridLevel.hpp"
#include "KRobotParser.hpp"
#include "KRobotWriter.hpp"
#include "URDFParser.hpp"
#include "SDFParser.hpp"
#include "RobotEnrichmentDialog.hpp"
#include "Mesh.hpp" // Required for the test cube's mesh data.
#include "IntersectionSystem.hpp" 
#include "RenderingSystem.hpp"
#include "MpmSystem.hpp"
#include "MaterialLibrary.hpp"
#include "CadImporter.hpp"
#include "FanucArticulation.hpp"   // Phase V: shared FANUC-setup helper (default boot demo)
#include <filesystem>
#include <QActionGroup>
#include <QToolBar>
#include <QComboBox>
#include <QLabel>
#include <QInputDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QElapsedTimer>   // [PERF] frame-split instrumentation (opt-in via KRS_PERF)
#include <QDebug>
#include "FluidSystem.hpp"
#include "FlowVisualizerMenu.hpp"
#include "Helpers.hpp"   
#include "entt/entt.hpp"
#include "RealSenseConfigMenu.hpp"
#include "SlamManager.hpp"
#include "CustomDataFlowScene.hpp"
#include "ExecutionControlWidget.hpp"
#include "DatabasePanel.hpp"
#include "DatabaseManager.hpp"
#include "DiagnosticsPanel.hpp"
#include "ViewportManagerPopup.hpp"
#include "MaterialLoader.hpp"
#include "MeshUtils.hpp"
#include "ResourceManager.hpp"
#include "GizmoSystem.hpp"
#include "SimulationController.hpp"
#include "PrimitiveBuilders.hpp"
#include "PhysicsPropertiesWidget.hpp"
#include "RobotBuilderPanel.hpp"   // robot-builder editing panel (invokes proven krs::rbuild ops)
#include "RobotViewport.hpp"       // robot-only spinning viewport bound to the live graph
#include "RobotModel.hpp"          // krs::robot::instantiateFanucRobot (first-class Robot)
#include "RobotBuilder.hpp"        // krs::rbuild::RobotGraph (re-apply edits to the live robot)
#include "MaterialApply.hpp"       // krs::material::applyPackTags (texture-apply repro hook)
#include "OutlinerWidget.hpp"
#include "FluidPropertiesWidget.hpp"
#include "SmokePropertiesWidget.hpp"
#include "LightingPropertiesWidget.hpp"
#include "SettingsManager.hpp"
#include "SettingsDialog.hpp"
#include <QPointer>
#include <glm/glm.hpp>
#include "TextureBrowserWidget.hpp"
#include "AssetBrowserWidget.hpp"
#include "FluidMesher.hpp"
#include "BenchmarkRunner.hpp"

#include <QDir>
#include <QDirIterator>
#include <QMenuBar>
#include <QMenu>

#include <QtNodes/NodeDelegateModelRegistry>
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/DataFlowGraphicsScene>
#include <QtNodes/GraphicsView>
#include <QtNodes/NodeData>
#include <QtNodes/ConnectionStyle>
#include <QtNodes/NodeStyle>
#include <QtNodes/GraphicsViewStyle>
#include <QShortcut>
#include <QKeySequence>

#include "NodeDelegate.hpp"          // ADD THIS
#include "NodeCatalogWidget.hpp"   // ADD THIS if you haven't already
#include "DroppableGraphicsView.hpp" // ADD THIS if you haven't already
#include "NodeFactory.hpp" // ADD THIS if you haven't already
#include "OrbProbe.hpp"     // Phase 4: velocity-probe orb<->node binding
#include "RobotGraph.hpp"  // default boot node graph (spawnDefaultRobotGraph / tickRobotGraph)
#include "NodeEditQueue.hpp" // decouple UI edits from the physics thread
#include "EvalEngine.hpp"  // quiet eval + capped UI refresh (decouple eval from UI-update)
#include <QDoubleSpinBox>
#include <QLabel>
#include <algorithm>
#include <cmath>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QWidget>
#include <QMenuBar>
#include <QStatusBar>
#include <QTimer>
#include <QDebug>
#include <DockManager.h>
#include <DockWidget.h>
#include <DockAreaWidget.h>
#include <QApplication>
#include <QButtonGroup>
#include <QSplitter>
#include "DockSplitter.h" 
#include <QOpenGLVersionFunctionsFactory>
#include <QPushButton>
#include <QListWidget>
#include <QSpinBox>
#include <DockAreaTitleBar.h> 
#include <QRandomGenerator>
#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QSlider>
#include <QApplication>
#include <QStyle>
#include <QAbstractButton>
#include <QFrame>
#include <QMenu>
#include <QWidgetAction>
#include <memory>

const QString sidePanelStyle = R"(
    /* General Window and Text Styling */
    QWidget {
        background-color: #2c313a; /* Dark gray background */
        color: #d5d5d5;            /* Light gray text */
        font-family: "Segoe UI";    /* Or another clean, modern font */
        font-size: 9pt;
    }

    /* GroupBox Styling for a modern, clean look */
    QGroupBox {
        background-color: #353b46; /* Slightly lighter background for groups */
        border: 1px solid #4a5260;
        border-radius: 4px;
        margin-top: 10px; /* Provides space for the title */
    }

    QGroupBox::title {
        subcontrol-origin: margin;
        subcontrol-position: top center;
        padding: 0 5px;
        background-color: #353b46;
        border: none;
    }

    /* Apply a shared style to BOTH QToolButton and QPushButton */
    QToolButton, QPushButton {
        background-color: transparent;
        border: 1px solid #4a5260;
        border-radius: 4px;
        padding: 5px;
        min-width: 65px;
        min-height: 20px;
        color: #d5d5d5; /* Make sure text color is set */
    }

    QToolButton:hover, QPushButton:hover {
        background-color: #4a5260;
        border: 1px solid #5a6474;
    }

    QToolButton:pressed, QPushButton:pressed {
        background-color: #5a6474;
    }

    QToolButton:checked { /* This is for toggle buttons like your tools */
        background-color: #0078d7;
        color: white;
        border: 1px solid #0078d7;
    }

    QComboBox {
        background-color: #2c313a;
        border: 1px solid #4a5260;
        border-radius: 4px;
        padding: 5px;
        min-height: 20px;
    }

    QComboBox:hover {
        border: 1px solid #5a6474;
    }

    QComboBox::drop-down {
        border: none;
    }

    QComboBox::down-arrow {
        image: url(:/icons/chevron-down.png);
        width: 12px;
        height: 12px;
    }
    
    QSlider::groove:horizontal {
        border: 1px solid #4a5260;
        height: 4px; 
        background: #353b46;
        margin: 2px 0;
        border-radius: 2px;
    }

    QSlider::handle:horizontal {
        background: #0078d7;
        border: 1px solid #0078d7;
        width: 14px;
        margin: -5px 0; 
        border-radius: 7px;
    }

    QFrame[frameShape="VLine"] {
        border: 1px solid #4a5260;
    }
    QFrame[frameShape="HLine"] {
        border: 1px solid #4a5260;
    }
)";

// Light theme: mirrors sidePanelStyle with a light palette (kept in sync structurally).
// Swapped in by MainWindow::applyTheme on every dock + the ribbon when the user picks Light.
const QString lightPanelStyle = R"(
    QWidget {
        background-color: #f2f3f5;
        color: #20242b;
        font-family: "Segoe UI";
        font-size: 9pt;
    }
    QGroupBox {
        background-color: #e6e8ec;
        border: 1px solid #c2c7d0;
        border-radius: 4px;
        margin-top: 10px;
    }
    QGroupBox::title {
        subcontrol-origin: margin;
        subcontrol-position: top center;
        padding: 0 5px;
        background-color: #e6e8ec;
        border: none;
    }
    QToolButton, QPushButton {
        background-color: transparent;
        border: 1px solid #c2c7d0;
        border-radius: 4px;
        padding: 5px;
        min-width: 65px;
        min-height: 20px;
        color: #20242b;
    }
    QToolButton:hover, QPushButton:hover {
        background-color: #dde0e6;
        border: 1px solid #aab0bc;
    }
    QToolButton:pressed, QPushButton:pressed {
        background-color: #cfd3db;
    }
    QToolButton:checked {
        background-color: #0078d7;
        color: white;
        border: 1px solid #0078d7;
    }
    QComboBox {
        background-color: #ffffff;
        border: 1px solid #c2c7d0;
        border-radius: 4px;
        padding: 5px;
        min-height: 20px;
    }
    QComboBox:hover { border: 1px solid #aab0bc; }
    QComboBox::drop-down { border: none; }
    QComboBox::down-arrow { image: url(:/icons/chevron-down.png); width: 12px; height: 12px; }
    QSlider::groove:horizontal {
        border: 1px solid #c2c7d0;
        height: 4px;
        background: #d6dae1;
        margin: 2px 0;
        border-radius: 2px;
    }
    QSlider::handle:horizontal {
        background: #0078d7;
        border: 1px solid #0078d7;
        width: 14px;
        margin: -5px 0;
        border-radius: 7px;
    }
    QFrame[frameShape="VLine"] { border: 1px solid #c2c7d0; }
    QFrame[frameShape="HLine"] { border: 1px solid #c2c7d0; }
)";

void setNodeEditorStyle()
{
    // A bold, accented style for the nodes using safe color formats.
    QtNodes::NodeStyle::setNodeStyle(
        R"(
        {
          "NodeStyle": {
            "NormalBoundaryColor": [91, 74, 57],
            "SelectedBoundaryColor": [0, 120, 215],
            "GradientColor0": [53, 59, 70],
            "GradientColor1": [49, 54, 64],
            "GradientColor2": [44, 49, 58],
            "GradientColor3": [40, 44, 52],
            "ShadowColor": "#82281603",
            "FontColor" : [224, 224, 224],
            "FontColorFaded" : [160, 160, 160],
            "ConnectionPointColor": [90, 100, 116],
            "FilledConnectionPointColor": [191, 94, 9],
            "PenWidth": 2.0,
            "HoveredPenWidth": 2.5,
            "ConnectionPointDiameter": 11.0,
            "Opacity": 0.95
          }
        }
        )"
    );

    // An updated connection style using your orange and blue accents.
    QtNodes::ConnectionStyle::setConnectionStyle(
        R"(
        {
          "ConnectionStyle": {
            "ConstructionColor": "gray",
            "NormalColor": "#bf5e09",
            "SelectedColor": "#0090ff",
            "SelectedHaloColor": [0, 120, 215, 150],
            "HoveredColor": "#F39C12",
            "LineWidth": 3.5,
            "ConstructionLineWidth": 2.5,
            "PointDiameter": 10.0,
            "UseDataDefinedColors": false
          }
        }
        )"
    );
}

using ads::CDockWidget;

// ---------------------------------------------------------------------
//  Small helper that strips the Floatable flag from any CDockWidget
// ---------------------------------------------------------------------
static void makeNonFloatable(CDockWidget* dw)
{
    auto feats = dw->features();
    feats &= ~CDockWidget::DockWidgetFloatable;   // clear the bit
    dw->setFeatures(feats);
}

QString MainWindow::generateCameraColourCss(ads::CDockWidget* dock,
    entt::entity      camEntity,
    const QString& objectName)
{
    // 1. Give the dock a unique object name for specific targeting
    dock->setObjectName(objectName);

    // 2. Get color from the camera component
    auto& registry = m_scene->getRegistry();
    const auto& cam = registry.get<CameraComponent>(camEntity);
    const glm::vec3& t = cam.tint;

    QColor bgColor;
    bgColor.setRgbF(t.r, t.g, t.b);
    const QString bg = bgColor.name();
    const QString fg = (bgColor.lightnessF() > 0.5) ? "#000000" : "#ffffff";

    // 3. Generate and RETURN the CSS string for this one dock.
    //    Note the use of the descendant selector (a space).
    return QString(R"(
        ads--CDockWidget#%3 ads--CDockWidgetTitleBar {
            background-color: %1;
            color: %2;
        }
        ads--CDockWidget#%3 ads--CDockWidgetTab {
            background-color: %1;
            color: %2;
        }
    )").arg(bg, fg, objectName);
}
void MainWindow::disableFloatingForAllDockWidgets()
{
    const auto docks = m_dockManager->findChildren<ads::CDockWidget*>();

    for (auto* dw : docks)
        makeNonFloatable(dw);
}

namespace {
// Phase 4: reverse orb->node lifecycle plumbing. entt's on_destroy needs a FREE function (C++17 -- no
// lambda as a non-type template argument); it records the bound node id of a just-destroyed orb into a
// registry-context queue that the UI tick drains to delete the matching graph node.
struct OrbReverseQueue { std::vector<std::uint64_t> ids; };
void onOrbBindingDestroyed(entt::registry& r, entt::entity e) {
    const std::uint64_t id = r.get<OrbBindingComponent>(e).nodeId;
    OrbReverseQueue* q = r.ctx().find<OrbReverseQueue>();
    if (!q) q = &r.ctx().emplace<OrbReverseQueue>();
    q->ids.push_back(id);
}
} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
    m_colorInUse(kPalette.size(), false)
{
    qInfo() << ">>> ENTER MainWindow ctor";
    setNodeEditorStyle();
    // --- 1. Create the Scene and Initial Entities ---
    m_scene = std::make_unique<Scene>();
    auto& registry = m_scene->getRegistry();

    // Simulation exists BEFORE any entity spawns: spawn paths request
    // speculative collision cooks, which need the PhysX core (created
    // eagerly in the SimulationController constructor).
    m_simulation = std::make_unique<SimulationController>(m_scene.get(), this);

    // Phase V: the visibly-articulating FANUC is the DEFAULT boot scene (ROADMAP R) --
    // built through the SAME krs::fanuc helper the V-assign / V.2 / V.6 gates validate.
    // Any other demo (KRS_*_DEMO) or KRS_FEM_DEMO (the classic FEM block) takes precedence;
    // KRS_FANUC_DEMO forces it on. Needs OpenCASCADE + the deployed STEP asset.
    const std::string fanucStepPath =
        (QCoreApplication::applicationDirPath() + QLatin1String("/assets/FANUC-430 Robot.STEP")).toStdString();
    const bool bootFanuc = krs::cad::available()
        && std::filesystem::exists(fanucStepPath)
        && (qEnvironmentVariableIsSet("KRS_FANUC_DEMO")
            || (!qEnvironmentVariableIsSet("KRS_BENCH")
                && !qEnvironmentVariableIsSet("KRS_FLUID_DEMO")
                && !qEnvironmentVariableIsSet("KRS_SMOKE_DEMO")
                && !qEnvironmentVariableIsSet("KRS_MPM_DEMO")
                && !qEnvironmentVariableIsSet("KRS_HEAT_DEMO")
                && !qEnvironmentVariableIsSet("KRS_FEM_DEMO")));

    m_gizmoSystem = std::make_unique<GizmoSystem>(*m_scene);

    ads::CDockManager::setConfigFlag(ads::CDockManager::ActiveTabHasCloseButton, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasCloseButton, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::TabCloseButtonIsToolButton, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::AllTabsHaveCloseButton, true);

    // --- Set up scene-wide properties like fog ---
    auto& sceneProps = registry.ctx().emplace<SceneProperties>();
    sceneProps.fogEnabled = true;
    sceneProps.fogColor = glm::vec3(0.1f, 0.1f, 0.15f);
    sceneProps.fogStartDistance = 15.0f;
    sceneProps.fogEndDistance = 100.0f;
    // Test hook: boot with the collision wireframe overlay on.
    sceneProps.showCollisionShapes = qEnvironmentVariableIsSet("KRS_SHOW_COLLISION");

    // Sub-feature selection highlight state (hover + accumulating selected set),
    // rendered by SelectionHighlightPass; written by ViewportWidget hover/click.
    registry.ctx().emplace<krs::sel::SelectionState>();

    // --- Create a default grid ---
    {
        auto gridEntity = registry.create();
        registry.emplace<TagComponent>(gridEntity, "Primary Grid");
        registry.emplace<TransformComponent>(gridEntity);
        auto& gridComp = registry.emplace<GridComponent>(gridEntity);
        gridComp.levels.emplace_back(0.001f, glm::vec3(1.0f, 0.56f, 1.0f), 0.0f, 2.0f);
        gridComp.levels.emplace_back(0.01f, glm::vec3(0.66f, 0.66f, 0.5f), 0.0f, 5.0f);
        gridComp.levels.emplace_back(0.1f, glm::vec3(0.35f, 0.35f, 0.35f), 0.0f, 25.0f);
        gridComp.levels.emplace_back(1.0f, glm::vec3(1.0f, 0.6f, 0.0f), 5.0f, 50.0f);
        gridComp.levels.emplace_back(10.0f, glm::vec3(0.28f, 0.56f, 0.86f), 25.0f, 200.0f);
    }

    // --- Phase 3: REVIVE the field visualizer ---------------------------------------------------
    // The FieldVisualizerPass overlay has shipped DORMANT: it runs every frame but no
    // FieldVisualizerComponent has ever existed for it to act on (its view was always empty).
    // Emplace one so the arrow field actually renders over any field source -- the avoidance-
    // emitter node the operator drops places PointEffectors (+FieldSourceTag) that this now
    // traces direction + magnitude. The VISUALIZER-DATA gate proves the arrows ARE that field.
    // KRS_FIELD_DEMO adds a standalone point source so the starburst is visible at boot without
    // building a node graph (OPERATOR VISUAL-CONFIRM: a radial fan of blue->green->red arrows).
    {
        auto visEntity = registry.create();
        registry.emplace<TagComponent>(visEntity, "Field Visualizer");
        registry.emplace<TransformComponent>(visEntity);
        auto& vis = registry.emplace<FieldVisualizerComponent>(visEntity);
        vis.isEnabled = true;
        vis.displayMode = FieldVisualizerComponent::DisplayMode::Arrows;
        vis.bounds = { glm::vec3(-3.0f), glm::vec3(3.0f) };
        vis.arrowSettings.density = glm::ivec3(16, 16, 16);
        vis.arrowSettings.vectorScale = 0.2f;
        vis.arrowSettings.headScale = 0.5f;
        vis.arrowSettings.cullingThreshold = 0.01f;
        vis.arrowSettings.coloringMode = FieldVisualizerComponent::ColoringMode::Intensity;
        vis.arrowSettings.intensityGradient = {
            { 0.0f, glm::vec4(0.10f, 0.20f, 0.90f, 1.0f) },   // low  -> blue
            { 0.5f, glm::vec4(0.20f, 0.90f, 0.40f, 1.0f) },   // mid  -> green
            { 1.0f, glm::vec4(0.95f, 0.25f, 0.10f, 1.0f) },   // high -> red
        };
        vis.isGpuDataDirty = true;

        if (qEnvironmentVariableIsSet("KRS_FIELD_DEMO")) {
            auto src = registry.create();
            registry.emplace<TagComponent>(src, "Field Demo Source");
            auto& xf = registry.emplace<TransformComponent>(src);
            xf.translation = glm::vec3(0.0f, 0.8f, 0.0f);
            auto& pe = registry.emplace<PointEffectorComponent>(src);
            pe.strength = 2.0f; pe.radius = 2.5f; pe.falloff = PointEffectorComponent::FalloffType::Linear;
            registry.emplace<FieldSourceTag>(src);
        }
    }

    {
        QString assetDir = QCoreApplication::applicationDirPath() + QLatin1String("/assets/");

        // Demo material: packs live under assets/materials/<category>/<pack>/
        // with files named *-albedo, *-normal, *-roughness, *-metallic, *-ao,
        // *-height, *-emissive (see MaterialLoader). The default pack below
        // can be overridden with the KRS_DEMO_MATERIAL env var (a path
        // relative to assets/materials, e.g. "metals/brushed-steel").
        QString demoTexDir;
        QDir materialsRoot(assetDir + QLatin1String("materials"));
        // rocky-shoreline1 looked broken-dark for months: its albedo's MEAN
        // brightness is 0.15 — the texture itself is near-black. blocky-cliff
        // (0.37) reads as rock under our lighting.
        const QString requestedPack = qEnvironmentVariableIsSet("KRS_DEMO_MATERIAL")
            ? qEnvironmentVariable("KRS_DEMO_MATERIAL")
            : QStringLiteral("rocks/blocky-cliff");
        if (materialsRoot.exists(requestedPack)) {
            demoTexDir = materialsRoot.absoluteFilePath(requestedPack);
        }
        else {
            // Fallback: first directory (recursively) that contains an albedo map.
            QDirIterator it(materialsRoot.absolutePath(), QDir::Dirs | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                QDir d(it.next());
                if (!d.entryList({ QStringLiteral("*albedo*") }, QDir::Files).isEmpty()) {
                    demoTexDir = d.absolutePath();
                    break;
                }
            }
            if (!demoTexDir.isEmpty())
                qWarning() << "[Spawner] Pack" << requestedPack << "not found; falling back to" << demoTexDir;
        }
        const bool hasDemoTextures = !demoTexDir.isEmpty();
        if (hasDemoTextures)
            qInfo() << "[Spawner] Using material pack:" << demoTexDir;
        else
            qWarning() << "[Spawner] No material packs in" << materialsRoot.absolutePath()
                       << "— using untextured material.";

        // A real floor: a large concrete slab under the whole working area
        // (the photo "ground" below the grid was just the HDR skybox).
        // Skipped for benchmarks and the minimal fluid test scenes.
        if (!qEnvironmentVariableIsSet("KRS_BENCH")
            && !qEnvironmentVariableIsSet("KRS_FLUID_DEMO")) {
            auto& reg = m_scene->getRegistry();
            entt::entity slab = SceneBuilder::spawnPrimitive(
                *m_scene, int(Primitive::Cube), glm::vec3(0.0f, -0.1f, 0.0f),
                glm::vec3(24.0f, 0.2f, 24.0f), "Ground.Slab");
            auto& rb = reg.emplace<RigidBodyComponent>(slab);
            rb.bodyType = RigidBodyComponent::BodyType::Static;
            auto& col = reg.emplace<BoxCollider>(slab);
            col.halfExtents = glm::vec3(0.5f); // unit cube scaled by transform
            const QString concrete = materialsRoot.exists(QStringLiteral("concrete/clean-concrete"))
                ? materialsRoot.absoluteFilePath(QStringLiteral("concrete/clean-concrete"))
                : demoTexDir;
            if (!concrete.isEmpty()) {
                reg.emplace_or_replace<TriPlanarMaterialTag>(slab);
                reg.emplace_or_replace<MaterialDirectoryTag>(slab, concrete.toStdString());
                auto& req = reg.emplace_or_replace<MaterialReloadRequest>(slab);
                req.tilingOverride = 0.5f; // 2 m per tile: reads as poured slabs
            } else {
                auto& mat = reg.emplace_or_replace<MaterialComponent>(slab);
                mat.albedoColor = glm::vec3(0.55f);
            }
        }

        // --- Default lighting: a soft overhead AREA key + a dim point FILL ---
        // These primitive-attached emitters replace the legacy orbiting fallback light
        // (LightingPass injects that ONLY when the scene has no light entities). The
        // Settings-driven directional sun stays as gentle fill. KRS_NOFALLBACK leaves the
        // scene unlit for clean light tests; bench/fluid demos stay minimal.
        if (!qEnvironmentVariableIsSet("KRS_BENCH")
            && !qEnvironmentVariableIsSet("KRS_FLUID_DEMO")
            && !qEnvironmentVariableIsSet("KRS_NOFALLBACK")) {
            auto& reg = m_scene->getRegistry();
            // Soft overhead AREA key, 8x8 m at ~6 m, facing down. Intensity is LUMINANCE in
            // nits (the LTC reads it as radiance); exposure brings it to display range.
            entt::entity key = SceneBuilder::spawnLightEmitter(
                *m_scene, LightComponent::Type::RectArea, glm::vec3(0.0f, 6.0f, 0.0f),
                glm::vec3(1.0f, 0.97f, 0.92f), 8000.0f, "Key Light");
            if (auto* lc = reg.try_get<LightComponent>(key)) lc->size = glm::vec2(8.0f, 8.0f);
            if (auto* xf = reg.try_get<TransformComponent>(key))
                xf->scale = glm::vec3(8.0f, 8.0f, 1.0f);   // visible panel matches lc.size
            // Cool point FILL off to one side. Intensity is luminous power in LUMENS (1/d^2
            // falloff), so it needs a large value to matter at a few metres.
            SceneBuilder::spawnLightEmitter(
                *m_scene, LightComponent::Type::Point, glm::vec3(3.5f, 3.0f, 3.5f),
                glm::vec3(0.85f, 0.92f, 1.0f), 60000.0f, "Fill Light");
        }

        // Default boot content (Phase 5): a RIGID + FEM 6061-aluminium block on the
        // floor with a heater at one face. This is the VISUALIZATION TARGET rendered
        // as a SOLID surface (not particles) -- the async FEM oracle computes the true
        // linear-elastic stress (under gravity + its own weight, clamped base) and the
        // steady heat field, and the Visualize dropdown (Stress/Thermal/Strain)
        // recolours the surface through the unified ramp. Rigid + FEM is the DEFAULT
        // representation for solids; MPM is reserved for soft / large-deformation
        // bodies (representation policy, ROADMAP §L). Skipped under demo/bench scenes.
        if (!bootFanuc && !qEnvironmentVariableIsSet("KRS_BENCH") && !qEnvironmentVariableIsSet("KRS_FLUID_DEMO")
            && !qEnvironmentVariableIsSet("KRS_SMOKE_DEMO") && !qEnvironmentVariableIsSet("KRS_MPM_DEMO")
            && !qEnvironmentVariableIsSet("KRS_HEAT_DEMO")) {
            auto& registry = m_scene->getRegistry();
            // Solid 0.64 m cube resting on the floor (a real triangle mesh surface).
            entt::entity block = SceneBuilder::spawnPrimitive(
                *m_scene, int(Primitive::Cube), glm::vec3(0.0f, 0.32f, 0.0f),
                glm::vec3(0.64f, 0.64f, 0.64f), "Aluminium Block (FEM)");
            auto& rb = registry.emplace<RigidBodyComponent>(block);
            rb.bodyType = RigidBodyComponent::BodyType::Static;   // fixed solid (not soft MPM)
            auto& col = registry.emplace<BoxCollider>(block);
            col.halfExtents = glm::vec3(0.5f);
            auto& mat = registry.emplace_or_replace<MaterialComponent>(block); // defaults = 6061-T6
            mat.albedoColor = glm::vec3(0.82f, 0.83f, 0.85f);
            auto& fem = registry.emplace<FemBodyComponent>(block);
            fem.resolution = 14; fem.useGravity = true; fem.fixBottom = true; fem.solveThermal = true;

            entt::entity heater = registry.create();
            registry.emplace<TransformComponent>(heater, glm::vec3(-0.4f, 0.32f, 0.0f),
                                                 glm::quat(1, 0, 0, 0), glm::vec3(0.15f));
            auto& hs = registry.emplace<HeatSourceComponent>(heater);
            hs.power = 5.0e3f;                 // W into the block's near face (Neumann)
            hs.radius = 0.3f;
            hs.temperature = 320.0f;           // nominal glow colour
            registry.emplace<TagComponent>(heater, std::string("Heater"));
        }
    }

    // --- Minimal fluid-render test scene (KRS_FLUID_DEMO): a walled basin
    // with water, dead-centre in front of the default camera.
    if (qEnvironmentVariableIsSet("KRS_FLUID_DEMO")) {
        auto& registry = m_scene->getRegistry();
        auto wall = [&](const char* name, glm::vec3 pos, glm::vec3 halfExt) {
            entt::entity e = SceneBuilder::spawnPrimitive(*m_scene, int(Primitive::Cube), pos,
                                                          halfExt * 2.0f, name);
            auto& rb = registry.emplace<RigidBodyComponent>(e);
            rb.bodyType = RigidBodyComponent::BodyType::Static;
            auto& col = registry.emplace<BoxCollider>(e);
            col.halfExtents = glm::vec3(0.5f);
            auto& mat = registry.emplace_or_replace<MaterialComponent>(e);
            mat.albedoColor = glm::vec3(0.55f, 0.52f, 0.48f);
        };
        const float W = 1.0f, H = 0.7f, T = 0.06f;
        wall("Basin.Wall+X", { W + T, H * 0.5f, 0 }, { T, H * 0.5f, W + 2 * T });
        wall("Basin.Wall-X", { -W - T, H * 0.5f, 0 }, { T, H * 0.5f, W + 2 * T });
        wall("Basin.Wall+Z", { 0, H * 0.5f, W + T }, { W, H * 0.5f, T });
        wall("Basin.Wall-Z", { 0, H * 0.5f, -W - T }, { W, H * 0.5f, T });

        entt::entity water = registry.create();
        registry.emplace<TransformComponent>(water, glm::vec3(0.0f, 0.3f, 0.0f),
                                             glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
        auto& vol = registry.emplace<FluidVolumeComponent>(water);
        vol.halfExtents = { 0.9f, 0.22f, 0.9f }; // fill top 0.52 — under the 0.7 rim
        registry.emplace<TagComponent>(water, std::string("TestWater"));

        // Level 2: drop a light cube in — it must SPLASH and FLOAT
        // (box 0.3 m³ displaces ~27 kg of water; 8 kg ⇒ ~30 % submerged).
        if (qEnvironmentVariable("KRS_FLUID_DEMO") == QLatin1String("2")) {
            entt::entity floater = SceneBuilder::spawnPrimitive(
                *m_scene, int(Primitive::Cube), { 0.0f, 1.3f, 0.0f }, glm::vec3(0.3f), "Floater");
            auto& rb = registry.emplace<RigidBodyComponent>(floater);
            rb.bodyType = RigidBodyComponent::BodyType::Dynamic;
            rb.mass = 8.0f;
            auto& col = registry.emplace<BoxCollider>(floater);
            col.halfExtents = glm::vec3(0.5f);
            auto& mat = registry.emplace_or_replace<MaterialComponent>(floater);
            mat.albedoColor = glm::vec3(0.85f, 0.65f, 0.2f);
        }
    }

    // --- Smoke/fire test scene (KRS_SMOKE_DEMO): an emitter on the ground
    //     in front of the default camera. =2 makes it a fire. ---
    if (qEnvironmentVariableIsSet("KRS_SMOKE_DEMO")) {
        auto& registry = m_scene->getRegistry();
        entt::entity e = SceneBuilder::spawnPrimitive(*m_scene, int(Primitive::IcoSphere),
                                                      glm::vec3(1.5f, 0.15f, 0.6f),
                                                      glm::vec3(0.12f), "SmokeSource");
        auto& mat = registry.emplace_or_replace<MaterialComponent>(e);
        auto& sm = registry.emplace<SmokeEmitterComponent>(e);
        if (qEnvironmentVariable("KRS_SMOKE_DEMO") == QLatin1String("2")) {
            sm.fuelRate = 2.5f; sm.temperature = 1.0f; sm.densityRate = 1.2f;
            sm.color = { 0.2f, 0.2f, 0.2f };
            mat.albedoColor = { 0.95f, 0.45f, 0.1f };
            mat.emissiveColor = { 1.0f, 0.4f, 0.05f };
            mat.emissiveStrength = 2.0f;
        } else {
            mat.albedoColor = { 0.7f, 0.7f, 0.75f };
        }
    }

    // --- MLS-MPM continuum demo (KRS_MPM_DEMO): material blocks that drop and
    //     deform on Play. 1=water, 2=elastic jello, 3=sand column, 4=all three.
    if (qEnvironmentVariableIsSet("KRS_MPM_DEMO")) {
        auto& reg = m_scene->getRegistry();
        const QString mode = qEnvironmentVariable("KRS_MPM_DEMO");
        auto addBody = [&](MpmMaterial m, glm::vec3 pos, glm::vec3 half, glm::vec3 col,
                           float E, float dens, float sp, glm::vec3 v0) {
            entt::entity e = reg.create();
            auto& xf = reg.emplace<TransformComponent>(e);
            xf.translation = pos;
            auto& b = reg.emplace<MpmBodyComponent>(e);
            b.material = m; b.halfExtents = half; b.color = col;
            b.youngsModulus = E; b.density = dens; b.particleSpacing = sp;
            b.initialVelocity = v0;
            if (m == MpmMaterial::Sand) b.frictionDegrees = 35.0f;
        };
        if (mode == QLatin1String("2")) {
            addBody(MpmMaterial::Elastic, { 0, 1.0f, 0 }, glm::vec3(0.30f), { 0.90f, 0.30f, 0.38f },
                    8.0e4f, 1000.0f, 0.045f, { 0, 0, 0 });
        } else if (mode == QLatin1String("3")) {
            addBody(MpmMaterial::Sand, { 0, 1.1f, 0 }, glm::vec3(0.25f, 0.45f, 0.25f),
                    { 0.86f, 0.72f, 0.45f }, 6.0e5f, 1600.0f, 0.04f, { 0, 0, 0 });
        } else if (mode == QLatin1String("4")) {
            addBody(MpmMaterial::Fluid, { -0.65f, 1.0f, 0 }, glm::vec3(0.24f), { 0.20f, 0.45f, 0.85f },
                    5.0e4f, 1000.0f, 0.045f, { 0, 0, 0 });
            addBody(MpmMaterial::Elastic, { 0.0f, 1.0f, 0 }, glm::vec3(0.22f), { 0.90f, 0.30f, 0.38f },
                    8.0e4f, 1000.0f, 0.045f, { 0, 0, 0 });
            addBody(MpmMaterial::Sand, { 0.65f, 1.0f, 0 }, glm::vec3(0.20f), { 0.86f, 0.72f, 0.45f },
                    6.0e5f, 1600.0f, 0.04f, { 0, 0, 0 });
        } else if (mode == QLatin1String("5")) {
            // Thermodynamics: an ice block (melt point 0C, starts cold) in a
            // warm ambient melts into water as it heats past its threshold.
            entt::entity e = reg.create();
            reg.emplace<TransformComponent>(e).translation = glm::vec3(0, 0.9f, 0);
            auto& b = reg.emplace<MpmBodyComponent>(e);
            b.material = MpmMaterial::Elastic;
            b.halfExtents = glm::vec3(0.28f);
            b.color = { 0.78f, 0.86f, 0.95f };  // icy white-blue
            b.youngsModulus = 1.2e5f; b.density = 900.0f; b.particleSpacing = 0.045f;
            b.temperature = -12.0f; b.meltTemperature = 0.0f;
        } else if (mode == QLatin1String("6")) {
            // Viscous fluid (honey): high dynamic viscosity, soft EOS so it
            // oozes into a slow dome instead of splashing like water.
            entt::entity e = reg.create();
            reg.emplace<TransformComponent>(e).translation = glm::vec3(0, 0.9f, 0);
            auto& b = reg.emplace<MpmBodyComponent>(e);
            b.material = MpmMaterial::Fluid;
            b.halfExtents = glm::vec3(0.24f);
            b.color = { 0.85f, 0.6f, 0.12f };  // amber
            b.youngsModulus = 3.0e4f; b.density = 1400.0f; b.particleSpacing = 0.04f;
            b.viscosity = 6.0f;
        } else {
            addBody(MpmMaterial::Fluid, { 0, 1.0f, 0 }, glm::vec3(0.30f), { 0.20f, 0.45f, 0.85f },
                    5.0e4f, 1000.0f, 0.045f, { 0, 0, 0 });
        }
    }

    // --- Thermodynamics demo (KRS_HEAT_DEMO): a flame scorches a suspended MPM
    //     block, and a HeatSourceComponent "motor" glows + heats a second block.
    //     View > Physics Visualization > Thermal (or KRS_MPM_VIZ=1) to watch it.
    if (qEnvironmentVariableIsSet("KRS_HEAT_DEMO")) {
        auto& reg = m_scene->getRegistry();
        // Fire emitter on the ground.
        entt::entity fire = SceneBuilder::spawnPrimitive(*m_scene, int(Primitive::IcoSphere),
                                                         glm::vec3(0, 0.12f, 0), glm::vec3(0.12f), "Flame");
        auto& fmat = reg.emplace_or_replace<MaterialComponent>(fire);
        fmat.emissiveColor = { 1.0f, 0.4f, 0.05f }; fmat.emissiveStrength = 2.0f;
        auto& sm = reg.emplace<SmokeEmitterComponent>(fire);
        sm.fuelRate = 2.5f; sm.temperature = 1.0f; sm.densityRate = 1.2f; sm.color = { 0.2f, 0.2f, 0.2f };
        // Elastic block suspended in the plume -> scorched by the flame.
        { entt::entity e = reg.create();
          reg.emplace<TransformComponent>(e).translation = glm::vec3(0, 0.9f, 0);
          auto& b = reg.emplace<MpmBodyComponent>(e);
          b.material = MpmMaterial::Elastic; b.halfExtents = glm::vec3(0.22f);
          b.color = { 0.75f, 0.75f, 0.78f }; b.youngsModulus = 8.0e4f;
          b.density = 1000.0f; b.particleSpacing = 0.045f; }
        // "Motor": a HeatSourceComponent body that glows and heats a nearby block.
        { entt::entity m = SceneBuilder::spawnPrimitive(*m_scene, int(Primitive::Cube),
                                                        glm::vec3(1.1f, 0.25f, 0), glm::vec3(0.12f), "Motor");
          reg.emplace_or_replace<MaterialComponent>(m);
          auto& hs = reg.emplace<HeatSourceComponent>(m);
          hs.temperature = 180.0f; hs.radius = 0.45f; }
        { entt::entity e = reg.create();
          reg.emplace<TransformComponent>(e).translation = glm::vec3(1.1f, 0.55f, 0);
          auto& b = reg.emplace<MpmBodyComponent>(e);
          b.material = MpmMaterial::Elastic; b.halfExtents = glm::vec3(0.16f);
          b.color = { 0.7f, 0.7f, 0.75f }; b.youngsModulus = 8.0e4f;
          b.density = 1000.0f; b.particleSpacing = 0.04f; }
    }

    // --- Physics demo: a small stack of primitives that falls on Play ---
    // (Skipped under KRS_BENCH: benchmarks need a clean world; and under the FANUC boot.)
    if (!bootFanuc && !qEnvironmentVariableIsSet("KRS_BENCH") && !qEnvironmentVariableIsSet("KRS_FLUID_DEMO")
        && !qEnvironmentVariableIsSet("KRS_SMOKE_DEMO") && !qEnvironmentVariableIsSet("KRS_MPM_DEMO")
        && !qEnvironmentVariableIsSet("KRS_HEAT_DEMO")) {
        auto& registry = m_scene->getRegistry();
        auto addRigidPrimitive = [&](Primitive prim, const char* name, glm::vec3 pos,
                                     glm::vec3 scale, bool sphere) {
            entt::entity e = SceneBuilder::spawnPrimitive(*m_scene, int(prim), pos, scale, name);
            auto& rb = registry.emplace<RigidBodyComponent>(e);
            rb.bodyType = RigidBodyComponent::BodyType::Dynamic;
            rb.mass = 1.0f;
            if (sphere) {
                auto& col = registry.emplace<SphereCollider>(e);
                // Local-space radius: unit icosphere is 1 m across; the
                // backend multiplies by the entity scale (was double-scaled).
                col.radius = 0.5f;
                col.material.restitution = 0.45f;
            }
            else {
                auto& col = registry.emplace<BoxCollider>(e);
                col.halfExtents = glm::vec3(0.5f);
                col.material.restitution = 0.15f;
            }
            auto& mat = registry.emplace_or_replace<MaterialComponent>(e);
            mat.albedoColor = sphere ? glm::vec3(0.75f, 0.35f, 0.2f) : glm::vec3(0.3f, 0.5f, 0.8f);
            return e;
        };

        // Keep clear of the dragon's collision mesh at the origin, but close
        // enough that the default camera frames floor + dragon + stack + glass.
        addRigidPrimitive(Primitive::Cube, "Cube.001", { 3.0f, 0.5f, -0.5f }, glm::vec3(1.0f), false);
        addRigidPrimitive(Primitive::Cube, "Cube.002", { 3.05f, 1.6f, -0.45f }, glm::vec3(1.0f), false);
        addRigidPrimitive(Primitive::Cube, "Cube.003", { 2.95f, 2.7f, -0.55f }, glm::vec3(1.0f), false);
        addRigidPrimitive(Primitive::IcoSphere, "Sphere.001", { 3.3f, 4.2f, -0.4f }, glm::vec3(0.5f), true);
        addRigidPrimitive(Primitive::IcoSphere, "Sphere.002", { 2.7f, 5.2f, -0.6f }, glm::vec3(0.5f), true);

        // --- Fluid demo: an open-top "glass" with water seeded inside and a
        // tap pouring more in from above. Walls are static rigid boxes so the
        // PhysX bodies AND the fluid both collide with them.
        auto addWall = [&](const char* name, glm::vec3 pos, glm::vec3 halfExt) {
            entt::entity e = SceneBuilder::spawnPrimitive(*m_scene, int(Primitive::Cube), pos,
                                                          halfExt * 2.0f, name);
            auto& rb = registry.emplace<RigidBodyComponent>(e);
            rb.bodyType = RigidBodyComponent::BodyType::Static;
            auto& col = registry.emplace<BoxCollider>(e);
            col.halfExtents = glm::vec3(0.5f); // unit cube scaled by transform
            auto& mat = registry.emplace_or_replace<MaterialComponent>(e);
            mat.albedoColor = glm::vec3(0.65f, 0.65f, 0.7f);
            // It's called a glass — render it as one (GlassPass: screen-space
            // refraction + Fresnel). The floor stays opaque.
            if (std::string(name).find("Wall") != std::string::npos)
                registry.emplace<GlassComponent>(e);
            return e;
        };
        const glm::vec3 g(-2.0f, 0.0f, 0.0f);  // glass center on the ground
        const float W = 0.45f, H = 0.6f, T = 0.05f; // inner half-width, height, wall thickness
        addWall("Glass.Floor", g + glm::vec3(0, T, 0), { W + 2 * T, T, W + 2 * T });
        addWall("Glass.Wall+X", g + glm::vec3(W + T, H * 0.5f, 0), { T, H * 0.5f, W + 2 * T });
        addWall("Glass.Wall-X", g + glm::vec3(-W - T, H * 0.5f, 0), { T, H * 0.5f, W + 2 * T });
        addWall("Glass.Wall+Z", g + glm::vec3(0, H * 0.5f, W + T), { W, H * 0.5f, T });
        addWall("Glass.Wall-Z", g + glm::vec3(0, H * 0.5f, -W - T), { W, H * 0.5f, T });

        // Water sitting in the glass at t=0
        {
            entt::entity water = registry.create();
            registry.emplace<TransformComponent>(water, g + glm::vec3(0.0f, 0.32f, 0.0f),
                                                 glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
            auto& vol = registry.emplace<FluidVolumeComponent>(water);
            vol.halfExtents = { 0.38f, 0.22f, 0.38f };
            registry.emplace<TagComponent>(water, std::string("Water.Volume"));
        }
        // Tap above, pouring in
        {
            entt::entity tap = registry.create();
            registry.emplace<TransformComponent>(tap, g + glm::vec3(0.15f, 2.0f, 0.0f),
                                                 glm::quat(1, 0, 0, 0), glm::vec3(1.0f));
            auto& em = registry.emplace<FluidEmitterComponent>(tap);
            em.ratePerSecond = 250.0f;
            em.initialSpeed = 0.8f;
            em.spreadDegrees = 3.0f;
            em.emitterRadius = 0.025f;
            em.particleLifetime = 0.0f; // immortal — fills the glass
            registry.emplace<TagComponent>(tap, std::string("Water.Tap"));
        }
    }

    // --- Phase V: boot into the visibly-articulating FANUC (default scene) ---
    // The SAME shared helper + kinematic demo drive the V-assign / V.2 / V.6 gates
    // validate: the 17 STEP solids track their serial links live, J1/J2/J3 sweep on
    // play. setupFanucScene imports + builds the canonical articulation + maps the
    // solids; the sim auto-plays so the arm moves from the first frame.
    if (bootFanuc && !qEnvironmentVariableIsSet("KRS_FANUC_LEGACY")) {
        // TRUE 6-DoF boot (DEFAULT): STEPCAF assembly import -> one entity per NAMED part -> name-driven
        // serial chain (base->j1..j6, the 430_j3-* cluster collapsed into one link) -> first-class
        // LiveRobot. Each joint drives its OWN geometry via FK viz (no PhysX needed). The graph is
        // stashed in the ctx so the Robot Builder can edit it (re-type / redefine / refine axes).
        auto& reg = m_scene->getRegistry();
        std::vector<krs::rbuild::ParsedPart> parts = krs::cad::importStepAssembly(*m_scene, fanucStepPath);
        krs::rbuild::RobotGraph g = krs::rbuild::buildNamedSerialChain(parts);
        g.robotId = 0;
        { auto* gp = reg.ctx().find<krs::rbuild::RobotGraph>();
          if (!gp) gp = &reg.ctx().emplace<krs::rbuild::RobotGraph>();
          *gp = g; }
        if (krs::robot::LiveRobot* lr = krs::robot::instantiateFromGraph(*m_scene, g, 0)) {
            lr->name = lr->model.name = "FANUC-430";
            lr->ownsDrive = true; lr->useRobotFkViz = true;
            if (reg.valid(lr->root)) {
                reg.emplace_or_replace<RobotRootComponent>(lr->root, RobotRootComponent{ "FANUC-430", 0 });
                reg.emplace_or_replace<TagComponent>(lr->root, std::string("FANUC-430"));
            }
            QString lc; for (int k = 0; k < int(lr->linkEntities.size()); ++k)
                lc += QString::number(int(lr->linkEntities[k].size())) + " ";
            qInfo() << "[FANUC] 6-DoF assembly boot:" << int(parts.size()) << "named parts; dof" << lr->ndof()
                    << " linkEntityCounts=[" << lc.trimmed() << "] (each >0 => that joint drives its own geometry)";
        } else {
            qWarning() << "[FANUC] 6-DoF assembly boot FAILED (import/chain)";
        }
    }
    else if (bootFanuc) {
        krs::fanuc::Setup fs = krs::fanuc::setupFanucScene(*m_scene, *m_simulation, fanucStepPath);
        if (fs.ok) {
            // The hardcoded demo sweep is GONE. The FANUC is driven by the DEFAULT NODE GRAPH spawned
            // below (time_source -> gen_sine -> physics_articulation_drive) -- the single joint driver.
            qInfo() << "[FANUC] visible articulated robot booted:" << fs.solids
                    << "bodies (solids+shells); assignment" << QString::fromStdString(fs.fingerprint);
            // STEP 6a: make the FANUC a FIRST-CLASS Robot -- a named root entity with all
            // its bodies nested under it + a real robotId + the chain link->entity map.
            // ownsDrive=false, so the legacy node-graph drive keeps the sweep intact; this
            // only adds outliner hierarchy + robot selection (drive ownership is step 6b).
            if (krs::robot::LiveRobot* lr = krs::robot::instantiateFanucRobot(
                    *m_scene, fs.movingLinkEntities, fs.solidEntity, /*robotId*/ 0,
                    QStringLiteral("FANUC-430").toStdString())) {
                qInfo() << "[FANUC] registered as first-class Robot 'FANUC-430' (robotId 0, dof"
                        << lr->ndof() << ")";
            }
        } else {
            qWarning() << "[FANUC] setup failed:" << QString::fromStdString(fs.message);
        }
    }

    // --- Create Splines ---
    {
        /*
        using SB = SceneBuilder;
        auto& reg = m_scene->getRegistry();
        auto CREntity = SB::makeCR(reg, { {-2,0,-1}, {-2,0, 1}, { 2,0, 1}, { 2,0,-1} }, { 0.9f,0.9f,0.9f,1 }, { 0.9f,0.3f,0.3f,1 }, 18.0f);
        reg.emplace<PulsingSplineTag>(CREntity);
        auto ParamEntity = SB::makeParam(reg, [](float t) { return glm::vec3(2 * std::cos(6.28f * 3 * t), 6 * t, 2 * std::sin(6.28f * 3 * t)); }, { 0.9f,0.9f,0.9f,1 }, { 0.2f,0.6f,1.0f,1 }, 12.0f);
        reg.emplace<PulsingSplineTag>(ParamEntity);
        auto LinearEntity = SB::makeLinear(reg, { {-5, 0.1, -5}, {-5, 2, -5}, {0, 2, -5}, {0, 0.1, -5} }, { 0.9f,0.9f,0.9f,1 }, { 0.1f, 1.0f, 0.2f, 1 }, 18.0f);
        reg.emplace<PulsingSplineTag>(LinearEntity);
        auto bezierEntity = SB::makeBezier(reg, { {5, 0.1, -5}, {5, 4, -5}, {2, 4, -5}, {2, 0.1, -5} }, { 0.9f,0.9f,0.9f,1 }, { 1.0f, 0.9f, 0.2f, 1 }, 18.0f);
        reg.emplace<PulsingSplineTag>(bezierEntity);*/
    }

    // --- Create initial cameras for the viewports ---
    const QColor& c0 = kPalette[0];
    glm::vec3 tint0{ c0.redF(), c0.greenF(), c0.blueF() };

    auto cameraEntity1 = SceneBuilder::createCamera(*m_scene, { 0, 2, 5 }, tint0);

    // mark slot 0 taken
    m_colorInUse[0] = true;
    registry.emplace<ColorIndexComponent>(cameraEntity1, 0);

    // --- 2. Create the SINGLE Shared Rendering System ---
    m_renderingSystem = std::make_unique<RenderingSystem>(nullptr);

    // Two-way fluid coupling: the FluidSystem is created lazily on the
    // engine GL context, so retry the wiring until it exists.
    {
        auto* couplingTimer = new QTimer(this);
        connect(couplingTimer, &QTimer::timeout, this, [this, couplingTimer]() {
            FluidSystem* fluid =
                m_renderingSystem ? m_renderingSystem->getFluidSystem() : nullptr;
            if (!fluid) return;
            fluid->setImpulseSink([this](entt::entity e, const glm::vec3& impulse) {
                if (m_simulation) m_simulation->applyFluidImpulse(e, impulse);
            });
            couplingTimer->stop();
            couplingTimer->deleteLater();
        });
        couplingTimer->start(500);
    }

    // ---------------------------------------------------------------------------
    // 3.  Core UI layout & dock setup
    // ---------------------------------------------------------------------------
    m_centralContainer = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(m_centralContainer);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // fixed toolbar
    m_fixedTopToolbar = new StaticToolbar(this);
    mainLayout->addWidget(m_fixedTopToolbar, 0);

    // Grab the toolbar�s embedded popup
    auto* popup = m_fixedTopToolbar->viewportManagerPopup();
    m_viewportManagerPopup = popup;
    // Wire up its buttons to your slots
    connect(popup, &ViewportManagerPopup::addViewportRequested, this, &MainWindow::addViewport);
    connect(popup, &ViewportManagerPopup::removeViewportRequested, this, &MainWindow::removeViewport);
    connect(popup, &ViewportManagerPopup::resetViewportsRequested, this, &MainWindow::onResetViewports);
    connect(popup, &ViewportManagerPopup::showViewportRequested, this, &MainWindow::onShowViewportRequested);

    // Also re-sync the initial state of the menu
    popup->updateUi(m_dockContainers, m_scene.get());
    syncViewportManagerPopup();

    // ADS dock manager
    m_dockManager = new ads::CDockManager();
    mainLayout->addWidget(m_dockManager, 1);

    connect(m_dockManager, &ads::CDockManager::dockWidgetRemoved, this, &MainWindow::onViewportDockClosed);
    setCentralWidget(m_centralContainer);
    setDockManagerBaseStyle();

    // ---------------------------------------------------------------------------
    // Viewport 1  (left)
    // ---------------------------------------------------------------------------
    ViewportWidget* viewport1 = new ViewportWidget(
        m_scene.get(), m_renderingSystem.get(), cameraEntity1, this);

    auto* viewportDock1 = new ads::CDockWidget(QStringLiteral("3D Viewport 1"));
    viewportDock1->setWidget(viewport1);
    viewportDock1->setFeature(ads::CDockWidget::DockWidgetFloatable, false);

    auto* viewportArea1 =
        m_dockManager->addDockWidget(ads::LeftDockWidgetArea, viewportDock1);

    if (auto* area = viewportDock1->dockAreaWidget()) {
        if (auto* tb = area->titleBar()) {
            const glm::vec3 camTint =
                m_scene->getRegistry()
                .get<CameraComponent>(cameraEntity1).tint;
            glm::vec3 darkTint = camTint * 0.8f;

            QColor bg; bg.setRgbF(darkTint.r, darkTint.g, darkTint.b);
            QColor fg = Qt::white;

            tb->setStyleSheet(QStringLiteral(
                "background:%1;"
                "color:%2;"
                "border:0;"
            ).arg(bg.name(), fg.name()));
        }
    }

    m_dockContainers << viewportDock1;
    applyCameraColorToDock(viewportDock1, cameraEntity1);

    viewport1->setGizmoSystem(m_gizmoSystem.get());

    connect(
        viewport1,
        &ViewportWidget::selectionChanged,
        this,
        &MainWindow::onSelectionChanged
    );
    connect(viewport1, &ViewportWidget::assetDropped, this, &MainWindow::spawnMeshAssetAt);
    connect(viewport1, &ViewportWidget::contextMenuRequested, this,
            &MainWindow::onViewportContextMenu);

    connect(viewport1, &ViewportWidget::gizmoModeRequested, this, [this](int m) {
        if (!m_gizmoSystem) return;
        switch (m) {
        case 0: m_gizmoSystem->setMode(GizmoMode::Translate); break;
        case 1: m_gizmoSystem->setMode(GizmoMode::Rotate);    break;
        case 2: m_gizmoSystem->setMode(GizmoMode::Scale);     break;
        }
        });

    connect(viewport1, &ViewportWidget::gizmoHandleDoubleClicked,
        this, [this](int mode, int axis) {
            if (!m_gizmoSystem) return;
            switch (mode) {
            case 0: m_gizmoSystem->onTranslateDoubleClick(static_cast<GizmoAxis>(axis)); break;
            case 2: m_gizmoSystem->onScaleDoubleClick(static_cast<GizmoAxis>(axis));     break;
            default: break; // rotation: no-op for now
            }
        });

    connect(viewportDock1, &ads::CDockWidget::closed, this, [this, viewportDock1]() {
        if (auto* vp = qobject_cast<ViewportWidget*>(viewportDock1->widget())) {
            destroyCameraRig(vp->getCameraEntity());
        }
        QTimer::singleShot(0, this, [this, viewportDock1]() {
            m_dockContainers.removeAll(viewportDock1);
            syncViewportManagerPopup();
            });
        });

    // Connect the database panel's scene reload signal
    // This is connected here, but the panel itself is created on-demand.
    // We need to connect it when the panel is created in showMenu.
    // connect(databasePanel, &DatabasePanel::requestSceneReload, this, &MainWindow::onSceneReloadRequested);

    // --- Set Proportions for the three main dock areas ---
    ads::CDockSplitter* mainSplitter = viewportArea1->parentSplitter();
    if (mainSplitter)
    {
        // Blender-style layout: the 3D viewport dominates, side panels flank it.
        mainSplitter->setSizes({ 5, 2, 2 });
    }

    // ===================================================================
    // --- Node Editor Setup (with combined Catalog and Editor) ---
    // ===================================================================
    auto nodeRegistry = std::make_shared<QtNodes::NodeDelegateModelRegistry>();
    for (auto const& [typeId, desc] : NodeFactory::instance().getRegisteredNodeTypes())
    {
        nodeRegistry->registerModel<NodeDelegate>(
            [typeId]() {
                return std::make_unique<NodeDelegate>(typeId);
            },
            QString::fromStdString(desc.category)
        );
    }

    auto graphModel = std::make_shared<QtNodes::DataFlowGraphModel>(nodeRegistry);
    m_nodeScene = new CustomDataFlowScene(*graphModel, this);
    m_nodeView = new DroppableGraphicsView(m_nodeScene, this);

    auto* nodeEditorContainer = new QWidget();
    auto* splitter = new QSplitter(Qt::Horizontal, nodeEditorContainer);
    NodeCatalogWidget* catalog = new NodeCatalogWidget(splitter);
    catalog->setMaximumWidth(300);
    catalog->setMinimumWidth(200);
    splitter->addWidget(catalog);
    splitter->addWidget(m_nodeView);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    // Eval-rate control (the refresh-rate feature): the graph EVALUATES at this rate (30Hz..20kHz for a
    // tight control loop); the UI repaint is hard-capped at 30Hz independently (so widgets never repaint at
    // kHz). The two rates are deliberately different (GATE RATE).
    auto* evalRateRow = new QWidget(nodeEditorContainer);
    auto* evalRateLayout = new QHBoxLayout(evalRateRow);
    evalRateLayout->setContentsMargins(6, 2, 6, 2);
    evalRateLayout->addWidget(new QLabel(QStringLiteral("Eval Hz:")));
    auto* evalRateSpin = new QDoubleSpinBox(evalRateRow);
    evalRateSpin->setRange(1.0, 20000.0); evalRateSpin->setDecimals(0); evalRateSpin->setValue(60.0);
    evalRateLayout->addWidget(evalRateSpin);
    evalRateLayout->addWidget(new QLabel(QStringLiteral("(UI repaint capped at 30 Hz)")));
    evalRateLayout->addStretch(1);
    auto* containerLayout = new QVBoxLayout(nodeEditorContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->addWidget(evalRateRow, 0);
    containerLayout->addWidget(splitter, 1);
    nodeEditorContainer->setLayout(containerLayout);
    auto* combinedNodeDock = new ads::CDockWidget("Node Editor");
    combinedNodeDock->setWidget(nodeEditorContainer);
    combinedNodeDock->setStyleSheet(sidePanelStyle);
    m_dockManager->addDockWidget(ads::RightDockWidgetArea, combinedNodeDock, m_propertiesArea);
    registerPanelDock(QStringLiteral("Node Editor"), combinedNodeDock);

    connect(graphModel.get(), &QtNodes::AbstractGraphModel::nodeCreated,
        this, [this, graphModel](QtNodes::NodeId nodeId) {
            auto* delegate = graphModel->delegateModel<NodeDelegate>(nodeId);
            if (!delegate) return;
            // The delegate self-creates its backend node + widget lazily (the MOUNT FIX) during
            // NodeGraphicsObject construction, so the widget is already embedded. Here we just inject the
            // live Scene (Phase 5 backend bridge) and ask QtNodes to recompute the node's geometry.
            Node* n = delegate->backendNode();
            if (n) {
                n->setScene(m_scene.get());

                // Phase 4: a VelocityProbeOrb node spawns its bound glass-sphere probe. Distinct colour
                // per node (golden-ratio hue) so N nodes -> N orbs in N colours; the orb is a measurement
                // VOLUME (spawnPrimitive's auto-collider is stripped by decorateProbeOrb), tinted the
                // node's colour. The binding key is the QtNodes NodeId (param "orbNodeId").
                if (n->getId() == "velocity_probe_orb") {
                    const std::uint64_t id = std::uint64_t(nodeId);
                    const float h = std::fmod(float(id) * 0.61803398875f + 0.12f, 1.0f);  // distinct hue
                    const float s = 0.72f, v = 0.96f;
                    const float hh = h * 6.0f; const int hi = int(hh) % 6; const float fr = hh - std::floor(hh);
                    const float p = v * (1 - s), q = v * (1 - s * fr), t = v * (1 - s * (1 - fr));
                    glm::vec3 color;
                    switch (hi) {
                        case 0: color = { v, t, p }; break; case 1: color = { q, v, p }; break;
                        case 2: color = { p, v, t }; break; case 3: color = { p, q, v }; break;
                        case 4: color = { t, p, v }; break; default: color = { v, p, q }; break;
                    }
                    const glm::vec3 center(0.0f, 0.8f, 0.0f);
                    const float radius = 0.5f;     // scale 0.5 * base-mesh radius 0.5 = 0.25 m world radius (grabbable)
                    entt::entity orb = SceneBuilder::spawnPrimitive(*m_scene, int(Primitive::IcoSphere),
                                                                    center, glm::vec3(radius), "Velocity Probe Orb");
                    krs::orb::decorateProbeOrb(m_scene->getRegistry(), orb, id, color, center, radius);
                    n->setParam<long long>("orbNodeId", (long long)id);
                    n->setParam<double>("radius", double(radius));
                    n->setPortLiteral<double>("Radius", double(radius));   // so the in-node Radius widget shows the live size
                    qInfo() << "[orb] spawned velocity-probe orb for node" << id;
                }
            }
            Q_EMIT graphModel->nodeUpdated(nodeId);
        });

    // Phase 4: deleting the NODE removes its bound orb (forward lifecycle).
    connect(graphModel.get(), &QtNodes::AbstractGraphModel::nodeDeleted,
        this, [this](QtNodes::NodeId nodeId) {
            if (m_scene) krs::orb::removeOrbForNode(m_scene->getRegistry(), std::uint64_t(nodeId));
        });

    // Phase 4: deleting the ORB removes its node (reverse lifecycle). An entt on_destroy observer on
    // OrbBindingComponent records the bound node id; the UI tick deletes that graph node IF it still
    // exists (when the node was the one deleted, it is already gone -> the guard breaks the cycle, so a
    // node-side delete does not loop back through here). This closes the bidirectional binding.
    {
        m_scene->getRegistry().on_destroy<OrbBindingComponent>().connect<&onOrbBindingDestroyed>();
        auto* orbReverseTimer = new QTimer(this);
        connect(orbReverseTimer, &QTimer::timeout, this, [this, graphModel]() {
            if (!m_scene) return;
            auto& reg = m_scene->getRegistry();
            OrbReverseQueue* pending = reg.ctx().find<OrbReverseQueue>();
            if (!pending || pending->ids.empty()) return;
            std::vector<std::uint64_t> ids; ids.swap(pending->ids);
            for (std::uint64_t id : ids)
                if (graphModel->nodeExists(QtNodes::NodeId(id)))     // skip ids whose node is already gone
                    graphModel->deleteNode(QtNodes::NodeId(id));
        });
        orbReverseTimer->start(50);
    }

    // LIVE GRAPH TICK: re-evaluate time-source nodes every frame so downstream time-parametric nodes
    // (sine/ramp/oscillator) actually move over wall-clock instead of emitting a constant.
    // Decouple UI edits from the physics thread: dial/spinbox edits POST to the coalescing queue instead
    // of recomputing inline, and we DRAIN it once per frame here. A rapid drag no longer does O(graph) work
    // per event on the sim's critical path (GATE THREAD). Gates that read output immediately keep the
    // default immediate mode; the live app turns deferral on.
    krs::nodes::NodeEditQueue::instance().setDeferred(true);

    // EVALUATION tick (BUG 2 fix): evaluate the graph QUIETLY -- process every node + propagate backend data
    // with NO QtNodes dataUpdated, so there is NO per-eval scene repaint cascade (the ~45ms blowup). The math
    // is ~free, so this runs at the configurable eval rate (a tight control loop can ask for kHz).
    auto evalIterPerFire = std::make_shared<int>(1);
    auto* evalTimer = new QTimer(this);
    connect(evalTimer, &QTimer::timeout, this, [graphModel, evalIterPerFire]() {
        krs::nodes::NodeEditQueue::instance().drain();   // apply coalesced UI edits (off the per-event path)
        for (int i = 0; i < *evalIterPerFire; ++i) krs::nodes::evaluateGraphQuiet(*graphModel);
    });
    auto setEvalRate = [evalTimer, evalIterPerFire](double hz) {
        hz = std::clamp(hz, 1.0, 20000.0);
        if (hz <= 250.0) { evalTimer->setInterval(int(std::round(1000.0 / hz))); *evalIterPerFire = 1; }
        else { evalTimer->setInterval(4); *evalIterPerFire = std::max(1, int(std::round(hz / 250.0))); }  // >1kHz: iterate per fire
    };
    setEvalRate(60.0);   // poll the node graph per frame so live property streams (velocity etc.) stay fresh
    evalTimer->start();
    connect(evalRateSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [setEvalRate](double hz) { setEvalRate(hz); });

    // UI repaint tick -- CAPPED at 30 Hz, INDEPENDENT of the eval rate. Display widgets (readout/gauge) push
    // their value here only when it CHANGED; they never repaint per eval (GATE PERF / GATE RATE).
    auto* uiRefreshTimer = new QTimer(this);
    connect(uiRefreshTimer, &QTimer::timeout, this, [graphModel]() { krs::nodes::refreshGraphUi(*graphModel); });
    uiRefreshTimer->start(33);   // ~30 Hz UI cap

    // DEFAULT DEMO = A REAL NODE GRAPH. Spawn time_source -> gen_sine -> physics_articulation_drive on the
    // actual editor canvas; it drives the live FANUC J1 via the command bus + SimulationController bridge.
    // These nodes ARE the robot's driver (inspectable + editable), not a decorative copy -- editing the
    // sine's amp/freq changes the real motion (GATE DEMO-GRAPH). This is the SINGLE path to joint motion.
    if (bootFanuc) {
        krs::nodes::BootGraphHandles bg = krs::nodes::spawnDefaultRobotGraph(*graphModel, m_scene.get(),
                                                                             /*joint*/ 0, /*freqHz*/ 0.2, /*ampRad*/ 0.5);
        if (bg.ok) qInfo() << "[FANUC] default robot graph spawned (time->sine->drive J1) -- the node editor drives the arm";
        else       qWarning() << "[FANUC] default robot graph spawn FAILED";
    }

    // Phase 4 demo (KRS_ORB_DEMO): instance a velocity_probe_orb node, which (via the nodeCreated hook)
    // spawns its bound glass-sphere probe. Pair with KRS_FLUID_DEMO to watch the probe report the fluid's
    // speed -- and drop to ~0 when dragged off the stream. KRS_ORB_DEMO=N spawns N orbs in N colours.
    if (qEnvironmentVariableIsSet("KRS_ORB_DEMO")) {
        const int nOrbs = std::max(1, qEnvironmentVariableIntValue("KRS_ORB_DEMO"));
        for (int i = 0; i < nOrbs; ++i) {
            const QtNodes::NodeId id = graphModel->addNode(QStringLiteral("velocity_probe_orb"));
            // spread the demo orbs along x and drop them into the basin water (y in 0.08..0.52 under
            // KRS_FLUID_DEMO) so they immediately sample the moving fluid; they do not overlap.
            if (entt::entity orb = krs::orb::findOrbForNode(m_scene->getRegistry(), std::uint64_t(id)); orb != entt::null)
                if (auto* xf = m_scene->getRegistry().try_get<TransformComponent>(orb))
                    xf->translation = glm::vec3(-0.6f + 0.6f * float(i), 0.18f, 0.0f);
            qInfo() << "[orb] KRS_ORB_DEMO instanced velocity_probe_orb node" << id;
        }
        // auto-start so the probes immediately sample the moving fluid (else the sim is paused at boot).
        if (m_simulation) m_simulation->play();
    }

    connect(m_dockManager, &ads::CDockManager::dockWidgetRemoved, this, &MainWindow::syncViewportManagerPopup);

    // ===================================================================
    // --- Menu Toggle Connections ---
    // ===================================================================
    connect(m_fixedTopToolbar, &StaticToolbar::flowMenuToggled,
        this, [this](bool on) { handleMenuToggle(MenuType::FlowVisualizer, on); });
    connect(m_fixedTopToolbar, &StaticToolbar::realSenseMenuToggled,
        this, [this](bool on) { handleMenuToggle(MenuType::RealSense, on); });
    connect(m_fixedTopToolbar, &StaticToolbar::databaseMenuToggled,
        this, [this](bool on) { handleMenuToggle(MenuType::Database, on); });
    connect(m_fixedTopToolbar, &StaticToolbar::gridMenuToggled, // Connect the new signal for the grid properties
        this, [this](bool on) { handleMenuToggle(MenuType::GridProperties, on); });
    connect(m_fixedTopToolbar, &StaticToolbar::objectPropertiesMenuToggled,
        this, [this](bool on) { handleMenuToggle(MenuType::ObjectProperties, on); });
    // Generic always-docked panels (Physics, Lighting, Textures, Outliner, Robot Builder,
    // Assets, Diagnostics, Fluid, Gas, Robot View, Node Editor): toggle their dock by title.
    connect(m_fixedTopToolbar, &StaticToolbar::panelToggled, this,
        [this](const QString& title, bool on) {
            auto it = m_panelDocks.find(title);
            if (it != m_panelDocks.end() && it.value()) it.value()->toggleView(on);
        });
    // Theme selector: persist via settings; the settings applier drives applyTheme().
    connect(m_fixedTopToolbar, &StaticToolbar::themeSelected, this,
        [](const QString& theme) { krs::SettingsManager::instance().set(QStringLiteral("ui/theme"), theme); });

    // --- Simulation lifecycle (play/pause checkable button + stop + step) ---
    connect(m_fixedTopToolbar, &StaticToolbar::simulationPlayPauseToggled, this, [this](bool play) {
        if (!m_simulation) return;
        if (play) m_simulation->play();
        else      m_simulation->pause();
    });
    connect(m_fixedTopToolbar, &StaticToolbar::simulationResetClicked, this, [this]() {
        if (m_simulation) m_simulation->stop();
    });
    connect(m_fixedTopToolbar, &StaticToolbar::simulationStepClicked, this, [this]() {
        if (m_simulation) m_simulation->singleStep();
    });
    connect(m_simulation.get(), &SimulationController::stateChanged, this, [this](SimulationState s) {
        m_fixedTopToolbar->setSimulationPlaying(s == SimulationState::Playing);
        if (m_renderingSystem) {
            m_renderingSystem->setSimulationPlaying(s == SimulationState::Playing);
            if (s == SimulationState::Stopped)
                m_renderingSystem->resetFluidSimulation();
        }
    });

    //========================= Start the SLAM Manager =========================
    m_slamManager = new SlamManager(this);
    m_realSenseManager = std::make_unique<RealSenseManager>();
    m_slamManager->setRenderingSystem(m_renderingSystem.get());
    m_rsPollTimer = new QTimer(this);
    m_rsPollTimer->setInterval(33);
    m_pointCloud = std::make_unique<rs2::pointcloud>();

    connect(m_rsPollTimer, &QTimer::timeout, this, [this]() {
        rs2::frameset frames;
        if (m_realSenseManager->pollFrames(frames)) {
            auto depth = frames.get_depth_frame();
            auto color = frames.get_color_frame();
            if (depth && color) {
                m_pointCloud->map_to(color);
                rs2::points points = m_pointCloud->calculate(depth);
                m_slamManager->onPointCloudReady(points, color);
            }
        }
        });

    // NOTE: The connections to the RealSenseMenu will be made inside `showMenu` when it's created.
    m_slamManager->start();

    // --- 5. FINAL, ROBUST INITIALIZATION AND RENDER LOOP START ---
    auto engineFrame = [this]() {
        // Frame-rate cap (Settings: perf/maxFps). Render only once the free-running
        // clock passes the next deadline, then advance the deadline by exactly
        // m_minFrameMs so the *average* render rate equals the target regardless of
        // how coarse the driving ticks are (frameSwapped + fallback timer). Carrying
        // the deadline (vs. zeroing a clock) is what avoids the downward drift.
        // 0 (m_minFrameMs==0) = uncapped (vsync governs).
        if (m_minFrameMs > 0.0) {
            if (!m_frameClock.isValid()) m_frameClock.start();
            const double now = m_frameClock.nsecsElapsed() * 1e-6;
            if (now < m_nextRenderMs) return;                 // too soon -> skip
            m_nextRenderMs += m_minFrameMs;                   // schedule next (carry)
            if (m_nextRenderMs < now) m_nextRenderMs = now + m_minFrameMs; // resync after a stall
        }
        // Step the simulation (fixed-timestep physics), then render all
        // viewports on the engine's own GL context and schedule repaints.
        // [PERF] Split the frame period into physics vs render so the "CPU frame"
        // wall-time can be attributed. Opt-in via KRS_PERF=1; qDebug output lands
        // in Qt Creator/VS "Application Output" (or the console). Zero cost when off.
        static const bool krsPerf = qEnvironmentVariableIsSet("KRS_PERF");
        QElapsedTimer _ft;
        if (krsPerf) _ft.start();
        if (m_simulation) {
            m_simulation->tick();
        }
        const double _tickMs = krsPerf ? _ft.nsecsElapsed() * 1e-6 : 0.0;
        if (krsPerf) _ft.restart();
        if (m_renderingSystem) {
            m_renderingSystem->renderAllViewports();
        }
        if (krsPerf) {
            const double _renderMs = _ft.nsecsElapsed() * 1e-6;
            static double accTick = 0.0, accRender = 0.0;
            static int n = 0;
            static QElapsedTimer _wall; if (!_wall.isValid()) _wall.start();
            accTick += _tickMs;
            accRender += _renderMs;
            if (++n >= 60) {
                // True render rate = frames / wall-time over the window. Unlike
                // RenderingSystem::getFPS() this counts ONLY actual renders, so it
                // reflects the perf/maxFps cap (getFPS conflates the 60Hz logic loop).
                const double _wallMs = _wall.nsecsElapsed() * 1e-6;
                const double _renderFps = (_wallMs > 0.0) ? 1000.0 * n / _wallMs : 0.0;
                _wall.restart();
                // Pull the app's own numbers so the log shows what the user sees,
                // plus GPU execution time (gpuTotal includes the GPU fluid compute
                // via fluidSimMs) to localize cost: GPU-bound vs CPU-bound vs a
                // present/vsync stall (period >> cpuWork+gpuTotal).
                const double _fps      = m_renderingSystem ? m_renderingSystem->getFPS() : 0.0;
                const double _period   = m_renderingSystem ? m_renderingSystem->getFrameTime() : 0.0; // "CPU frame" = inter-frame period
                const double _gpuTotal = m_renderingSystem ? m_renderingSystem->getGpuTimings().totalMs() : 0.0;
                qDebug("[PERF] avg/%d: renderFPS=%.1f (cap minFrameMs=%.1f)  loopFPS=%.1f  period=%.1f ms  gpuTotal=%.2f ms  ||  tick=%.2f ms  render=%.2f ms  cpuWork=%.2f ms",
                       n, _renderFps, m_minFrameMs, _fps, _period, _gpuTotal, accTick / n, accRender / n, (accTick + accRender) / n);
                accTick = accRender = 0.0;
                n = 0;
            }
        }
    };
    m_masterRenderTimer = new QTimer(this);
    connect(m_masterRenderTimer, &QTimer::timeout, this, engineFrame);

    m_renderingSystem->initialize(m_scene.get());

    // --- Settings hot-swap routing ------------------------------------------
    // Route every persisted/changed setting to its owning subsystem so edits in
    // the Settings dialog apply live (the render loop shows them next frame).
    // applySetting is also invoked for every key now so persisted values reach
    // the subsystems before the first frame. Extend the dispatch per tier.
    {
        auto& smgr = krs::SettingsManager::instance();
        auto applySetting = [this](const QString& key, const QVariant& v) {
            RenderingSystem* rs = m_renderingSystem.get();
            if (!rs) return;
            if      (key == QLatin1String("render/iblNits"))          rs->setIblIntensity(v.toFloat());
            else if (key == QLatin1String("render/sunLux"))           rs->setSunIntensity(v.toFloat());
            else if (key == QLatin1String("render/exposureEV"))       rs->setExposureEV(v.toFloat());
            else if (key == QLatin1String("render/sunColor"))         { QColor c = v.value<QColor>(); rs->setSunColor(glm::vec3(float(c.redF()), float(c.greenF()), float(c.blueF()))); }
            else if (key == QLatin1String("render/sunDirection"))     rs->setSunDirection(krs::SettingsManager::vec3FromVariant(v));
            else if (key == QLatin1String("render/tonemapExposure"))  rs->setTonemapExposure(v.toFloat());
            else if (key == QLatin1String("render/specFireflyClamp")) rs->setSpecFireflyClamp(v.toFloat());
            else if (key == QLatin1String("render/hdrEnabled"))       rs->setHdrEnabled(v.toBool());
            else if (key == QLatin1String("ui/theme"))                applyTheme(v.toString());
            // Camera prefs are global statics (read by every viewport's camera each frame),
            // so a single set call applies live everywhere — no per-viewport iteration.
            else if (key == QLatin1String("viewport/cameraFovDeg"))     Camera::setFovDeg(v.toFloat());
            else if (key == QLatin1String("viewport/cameraNearPlane"))  Camera::setNearClip(v.toFloat());
            else if (key == QLatin1String("viewport/cameraFarPlane"))   Camera::setFarClip(v.toFloat());
            else if (key == QLatin1String("viewport/orbitSensitivity")) Camera::setOrbitSensitivity(v.toFloat());
            else if (key == QLatin1String("viewport/zoomFactor"))       Camera::setZoomFactor(v.toFloat());
            else if (key == QLatin1String("viewport/lookSmoothing"))    Camera::setLookSmoothing(v.toFloat());
            else if (key == QLatin1String("viewport/invertLookY"))      Camera::setInvertLookY(v.toBool());
            else if (key == QLatin1String("viewport/defaultNavMode"))   Camera::setDefaultNavMode(v.toString() == QLatin1String("FLY") ? Camera::NavMode::FLY : Camera::NavMode::ORBIT);
            // --- Physics (live knobs) -------------------------------------------
            // gravity: live AND baked into the next world build (setSceneGravity
            // hits the running scene; buildPhysicsWorld re-reads the setting).
            else if (key == QLatin1String("physics/gravity")) { if (m_simulation) m_simulation->setSceneGravity(0.0f, -v.toFloat(), 0.0f); }
            // physics rate: SimulationController::tick() reads the static each frame.
            else if (key == QLatin1String("physics/simRateHz"))         SimulationController::setSimRateHz(v.toInt());
            // The remaining physics/* keys (solver, CCD, stabilization, PCM, GPU
            // gate, bounce, weld) bake at createScene()/cook time -- buildPhysicsWorld
            // and the cooking service read SettingsManager directly, so they apply
            // on the next Play / mesh import. No live applier needed.
            // --- Performance ----------------------------------------------------
            else if (key == QLatin1String("perf/maxFps")) {
                const int fps = v.toInt();
                m_minFrameMs   = (fps > 0) ? 1000.0 / double(fps) : 0.0;
                m_nextRenderMs = 0.0;   // resync the deadline on change (gate self-corrects)
                // Pace the fallback timer at ~2x the cap so the carry-gate has fine
                // granularity even when the viewport isn't presenting (frameSwapped
                // silent); never slower than the 33 ms idle fallback.
                if (m_masterRenderTimer)
                    m_masterRenderTimer->setInterval(fps > 0 ? std::max(1, std::min(33, int(500.0 / fps))) : 33);
            }
            // perf/vsync is restart-only (applied in main.cpp at boot) -- no live action.
            else if (key.startsWith(QLatin1String("scene/")) && m_scene) {
                auto& sp = m_scene->getRegistry().ctx().get<SceneProperties>();
                if      (key == QLatin1String("scene/backgroundColor"))     { QColor c = v.value<QColor>(); sp.backgroundColor = glm::vec4(float(c.redF()), float(c.greenF()), float(c.blueF()), 1.0f); }
                else if (key == QLatin1String("scene/fogEnabled"))          sp.fogEnabled = v.toBool();
                else if (key == QLatin1String("scene/fogColor"))            { QColor c = v.value<QColor>(); sp.fogColor = glm::vec3(float(c.redF()), float(c.greenF()), float(c.blueF())); }
                else if (key == QLatin1String("scene/fogStartDistance"))    sp.fogStartDistance = v.toFloat();
                else if (key == QLatin1String("scene/fogEndDistance"))      sp.fogEndDistance = v.toFloat();
                else if (key == QLatin1String("scene/showCollisionShapes")) sp.showCollisionShapes = v.toBool();
            }
        };
        connect(&smgr, &krs::SettingsManager::changed, this, applySetting);
        for (const auto& d : smgr.registry()) applySetting(d.key, smgr.value(d.key));
    }

    // Drive engine frames from the primary viewport's vsync (frameSwapped):
    // a free-running 8 ms timer beat against the 60 Hz display, so animated
    // motion (the orbiting key light) alternated 1-and-2 engine steps per
    // presented frame — visible judder. The timer stays as a low-rate
    // fallback so the engine keeps ticking when no viewport presents
    // (hidden/minimised window, headless test runs).
    connect(viewport1, &QOpenGLWidget::frameSwapped, this, engineFrame);
    m_masterRenderTimer->setTimerType(Qt::PreciseTimer);
    // Honor the fallback interval the perf/maxFps applier set during applyAll
    // (33 ms when uncapped); only fall back to 33 if nothing set it.
    if (m_masterRenderTimer->interval() <= 0) m_masterRenderTimer->setInterval(33);
    m_masterRenderTimer->start();

    // Dev aid: KRS_GRAB=<path.png> software-renders the whole widget tree to
    // a file 8s after boot — screen captures lie under DPI virtualization,
    // this never does. KRS_CAM="px,py,pz,tx,ty,tz" pins the primary camera
    // (applied at +1s and again right before the grab) so verification
    // screenshots frame identically run to run.
    if (qEnvironmentVariableIsSet("KRS_GRAB") || qEnvironmentVariableIsSet("KRS_CAM")) {
        auto applyCamEnv = [this]() {
            const QStringList v = qEnvironmentVariable("KRS_CAM").split(',');
            if (v.size() != 6) return;
            if (ViewportWidget* vp = primaryViewport()) {
                const glm::vec3 pos(v[0].toFloat(), v[1].toFloat(), v[2].toFloat());
                const glm::vec3 tgt(v[3].toFloat(), v[4].toFloat(), v[5].toFloat());
                vp->getCamera().forceRecalculateView(pos, tgt, glm::length(pos - tgt));
            }
        };
        // KRS_GRAB_DELAY=<ms> moves the grab (default 8s) — e.g. 30000 to
        // capture the SETTLED state of a fluid scene instead of the splash.
        int grabDelay = qEnvironmentVariable("KRS_GRAB_DELAY").toInt();
        if (grabDelay <= 0) grabDelay = 8000;
        QTimer::singleShot(1000, this, applyCamEnv);
        // Re-pin half a second before the grab: the engine needs a frame or
        // two to render with the new camera before the widget tree is read.
        QTimer::singleShot(grabDelay - 500, this, applyCamEnv);
        if (qEnvironmentVariableIsSet("KRS_GRAB")) {
            QTimer::singleShot(grabDelay, this, [this]() {
                const QString path = qEnvironmentVariable("KRS_GRAB");
                this->grab().save(path);
                if (ViewportWidget* mvp = primaryViewport())   // clean GL of the MAIN viewport
                    mvp->grab().save(path + QStringLiteral(".vp.png"));
                qInfo() << "[UI] window grabbed to" << path;
            });
        }
    }

    // Test hook: KRS_TEXTEST=<outDir> reproduces "apply texture -> black": applies a
    // PARALLAX pack (lava, has height map) to the Ground.Slab and a PLAIN pack (no height
    // map) to the aluminium block, via the REAL applyPackTags path, then grabs + logs tags.
    if (qEnvironmentVariableIsSet("KRS_TEXTEST")) {
        const QString outDir = qEnvironmentVariable("KRS_TEXTEST");
        QTimer::singleShot(3000, this, [this]() {
            auto& reg = m_scene->getRegistry();
            auto findTag = [&](const char* t) {
                for (auto e : reg.view<TagComponent>()) if (reg.get<TagComponent>(e).tag == t) return e;
                return entt::entity(entt::null);
            };
            auto applyPack = [&](entt::entity e, const std::string& dir, bool hasHeight, float tiling) {
                if (e == entt::null) return;
                reg.remove<TriPlanarMaterialTag>(e); reg.remove<ParallaxMaterialTag>(e);
                reg.emplace_or_replace<MaterialDirectoryTag>(e, dir);
                auto& req = reg.emplace_or_replace<MaterialReloadRequest>(e);
                req.tilingOverride = tiling;
                // Exaggerated height so the parallax direction is clearly visible for the audit
                // (env KRS_POM_HEIGHT overrides; default 0.2).
                { bool ok=false; float hs = qEnvironmentVariable("KRS_POM_HEIGHT").toFloat(&ok);
                  req.heightScaleOverride = hasHeight ? (ok ? hs : 0.2f) : -1.0f; }
                krs::material::applyPackTags(reg, e, hasHeight);
                qInfo() << "[TEXTEST] applied" << QString::fromStdString(dir) << "-> UV="
                        << reg.all_of<UVTexturedMaterialTag>(e) << "TriPlanar=" << reg.all_of<TriPlanarMaterialTag>(e)
                        << "Parallax=" << reg.all_of<ParallaxMaterialTag>(e);
            };
            applyPack(findTag("Ground.Slab"), "assets/Textures/ground-bl/lava-and-rock-bl", true, 0.5f);   // parallax debug: rock=high, lava=low
            applyPack(findTag("Aluminium Block (FEM)"), "assets/Textures/bonus-bl/boulder1", false, 2.0f);       // PLAIN (no height)
        });
        QTimer::singleShot(9000, this, [this, outDir]() {
            if (ViewportWidget* mvp = primaryViewport()) mvp->grab().save(outDir + QStringLiteral("/textest.png"));
            qInfo() << "[TEXTEST] grabbed to" << outDir;
        });
    }

    // Test hook: KRS_LIGHTUI_SELFTEST=<outDir> opens the Object Properties dock, spawns +
    // selects a point light, brings the Light tab forward, grabs BEFORE, then drives the REAL
    // Light controls (colour via Kelvin + intensity) and grabs AFTER -- so a pixel diff proves
    // the per-object lighting menu hot-updates the live scene.
    if (qEnvironmentVariableIsSet("KRS_LIGHTUI_SELFTEST")) {
        const QString outDir = qEnvironmentVariable("KRS_LIGHTUI_SELFTEST");
        auto lightPanel = [this]() -> ObjectPropertiesWidget* {
            auto it = m_menus.find(MenuType::ObjectProperties);
            if (it != m_menus.end())
                return dynamic_cast<ObjectPropertiesWidget*>(it->menu->widget());
            return nullptr;
        };
        QTimer::singleShot(2000, this, [this]() { handleMenuToggle(MenuType::ObjectProperties, true); });
        QTimer::singleShot(3000, this, [this, lightPanel]() {
            addLightFromMenu(int(LightComponent::Type::Point), glm::vec3(1.6f, 1.4f, 0.0f));
            if (auto* w = lightPanel()) w->selfTestSelectLightTab();
        });
        QTimer::singleShot(8000, this, [this, outDir]() {
            this->grab().save(outDir + QStringLiteral("/lightui_before.png"));
            qInfo() << "[LIGHTUI] before grabbed";
        });
        QTimer::singleShot(8500, this, [this, lightPanel]() {
            if (auto* w = lightPanel()) w->selfTestNudgeLight();
        });
        QTimer::singleShot(9500, this, [this, outDir]() {
            this->grab().save(outDir + QStringLiteral("/lightui_after.png"));
            qInfo() << "[LIGHTUI] after grabbed";
        });
        // Phase B: prove emissive HOT-UPDATE on a TEXTURED body (the concrete floor uses the
        // triplanar shader): select it, grab BEFORE, run "Add Light Emitter" + crank a cyan
        // glow via the menu path, grab AFTER. A pixel diff proves textured emissive hot-updates.
        QTimer::singleShot(11000, this, [this]() {
            auto& reg = m_scene->getRegistry();
            entt::entity floor = entt::null;
            for (auto e : reg.view<TagComponent>())
                if (reg.get<TagComponent>(e).tag == std::string("Ground.Slab")) { floor = e; break; }
            if (floor == entt::null) return;
            for (auto eSel : reg.view<SelectedComponent>()) reg.remove<SelectedComponent>(eSel);
            reg.emplace<SelectedComponent>(floor);
            refreshGizmoAndProperties();   // -> onSelectionChanged -> setEntity(floor)
        });
        QTimer::singleShot(12500, this, [this, outDir]() {
            this->grab().save(outDir + QStringLiteral("/tex_before.png"));
        });
        QTimer::singleShot(13000, this, [this, lightPanel]() {
            if (auto* w = lightPanel()) w->selfTestAddAndGlowEmitter();
        });
        QTimer::singleShot(14000, this, [this, outDir]() {
            this->grab().save(outDir + QStringLiteral("/tex_after.png"));
            qInfo() << "[LIGHTUI] textured emissive after grabbed";
        });
    }

    // Test hook: KRS_TOOLBAR_SELFTEST=<outDir> simulates clicking each panel toggle button
    // (hide all, then show all) and records each dock's open/closed state + grabs, to verify
    // the toolbar buttons actually summon/dismiss their docks.
    if (qEnvironmentVariableIsSet("KRS_TOOLBAR_SELFTEST")) {
        const QString outDir = qEnvironmentVariable("KRS_TOOLBAR_SELFTEST");
        const QStringList panels = { QStringLiteral("Physics"), QStringLiteral("Lighting"),
            QStringLiteral("Textures"), QStringLiteral("Outliner"), QStringLiteral("Robot Builder"),
            QStringLiteral("Assets"), QStringLiteral("Diagnostics"), QStringLiteral("Node Editor"),
            QStringLiteral("Fluid"), QStringLiteral("Gas"), QStringLiteral("Robot View") };
        auto record = [this, panels, outDir](const QString& phase) {
            QFile f(outDir + QStringLiteral("/toolbar_result.txt"));
            if (f.open(QIODevice::Append | QIODevice::Text)) {
                QTextStream ts(&f); ts << phase << ": ";
                for (const QString& p : panels) {
                    auto it = m_panelDocks.find(p);
                    const bool closed = (it != m_panelDocks.end() && it.value() && it.value()->isClosed());
                    ts << p << "=" << (closed ? "CLOSED" : "OPEN") << "; ";
                }
                ts << "\n";
            }
        };
        QTimer::singleShot(7000, this, [this, outDir, record]() { record(QStringLiteral("initial")); this->grab().save(outDir + QStringLiteral("/panels_initial.png")); });
        QTimer::singleShot(7600, this, [this, panels]() { for (const QString& p : panels) m_fixedTopToolbar->selfTestClickPanel(p); }); // hide
        QTimer::singleShot(8400, this, [this, outDir, record]() { record(QStringLiteral("after_hide")); this->grab().save(outDir + QStringLiteral("/panels_hidden.png")); });
        QTimer::singleShot(9000, this, [this, panels]() { for (const QString& p : panels) m_fixedTopToolbar->selfTestClickPanel(p); }); // show
        QTimer::singleShot(9800, this, [this, outDir, record]() { record(QStringLiteral("after_show")); this->grab().save(outDir + QStringLiteral("/panels_shown.png")); });
        // Theme switch verification.
        QTimer::singleShot(10600, this, [this]() { applyTheme(QStringLiteral("light")); });
        QTimer::singleShot(11400, this, [this, outDir]() { this->grab().save(outDir + QStringLiteral("/theme_light.png")); });
        QTimer::singleShot(12000, this, [this]() { applyTheme(QStringLiteral("dark")); });
        QTimer::singleShot(12800, this, [this, outDir]() { this->grab().save(outDir + QStringLiteral("/theme_dark.png")); qInfo() << "[TOOLBAR] selftest done"; });
    }

    // Test hook: KRS_ROBOTVIEW_SELFTEST=<outDir> raises the Robot View dock (so its
    // QOpenGLWidget actually initializes + renders -- a hidden tab never does, which
    // is why it read "FPS 0.0"), lets it draw several frames, then grabs the widget
    // (GL grey-room background + the orbit-speed slider child) and the whole window,
    // and writes pixel samples to robotview_result.txt so the grey room + slider can
    // be verified by MEASUREMENT, not eyeballing.
    if (qEnvironmentVariableIsSet("KRS_ROBOTVIEW_SELFTEST")) {
        const QString outDir = qEnvironmentVariable("KRS_ROBOTVIEW_SELFTEST");
        // 1) Show + raise the Robot View dock (and the Outliner, to capture its tree).
        QTimer::singleShot(2500, this, [this]() {
            for (const QString& name : { QStringLiteral("Robot View"), QStringLiteral("Outliner") }) {
                auto it = m_panelDocks.find(name);
                if (it != m_panelDocks.end() && it.value()) {
                    it.value()->toggleView(true);
                    it.value()->setAsCurrentTab();
                    it.value()->raise();
                }
            }
        });
        // 2) Exercise the slider setter (proves the control + value path) at a known speed.
        QTimer::singleShot(5000, this, [this]() {
            if (m_robotViewport) m_robotViewport->setOrbitSpeed(0.75f);
        });
        // 2a2) Load the demo robot as a SECOND robot (robotId 1) to exercise multi-robot.
        QTimer::singleShot(4500, this, [this]() {
            RobotBuilderPanel* rbp = m_robotBuilderPanel ? m_robotBuilderPanel
                                                         : findChild<RobotBuilderPanel*>();
            QPushButton* btn = rbp ? rbp->findChild<QPushButton*>(QStringLiteral("rbLoadDemoButton")) : nullptr;
            qInfo() << "[MULTIROBOT] load-demo: panel=" << (rbp != nullptr) << "button=" << (btn != nullptr);
            if (btn) btn->click();
        });
        // 2a3) Editable FANUC: bind the builder to the boot FANUC (robotId 0) and edit a
        //      joint LIMIT through the panel; confirm the live model reflects the edit.
        QTimer::singleShot(6500, this, [this]() {
            if (!m_robotBuilderPanel) { qInfo() << "[EDITFANUC] no builder panel"; return; }
            m_robotBuilderPanel->editRobot(0);   // bind to the FANUC (the wired outliner path)
            auto* jl = m_robotBuilderPanel->findChild<QListWidget*>(QStringLiteral("rbJointsList"));
            auto* dofSpin = m_robotBuilderPanel->findChild<QSpinBox*>(QStringLiteral("rbLimitDofSpin"));
            auto* loSpin  = m_robotBuilderPanel->findChild<QDoubleSpinBox*>(QStringLiteral("rbLimitLoSpin"));
            auto* hiSpin  = m_robotBuilderPanel->findChild<QDoubleSpinBox*>(QStringLiteral("rbLimitHiSpin"));
            auto* applyBtn = m_robotBuilderPanel->findChild<QPushButton*>(QStringLiteral("rbApplyLimitButton"));
            const int jointRows = jl ? jl->count() : -1;
            auto& reg = m_scene->getRegistry();
            auto* rr  = reg.ctx().find<krs::robot::RobotRegistry>();
            krs::robot::LiveRobot* lr = rr ? rr->get(0) : nullptr;
            double beforeLo = -99, beforeHi = -99, afterLo = -99, afterHi = -99;
            int memberArr = -1;
            if (lr) for (int ji = 0; ji < int(lr->model.joints.size()); ++ji)
                if (lr->model.joints[ji].member && lr->model.joints[ji].type != krs::dyn::JType::Fixed) { memberArr = ji; break; }
            if (lr && memberArr >= 0) { beforeLo = lr->model.joints[memberArr].qLower; beforeHi = lr->model.joints[memberArr].qUpper; }
            // Bug 4: selecting a joint row must update the DOF field (was not plumbed).
            int dofAfterSelect = -1;
            if (jl && dofSpin && jl->count() > 2) { jl->setCurrentRow(2); dofAfterSelect = dofSpin->value(); }
            // Bug 2 diagnostic: entities driving each link (0 => that joint moves NO geometry).
            QString linkCounts;
            if (lr) for (int k = 0; k < int(lr->linkEntities.size()); ++k)
                linkCounts += QString::number(int(lr->linkEntities[k].size())) + " ";
            qInfo() << "[EDITFANUC] dofFieldFollowsSelect(row2->)" << dofAfterSelect
                    << " linkEntityCounts=[" << linkCounts.trimmed() << "] (0 => joint drives nothing)";
            if (dofSpin && loSpin && hiSpin && applyBtn) {
                dofSpin->setValue(0); loSpin->setValue(-0.5); hiSpin->setValue(0.5);
                applyBtn->click();   // -> onApplyLimit -> onApplyLimitLive -> writes lr.model + rebuild
            }
            if (lr && memberArr >= 0) { afterLo = lr->model.joints[memberArr].qLower; afterHi = lr->model.joints[memberArr].qUpper; }
            qInfo() << "[EDITFANUC] jointRows=" << jointRows << "memberArr=" << memberArr
                    << "before=[" << beforeLo << "," << beforeHi << "] after=[" << afterLo << "," << afterHi << "]"
                    << "editApplied=" << (afterLo > -0.51 && afterLo < -0.49 && afterHi > 0.49 && afterHi < 0.51);
        });
        // 2b) Select a robot body in the MAIN scene so the grab shows it gold-outlined in
        //     BOTH viewports (cross-viewport selection).
        QTimer::singleShot(5500, this, [this]() {
            auto& reg = m_scene->getRegistry();
            entt::entity pick = entt::null; float bestY = -1e9f;
            for (auto e : reg.view<RobotSubcomponentComponent, RenderableMeshComponent, TransformComponent>())
                if (reg.get<RobotSubcomponentComponent>(e).robotId == 0) {
                    const float y = reg.get<TransformComponent>(e).translation.y;  // highest = most visible
                    if (y > bestY) { bestY = y; pick = e; }
                }
            if (pick != entt::null) {
                for (auto s : reg.view<SelectedComponent>()) reg.remove<SelectedComponent>(s);
                reg.emplace_or_replace<SelectedComponent>(pick);
                qInfo() << "[XSEL] selected main robot body e=" << std::uint32_t(pick) << "y=" << bestY;
            }
        });
        // 3) After GL init + many frames, grab + sample pixels.
        QTimer::singleShot(8000, this, [this, outDir]() {
            if (!m_robotViewport) { qInfo() << "[ROBOTVIEW] no viewport"; return; }
            const QPixmap pm = m_robotViewport->grab();   // GL content + slider child
            pm.save(outDir + QStringLiteral("/robotview_widget.png"));
            this->grab().save(outDir + QStringLiteral("/robotview_window.png"));
            if (ViewportWidget* mvp = primaryViewport())  // clean high-res MAIN viewport
                mvp->grab().save(outDir + QStringLiteral("/mainvp.png"));
            if (OutlinerWidget* ow = findChild<OutlinerWidget*>())  // the Robot->bodies tree
                ow->grab().save(outDir + QStringLiteral("/outliner.png"));
            if (m_robotViewport)
                qInfo() << "[XSEL] view-twins selected (cross-viewport):" << m_robotViewport->viewSelectedCount();
            // Multi-robot check: count distinct RobotRootComponents (FANUC + any loaded demo).
            { auto& r = m_scene->getRegistry(); int roots = 0, r0 = 0, r1 = 0, rOther = 0;
              for (auto e : r.view<RobotRootComponent>()) { (void)e; ++roots; }
              for (auto e : r.view<RobotSubcomponentComponent>()) {
                  const int id = r.get<RobotSubcomponentComponent>(e).robotId;
                  if (id == 0) ++r0; else if (id == 1) ++r1; else ++rOther; }
              qInfo() << "[MULTIROBOT] roots:" << roots << " bodies robotId0=" << r0 << "robotId1=" << r1 << "other=" << rOther; }
            const QImage wi = pm.toImage();
            const int w = wi.width(), h = wi.height();
            QFile f(outDir + QStringLiteral("/robotview_result.txt"));
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream ts(&f);
                auto px = [&](int x, int y) {
                    const QColor c = wi.pixelColor(qBound(0, x, w - 1), qBound(0, y, h - 1));
                    return QString("(%1,%2,%3)").arg(c.red()).arg(c.green()).arg(c.blue());
                };
                ts << "widget_size=" << w << "x" << h << "\n";
                ts << "center="   << px(w / 2, h / 2)       << "\n";   // grey room or robot
                ts << "topleft="  << px(w / 8, h / 8)       << "\n";   // upper room (lighter)
                ts << "botleft="  << px(w / 8, 7 * h / 8)   << "\n";   // lower room (darker)
                ts << "topright=" << px(7 * w / 8, h / 8)   << "\n";
                // Orbit slider lives bottom-right: scan that band for the gold handle/groove.
                int goldHits = 0; int sampled = 0;
                for (int y = h - 30; y < h - 4 && y > 0; ++y)
                    for (int x = w - 135; x < w - 8 && x > 0; ++x) {
                        const QColor c = wi.pixelColor(x, y); ++sampled;
                        if (c.red() > 150 && c.green() > 110 && c.blue() < 110
                            && c.red() > c.blue() + 50) ++goldHits;   // gold-ish
                    }
                ts << "slider_band_sampled=" << sampled << " goldHits=" << goldHits << "\n";
                ts << "orbitSpeed=" << m_robotViewport->orbitSpeed() << "\n";
                ts << "sliderPresent=" << (m_robotViewport->findChild<QSlider*>() ? "yes" : "no") << "\n";
            }
            qInfo() << "[ROBOTVIEW] selftest grabbed to" << outDir;
        });
    }

    // Debug: KRS_HIDE_TAG=<substr> hides (HiddenComponent) every entity whose tag contains
    // the substring at +5s -- to bisect which object draws the reported black plane.
    if (qEnvironmentVariableIsSet("KRS_HIDE_TAG")) {
        const QString needle = qEnvironmentVariable("KRS_HIDE_TAG");
        QTimer::singleShot(5000, this, [this, needle]() {
            auto& reg = m_scene->getRegistry();
            int hidden = 0;
            // OpaquePass ignores HiddenComponent, so MOVE matched entities far below the
            // world to remove them from the render (decisive bisect of who draws what).
            for (auto e : reg.view<TagComponent, TransformComponent>()) {
                if (QString::fromStdString(reg.get<TagComponent>(e).tag).contains(needle, Qt::CaseInsensitive)) {
                    reg.get<TransformComponent>(e).translation.y -= 100000.0f;
                    reg.emplace_or_replace<HiddenComponent>(e); ++hidden;
                }
            }
            qInfo() << "[KRS_HIDE_TAG] moved/hid" << hidden << "entities matching" << needle;
        });
    }

    // Debug: KRS_SCENE_DUMP=1 logs every renderable main-scene entity's tag/scale/world
    // AABB extent at +6s, flagging OVERSIZED geometry + all lights -- to find the "black
    // plane converging at the focal point" reported in the main viewport.
    if (qEnvironmentVariableIsSet("KRS_SCENE_DUMP")) {
        QTimer::singleShot(6000, this, [this]() {
            auto& reg = m_scene->getRegistry();
            ViewportWidget::propagateTransforms(reg);
            qInfo() << "===== KRS_SCENE_DUMP (WORLD-space extents) =====";
            int n = 0, flagged = 0;
            for (auto e : reg.view<RenderableMeshComponent, TransformComponent>()) {
                ++n;
                const auto& m = reg.get<RenderableMeshComponent>(e);
                glm::mat4 W(1.0f);
                if (auto* wt = reg.try_get<WorldTransformComponent>(e)) W = wt->matrix;
                else W = reg.get<TransformComponent>(e).getTransform();
                glm::vec3 mn(1e30f), mx(-1e30f);
                for (int c = 0; c < 8; ++c) {
                    const glm::vec3 corner(
                        (c & 1) ? m.aabbMax.x : m.aabbMin.x,
                        (c & 2) ? m.aabbMax.y : m.aabbMin.y,
                        (c & 4) ? m.aabbMax.z : m.aabbMin.z);
                    const glm::vec3 wp = glm::vec3(W * glm::vec4(corner, 1.0f));
                    mn = glm::min(mn, wp); mx = glm::max(mx, wp);
                }
                const glm::vec3 ext = mx - mn;
                const float maxExt = std::max({ ext.x, ext.y, ext.z });
                const auto* tag = reg.try_get<TagComponent>(e);
                const QString name = tag ? QString::fromStdString(tag->tag) : QStringLiteral("?");
                const bool isLight = reg.any_of<LightComponent>(e);
                const bool isGizmo = reg.any_of<GizmoHandleComponent>(e) || name.contains("Gizmo");
                if (maxExt > 15.0f || isLight || isGizmo) {
                    ++flagged;
                    const glm::vec3 wc = (mn + mx) * 0.5f;
                    qInfo().noquote() << QString("  e=%1 '%2' worldCenter=(%3,%4,%5) worldExt=%6 light=%7 gizmo=%8 verts=%9")
                        .arg(std::uint32_t(e)).arg(name)
                        .arg(wc.x,0,'f',1).arg(wc.y,0,'f',1).arg(wc.z,0,'f',1)
                        .arg(maxExt,0,'f',1).arg(isLight?1:0).arg(isGizmo?1:0).arg(m.vertices.size());
                }
            }
            // Gizmo root scale (the suspected culprit: viewports fight over updateScreenScale).
            for (auto e : reg.view<TagComponent, TransformComponent>()) {
                if (QString::fromStdString(reg.get<TagComponent>(e).tag) == "GizmoRoot") {
                    const auto& x = reg.get<TransformComponent>(e);
                    qInfo().noquote() << QString("  GizmoRoot scale=(%1,%2,%3) pos=(%4,%5,%6)")
                        .arg(x.scale.x,0,'f',2).arg(x.scale.y,0,'f',2).arg(x.scale.z,0,'f',2)
                        .arg(x.translation.x,0,'f',1).arg(x.translation.y,0,'f',1).arg(x.translation.z,0,'f',1);
                }
            }
            int selCount = 0; for (auto e : reg.view<SelectedComponent>()) { (void)e; ++selCount; }
            qInfo() << "===== renderable=" << n << " flagged=" << flagged << " selected=" << selCount << "=====";
        });
    }

    // Test hook: KRS_TEST_SPAWN_MESH=<path> spawns a mesh asset at the
    // origin shortly after boot (headless import/material verification).
    if (qEnvironmentVariableIsSet("KRS_TEST_SPAWN_MESH")) {
        QTimer::singleShot(1500, this, [this]() {
            spawnMeshAssetAt(qEnvironmentVariable("KRS_TEST_SPAWN_MESH"),
                             glm::vec3(0.0f, 1.0f, 0.0f));
        });
    }

    // Test hook: KRS_TESTLIGHT=point|rect spawns a LIGHT ENTITY shortly after boot, to
    // verify the ECS->lighting wiring (point) and the LTC rect area light (rect) against
    // the real rendered scene.
    if (qEnvironmentVariableIsSet("KRS_TESTLIGHT")) {
        const QString kind = qEnvironmentVariable("KRS_TESTLIGHT");
        QTimer::singleShot(1500, this, [this, kind]() {
            auto& reg = m_scene->getRegistry();
            entt::entity e = reg.create();
            auto& xf = reg.emplace<TransformComponent>(e);
            auto& lc = reg.emplace<LightComponent>(e);
            reg.emplace<TagComponent>(e, std::string("TestLight"));
            if (kind == QLatin1String("rect")) {
                lc.type = LightComponent::Type::RectArea;
                lc.size = glm::vec2(4.0f, 4.0f);
                lc.color = glm::vec3(1.0f, 0.95f, 0.9f);
                lc.intensity = 25.0f;
                lc.twoSided = false;
                // Aim the panel's emission normal (+localZ) at the robot from the camera side.
                glm::vec3 panelPos(3.0f, 1.5f, 3.0f);   // camera-side, robot height -> lights the VISIBLE faces
                if (qEnvironmentVariableIsSet("KRS_AREAPOS")) {
                    const QStringList p = qEnvironmentVariable("KRS_AREAPOS").split(',');
                    if (p.size() == 3) panelPos = glm::vec3(p[0].toFloat(), p[1].toFloat(), p[2].toFloat());
                }
                const glm::vec3 target(0.0f, 1.0f, 0.0f);
                const glm::vec3 d = glm::normalize(target - panelPos);   // desired +localZ
                const glm::vec3 zAxis(0.0f, 0.0f, 1.0f);
                const glm::vec3 axis = glm::cross(zAxis, d);
                const float axisLen = glm::length(axis);
                xf.translation = panelPos;
                xf.rotation = (axisLen < 1e-5f)
                    ? glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
                    : glm::angleAxis(std::acos(glm::clamp(glm::dot(zAxis, d), -1.0f, 1.0f)), axis / axisLen);
                // KRS_AREASIZE / KRS_AREAINT / KRS_AREATWO override size / intensity / two-sided live.
                if (qEnvironmentVariableIsSet("KRS_AREASIZE")) {
                    const float sz = qEnvironmentVariable("KRS_AREASIZE").toFloat();
                    lc.size = glm::vec2(sz, sz);
                }
                if (qEnvironmentVariableIsSet("KRS_AREAINT")) lc.intensity = qEnvironmentVariable("KRS_AREAINT").toFloat();
                if (qEnvironmentVariableIsSet("KRS_AREATWO")) lc.twoSided  = (qEnvironmentVariable("KRS_AREATWO").toInt() != 0);
                // LTC now sizes the rect from the transform scale (unit quad), so mirror size->scale.
                xf.scale = glm::vec3(lc.size.x, lc.size.y, 1.0f);
                qInfo() << "[KRS_TESTLIGHT] rect light: size" << lc.size.x << "intensity" << lc.intensity
                        << "twoSided" << lc.twoSided << "pos" << xf.translation.x << xf.translation.y << xf.translation.z;
            } else if (kind == QLatin1String("spot")) {
                lc.type = LightComponent::Type::Spot;
                lc.color = glm::vec3(0.4f, 1.0f, 0.4f);   // GREEN: obvious spot cone
                lc.intensity = 300.0f;
                lc.innerConeDeg = 18.0f; lc.outerConeDeg = 30.0f; lc.range = 0.0f;
                // Aim local +Z from (2.5,3,2.5) at the origin.
                const glm::vec3 pos(2.5f, 3.0f, 2.5f), tgt(0.0f, 0.8f, 0.0f);
                const glm::vec3 d = glm::normalize(tgt - pos), z(0, 0, 1);
                const glm::vec3 ax = glm::cross(z, d); const float al = glm::length(ax);
                xf.translation = pos;
                xf.rotation = (al < 1e-5f) ? glm::quat(1, 0, 0, 0)
                    : glm::angleAxis(std::acos(glm::clamp(glm::dot(z, d), -1.0f, 1.0f)), ax / al);
            } else if (kind == QLatin1String("dir")) {
                lc.type = LightComponent::Type::Directional;
                lc.color = glm::vec3(0.4f, 0.6f, 1.0f);   // BLUE: obvious directional
                lc.intensity = 3.0f;
                // Travel +Z -> aim down-ish (-0.4,-1,-0.3).
                const glm::vec3 d = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)), z(0, 0, 1);
                const glm::vec3 ax = glm::cross(z, d); const float al = glm::length(ax);
                xf.rotation = (al < 1e-5f) ? glm::quat(1, 0, 0, 0)
                    : glm::angleAxis(std::acos(glm::clamp(glm::dot(z, d), -1.0f, 1.0f)), ax / al);
            } else {
                lc.type = LightComponent::Type::Point;
                lc.color = glm::vec3(1.0f, 0.1f, 0.1f);   // RED: distinguishes the ECS light from the warm orbit fallback
                lc.intensity = 80.0f;
                xf.translation = glm::vec3(2.2f, 2.0f, 2.2f);
            }
            qInfo() << "[KRS_TESTLIGHT] spawned" << kind << "light entity";
        });
    }

    // Test hook: KRS_MESHER_SELFTEST=1 meshes a synthetic particle ball
    // through the OpenVDB hero-still path and spawns the result.
    if (qEnvironmentVariableIsSet("KRS_MESHER_SELFTEST")) {
        QTimer::singleShot(1500, this, [this]() {
            std::vector<glm::vec3> pts;
            const float d = 0.025f, R = 0.35f;
            for (float x = -R; x <= R; x += d)
                for (float y = -R; y <= R; y += d)
                    for (float z = -R; z <= R; z += d)
                        if (x * x + y * y + z * z <= R * R)
                            pts.emplace_back(x, y + 1.0f, z);
            RenderableMeshComponent mesh;
            if (!krs::meshFluidParticles(pts, d, mesh)) {
                qWarning() << "[MesherSelftest] FAILED";
                return;
            }
            auto& reg = m_scene->getRegistry();
            const entt::entity e = reg.create();
            reg.emplace<RenderableMeshComponent>(e, std::move(mesh));
            reg.emplace<TransformComponent>(e);
            reg.emplace<TagComponent>(e, std::string("MesherSelftest"));
            auto& mat = reg.emplace<MaterialComponent>(e);
            mat.albedoColor = glm::vec3(0.1f, 0.4f, 0.75f);
            mat.roughness = 0.08f;
            qInfo() << "[MesherSelftest] OK:" << pts.size() << "particles";
        });
    }

    // First-principles physics validation: KRS_BENCH=1 runs the analytic
    // benchmark suite and exits with the number of failed checks.
    if (qEnvironmentVariableIsSet("KRS_BENCH")) {
        auto* bench = new BenchmarkRunner(m_scene.get(), m_simulation.get(),
                                          m_renderingSystem.get(), this);
        QTimer::singleShot(4000, this, [bench]() { bench->start(); });
    }

    // Test hook: KRS_AUTOPLAY=1 starts the simulation shortly after boot and
    // logs settled body positions (used by automated verification).
    if (qEnvironmentVariableIsSet("KRS_AUTOPLAY")) {
        QTimer::singleShot(3000, this, [this]() { if (m_simulation) m_simulation->play(); });
        QTimer::singleShot(9000, this, [this]() {
            auto& reg = m_scene->getRegistry();
            for (auto e : reg.view<RigidBodyComponent, TransformComponent, TagComponent>()) {
                const auto& xf = reg.get<TransformComponent>(e);
                qInfo() << "[Sim][autoplay]" << QString::fromStdString(reg.get<TagComponent>(e).tag)
                        << "pos" << xf.translation.x << xf.translation.y << xf.translation.z;
            }
        });
    }


    connect(m_dockManager, &ads::CDockManager::focusedDockWidgetChanged, this, &MainWindow::updateViewportLayouts);
    connect(viewportDock1, &ads::CDockWidget::topLevelChanged, this, &MainWindow::updateViewportLayouts);

    // --- 6. Other Signal/Slot Connections ---
    connect(m_fixedTopToolbar, &StaticToolbar::loadRobotClicked, this, &MainWindow::onLoadRobotClicked);
    connect(viewportDock1, &ads::CDockWidget::topLevelChanged, this, [viewport1, viewportDock1](bool isFloating) {
        if (isFloating) {
            QTimer::singleShot(10, viewportDock1, [viewportDock1]() {
                viewportDock1->hide();
                viewportDock1->show();
                });
        }
        else {
            QTimer::singleShot(0, viewport1, [=]() {
                viewport1->update();
                });
        }
        });

    {
        QSignalBlocker block(m_fixedTopToolbar);
        handleMenuToggle(MenuType::GridProperties, true);
        m_fixedTopToolbar->gridMenuToggled(true);
    }



    showMenu(MenuType::GridProperties);

    // --- Diagnostics dock: live GPU/CPU frame timings ---
    {
        auto* diagPanel = new DiagnosticsPanel(this);
        diagPanel->setRenderingSystem(m_renderingSystem.get());
        auto* diagDock = new ads::CDockWidget(QStringLiteral("Diagnostics"), this);
        diagDock->setWidget(diagPanel);
        diagDock->setStyleSheet(sidePanelStyle);
        m_dockManager->addDockWidget(ads::BottomDockWidgetArea, diagDock);

        // Asset browser: mesh imports + drag-and-drop spawning, tabbed with
        // Diagnostics (the wide bottom area suits an icon grid).
        auto* assetBrowser = new AssetBrowserWidget(
            QCoreApplication::applicationDirPath() + QLatin1String("/assets"), this);
        connect(assetBrowser, &AssetBrowserWidget::spawnRequested, this,
                [this](const QString& path) {
                    spawnMeshAssetAt(path, glm::vec3(0.0f, 0.5f, 0.0f));
                });
        auto* assetDock = new ads::CDockWidget(QStringLiteral("Assets"), this);
        assetDock->setWidget(assetBrowser);
        assetDock->setStyleSheet(sidePanelStyle);
        if (auto* diagArea = diagDock->dockAreaWidget())
            m_dockManager->addDockWidget(ads::CenterDockWidgetArea, assetDock, diagArea);
        else
            m_dockManager->addDockWidget(ads::BottomDockWidgetArea, assetDock);
        registerPanelDock(QStringLiteral("Diagnostics"), diagDock);
        registerPanelDock(QStringLiteral("Assets"),      assetDock);
        diagDock->setAsCurrentTab();
    }

    // --- Physics properties dock: right column, below GridProperties ---
    {
        m_physicsPanel = new PhysicsPropertiesWidget(m_scene.get(), this);
        // Hot-apply edits to the live physics world while playing/paused.
        connect(m_physicsPanel, &PhysicsPropertiesWidget::entityComponentsChanged,
                this, [this](entt::entity e) {
                    if (m_simulation) m_simulation->notifyEntityChanged(e);
                });
        auto* physDock = new ads::CDockWidget(QStringLiteral("Physics"), this);
        physDock->setWidget(m_physicsPanel);
        physDock->setStyleSheet(sidePanelStyle);
        ads::CDockAreaWidget* rightArea = nullptr;
        auto gridIt = m_menus.find(MenuType::GridProperties);
        if (gridIt != m_menus.end() && gridIt->dock)
            rightArea = gridIt->dock->dockAreaWidget();
        if (rightArea)
            m_dockManager->addDockWidget(ads::BottomDockWidgetArea, physDock, rightArea);
        else
            m_dockManager->addDockWidget(ads::RightDockWidgetArea, physDock);

        // Fluid global controls: tabbed with the Physics panel.
        auto* fluidPanel = new FluidPropertiesWidget(m_renderingSystem.get(), this);
        auto* fluidDock = new ads::CDockWidget(QStringLiteral("Fluid"), this);
        fluidDock->setWidget(fluidPanel);
        fluidDock->setStyleSheet(sidePanelStyle);
        if (auto* physArea = physDock->dockAreaWidget())
            m_dockManager->addDockWidget(ads::CenterDockWidgetArea, fluidDock, physArea);
        else
            m_dockManager->addDockWidget(ads::RightDockWidgetArea, fluidDock);

        // Smoke/fire global controls: tabbed with the Fluid panel.
        auto* smokePanel = new SmokePropertiesWidget(m_renderingSystem.get(), this);
        auto* smokeDock = new ads::CDockWidget(QStringLiteral("Gas"), this);
        smokeDock->setWidget(smokePanel);
        smokeDock->setStyleSheet(sidePanelStyle);
        if (auto* physArea = physDock->dockAreaWidget())
            m_dockManager->addDockWidget(ads::CenterDockWidgetArea, smokeDock, physArea);
        else
            m_dockManager->addDockWidget(ads::RightDockWidgetArea, smokeDock);

        // Object lighting controls (IBL/ambient + sun): tabbed with the Fluid/Gas panels.
        auto* lightingPanel = new LightingPropertiesWidget(m_renderingSystem.get(), this);
        auto* lightingDock = new ads::CDockWidget(QStringLiteral("Lighting"), this);
        lightingDock->setWidget(lightingPanel);
        lightingDock->setStyleSheet(sidePanelStyle);
        if (auto* physArea = physDock->dockAreaWidget())
            m_dockManager->addDockWidget(ads::CenterDockWidgetArea, lightingDock, physArea);
        else
            m_dockManager->addDockWidget(ads::RightDockWidgetArea, lightingDock);

        // Texture browser: hot-swaps material packs onto the selection.
        const QString materialsRoot =
            QCoreApplication::applicationDirPath() + QLatin1String("/assets/materials");
        m_textureBrowser = new TextureBrowserWidget(m_scene.get(), materialsRoot, this);
        auto* texDock = new ads::CDockWidget(QStringLiteral("Textures"), this);
        texDock->setWidget(m_textureBrowser);
        texDock->setStyleSheet(sidePanelStyle);
        if (auto* physArea = physDock->dockAreaWidget())
            m_dockManager->addDockWidget(ads::CenterDockWidgetArea, texDock, physArea);
        else
            m_dockManager->addDockWidget(ads::RightDockWidgetArea, texDock);

        // Robot Builder: edits the live krs::rbuild::RobotGraph (delete/define joints,
        // hot-swap limits) via the proven EditController. Tabbed with the Physics panel.
        // graphChanged re-renders so spawned/edited bodies show immediately.
        m_robotBuilderPanel = new RobotBuilderPanel(m_scene.get(), this);
        connect(m_robotBuilderPanel, &RobotBuilderPanel::graphChanged, this, [this]() {
            // Re-apply the edited authoring graph to its live robot so builder edits
            // (re-type / define / delete / limits) take REAL effect on the running robot.
            if (m_scene) {
                auto& reg = m_scene->getRegistry();
                auto* gp = reg.ctx().find<krs::rbuild::RobotGraph>();
                auto* rr = reg.ctx().find<krs::robot::RobotRegistry>();
                if (gp && rr && rr->get(gp->robotId))
                    krs::robot::reapplyGraphToRobot(*m_scene, *gp, gp->robotId);
            }
            if (m_renderingSystem) m_renderingSystem->requestViewportUpdates();
        });
        auto* rbDock = new ads::CDockWidget(QStringLiteral("Robot Builder"), this);
        rbDock->setWidget(m_robotBuilderPanel);
        rbDock->setStyleSheet(sidePanelStyle);
        if (auto* physArea = physDock->dockAreaWidget())
            m_dockManager->addDockWidget(ads::CenterDockWidgetArea, rbDock, physArea);
        else
            m_dockManager->addDockWidget(ads::RightDockWidgetArea, rbDock);

        // Robot-only spinning viewport (isolated 2nd scene), bound to the SAME live
        // graph the panel edits. It BORROWS the main renderer's baked IBL (does not
        // bake its own -- a 2nd bake renders black and corrupts the environment).
        m_robotViewport = new RobotViewport(m_scene.get(), m_renderingSystem.get(), this);
        m_robotViewport->setMinimumSize(360, 280);   // don't collapse to a thin strip (else GL never inits/paints)
        connect(m_robotBuilderPanel, &RobotBuilderPanel::graphChanged,
                m_robotViewport, &RobotViewport::refreshFromLive);
        auto* rvDock = new ads::CDockWidget(QStringLiteral("Robot View"), this);
        rvDock->setWidget(m_robotViewport);
        m_dockManager->addDockWidget(ads::BottomDockWidgetArea, rvDock);

        // Register these always-docked panels for toolbar toggling (non-destructive).
        registerPanelDock(QStringLiteral("Physics"),       physDock);
        registerPanelDock(QStringLiteral("Fluid"),         fluidDock);
        registerPanelDock(QStringLiteral("Gas"),           smokeDock);
        registerPanelDock(QStringLiteral("Lighting"),      lightingDock);
        registerPanelDock(QStringLiteral("Textures"),      texDock);
        registerPanelDock(QStringLiteral("Robot Builder"), rbDock);
        registerPanelDock(QStringLiteral("Robot View"),    rvDock);

        physDock->setAsCurrentTab();
    }

    // --- Outliner: tabbed with GridProperties in the right column ---
    {
        auto* outliner = new OutlinerWidget(m_scene.get(), this);
        connect(outliner, &OutlinerWidget::selectionEdited, this,
                [this]() { refreshGizmoAndProperties(); });
        // Selecting a robot in the outliner binds the Robot Builder to it for editing
        // (the FANUC -> live limit editing; the demo -> graph authoring). Raises the
        // Builder panel so the edit controls are visible.
        connect(outliner, &OutlinerWidget::robotSelected, this, [this](int robotId) {
            if (robotId >= 0 && m_robotBuilderPanel) m_robotBuilderPanel->editRobot(robotId);
        });
        auto* outDock = new ads::CDockWidget(QStringLiteral("Outliner"), this);
        outDock->setWidget(outliner);
        outDock->setStyleSheet(sidePanelStyle);
        ads::CDockAreaWidget* rightArea = nullptr;
        auto gridIt2 = m_menus.find(MenuType::GridProperties);
        if (gridIt2 != m_menus.end() && gridIt2->dock)
            rightArea = gridIt2->dock->dockAreaWidget();
        if (rightArea)
            m_dockManager->addDockWidget(ads::CenterDockWidgetArea, outDock, rightArea);
        else
            m_dockManager->addDockWidget(ads::RightDockWidgetArea, outDock);
        registerPanelDock(QStringLiteral("Outliner"), outDock);
    }

    // Seed the toolbar panel-toggle buttons to match each dock's initial (open) visibility.
    for (auto it = m_panelDocks.constBegin(); it != m_panelDocks.constEnd(); ++it)
        if (it.value()) m_fixedTopToolbar->setPanelButtonChecked(it.key(), !it.value()->isClosed());

    // Apply the persisted UI theme now that all docks + the ribbon exist (the settings
    // boot-apply at startup ran before these were created, so it couldn't restyle them).
    applyTheme(krs::SettingsManager::instance().value(QStringLiteral("ui/theme")).toString());

    // --- Application menu bar (File / Add / Simulation) ---
    buildMenuBar();

    m_gizmoSystem->onAfterCommandApplied = [this] {
        this->refreshGizmoAndProperties(); // picks primary viewport automatically
        };

    // Phase 3 (subtree-grab): when the gizmo edits a body's transform, push the new
    // pose into the live physics world (rebuilds the actor for the moved body). For a
    // genuinely-detached sub-assembly this lets the grabbed subtree move as a passive
    // articulated body. (Previously onTransformEdited was never assigned.)
    m_gizmoSystem->onTransformEdited = [this](entt::entity e) {
        if (m_simulation) m_simulation->notifyEntityChanged(e);
    };

    auto undoSc = new QShortcut(QKeySequence::Undo, this);
    connect(undoSc, &QShortcut::activated, this, [this] {
        this->m_gizmoSystem->undo();   // adjust path to reach your gizmo system
        });

    auto redoSc = new QShortcut(QKeySequence::Redo, this);
    connect(redoSc, &QShortcut::activated, this, [this] {
        this->m_gizmoSystem->redo();
        });


    popup->updateUi(m_dockContainers, m_scene.get());
    syncViewportManagerPopup();

    // --- 7. Final Window Setup ---
    if (menuBar()) {
        menuBar()->setVisible(false);
    }
    resize(1600, 900);
    setWindowTitle("KR Studio - Kaedin Kurtz");
    setWindowIcon(QIcon(":/icons/kRLogoSquare.png"));
    statusBar()->showMessage("Ready.");

    disableFloatingForAllDockWidgets();
    QTimer::singleShot(0, this, &MainWindow::updateViewportLayouts);

    // Blender-style proportions: give the 3D viewport the lion's share once
    // all docks have been inserted (each insertion re-splits the layout).
    QTimer::singleShot(0, this, [this]() {
        if (m_dockContainers.isEmpty()) return;
        auto* area = m_dockContainers.first()->dockAreaWidget();
        if (!area) return;
        if (auto* split = area->parentSplitter()) {
            QList<int> sizes = split->sizes();
            if (sizes.size() < 2) return;
            int total = 0;
            for (int s : sizes) total += s;
            const int viewportShare = total * 60 / 100;
            const int rest = (total - viewportShare) / (sizes.size() - 1);
            sizes[0] = viewportShare;
            for (int i = 1; i < sizes.size(); ++i) sizes[i] = rest;
            split->setSizes(sizes);
        }
    });
}

// The destructor orchestrates a clean shutdown.
MainWindow::~MainWindow()
{
    m_masterRenderTimer->stop();
    m_menus.clear();

    // The new shutdown call is much simpler.
    if (m_renderingSystem) {
        m_renderingSystem->shutdown();
    }
}

// This slot handles loading robot files from the toolbar button.
void MainWindow::onLoadRobotClicked()
{
    const char* fileFilter =
        "All Supported Robot Files (*.krobot *.urdf *.sdf);;"
        "KStudio Robot (*.krobot);;"
        "URDF Robot (*.urdf);;"
        "SDF Robot (*.sdf);;"
        "All Files (*)";

    QString filePath = QFileDialog::getOpenFileName(this, "Load Robot Model", "", fileFilter);
    if (filePath.isEmpty()) {
        return;
    }

    RobotDescription description;
    bool loadSuccess = false;

    if (filePath.endsWith(".urdf", Qt::CaseInsensitive)) {
        description = URDFParser::parse(filePath.toStdString());
        loadSuccess = true;
    }
    else if (filePath.endsWith(".sdf", Qt::CaseInsensitive)) {
        description = SDFParser::parse(filePath.toStdString());
        loadSuccess = true;
    }
    else if (filePath.endsWith(".krobot", Qt::CaseInsensitive)) {
        description = KRobotParser::parse(filePath.toStdString());
        loadSuccess = true;
    }

    if (!loadSuccess) {
        QMessageBox::critical(this, "File Load Error", "Could not parse the selected robot file.");
        return;
    }

    if (description.needsEnrichment) {
        RobotEnrichmentDialog dialog(description, this);
        if (dialog.exec() == QDialog::Accepted) {
            const RobotDescription& finalDescription = dialog.getFinalDescription();
            QString krobotSavePath = QFileDialog::getSaveFileName(this, "Save Enriched KRobot File", "", "KRobot Files (*.krobot)");
            if (krobotSavePath.isEmpty()) return;

            if (KRobotWriter::save(finalDescription, krobotSavePath.toStdString())) {
                statusBar()->showMessage(QString("Successfully imported and enriched '%1'").arg(QFileInfo(filePath).fileName()));
                SceneBuilder::spawnRobot(*m_scene, finalDescription);
            }
            else {
                QMessageBox::critical(this, "File Save Error", "Could not save the new .krobot file.");
            }
        }
    }
    else {
        statusBar()->showMessage(QString("Successfully loaded robot '%1'").arg(QString::fromStdString(description.name)));
        SceneBuilder::spawnRobot(*m_scene, description);
    }
}

void MainWindow::onFlowVisualizerSettingsChanged()
{
    if (!m_flowVisualizerMenu) return;          // menu closed -> nothing to read from (avoid dangling deref)
    auto& registry = m_scene->getRegistry();
    auto view = registry.view<FieldVisualizerComponent, TransformComponent>();

    // --- FIX: Use .size_hint() to check if the view is empty ---
    if (view.size_hint() == 0) {
        qWarning() << "onFlowVisualizerSettingsChanged called, but no FieldVisualizerComponent found in scene.";
        return;
    }


    auto visualizerEntity = view.front();
    auto& visualizer = view.get<FieldVisualizerComponent>(visualizerEntity);
    auto& transform = view.get<TransformComponent>(visualizerEntity);

    // --- Update Transform and General Settings ---
    transform.translation = m_flowVisualizerMenu->getFieldPosition();
    transform.rotation = glm::quat(glm::radians(m_flowVisualizerMenu->getFieldOrientation()));
    visualizer.isEnabled = m_flowVisualizerMenu->isMasterVisible();
    visualizer.displayMode = m_flowVisualizerMenu->getDisplayMode();
    visualizer.bounds = m_flowVisualizerMenu->getBounds();
    m_flowVisualizerMenu->commitToComponent(visualizer);
    // --- FIX: Update the correct nested struct based on the display mode ---
    switch (visualizer.displayMode)
    {
    case FieldVisualizerComponent::DisplayMode::Arrows:
    {
        auto& settings = visualizer.arrowSettings;
        settings.density = m_flowVisualizerMenu->getArrowDensity();
        settings.vectorScale = m_flowVisualizerMenu->getArrowBaseSize();
        settings.headScale = m_flowVisualizerMenu->getArrowHeadScale();
        settings.intensityMultiplier = m_flowVisualizerMenu->getArrowIntensityMultiplier();
        settings.cullingThreshold = m_flowVisualizerMenu->getArrowCullingThreshold();
        settings.scaleByLength = m_flowVisualizerMenu->isArrowLengthScaled();
        settings.lengthScaleMultiplier = m_flowVisualizerMenu->getArrowLengthScaleMultiplier();
        settings.scaleByThickness = m_flowVisualizerMenu->isArrowThicknessScaled();
        settings.thicknessScaleMultiplier = m_flowVisualizerMenu->getArrowThicknessScaleMultiplier();
        settings.coloringMode = m_flowVisualizerMenu->getArrowColoringMode();
        settings.xPosColor = m_flowVisualizerMenu->getArrowDirColor(0);
        settings.xNegColor = m_flowVisualizerMenu->getArrowDirColor(1);
        settings.yPosColor = m_flowVisualizerMenu->getArrowDirColor(2);
        settings.yNegColor = m_flowVisualizerMenu->getArrowDirColor(3);
        settings.zPosColor = m_flowVisualizerMenu->getArrowDirColor(4);
        settings.zNegColor = m_flowVisualizerMenu->getArrowDirColor(5);
        settings.intensityGradient = m_flowVisualizerMenu->getArrowIntensityGradient();
        break;
    }
    case FieldVisualizerComponent::DisplayMode::Particles:
    {
        auto& settings = visualizer.particleSettings;
        settings.isSolid = m_flowVisualizerMenu->isParticleSolid();
        settings.particleCount = m_flowVisualizerMenu->getParticleCount();
        settings.lifetime = m_flowVisualizerMenu->getParticleLifetime();
        settings.baseSpeed = m_flowVisualizerMenu->getParticleBaseSpeed();
        settings.speedIntensityMultiplier = m_flowVisualizerMenu->getParticleSpeedIntensityMult();
        settings.baseSize = m_flowVisualizerMenu->getParticleBaseSize();
        settings.peakSizeMultiplier = m_flowVisualizerMenu->getParticlePeakSizeMult();
        settings.minSize = m_flowVisualizerMenu->getParticleMinSize();
        settings.baseGlowSize = m_flowVisualizerMenu->getParticleBaseGlow();
        settings.peakGlowMultiplier = m_flowVisualizerMenu->getParticlePeakGlowMult();
        settings.minGlowSize = m_flowVisualizerMenu->getParticleMinGlow();
        settings.randomWalkStrength = m_flowVisualizerMenu->getParticleRandomWalk();

        settings.coloringMode = m_flowVisualizerMenu->getParticleColoringMode();
        settings.xPosColor = m_flowVisualizerMenu->getParticleDirColor(0);
        settings.xNegColor = m_flowVisualizerMenu->getParticleDirColor(1);
        // ... etc for other particle direction colors
        settings.intensityGradient = m_flowVisualizerMenu->getParticleIntensityGradient();
        settings.lifetimeGradient = m_flowVisualizerMenu->getParticleLifetimeGradient();
        break;
    }
    case FieldVisualizerComponent::DisplayMode::Flow:
    {
        auto& settings = visualizer.flowSettings;
        settings.particleCount = m_flowVisualizerMenu->getFlowParticleCount();
        settings.lifetime = m_flowVisualizerMenu->getFlowLifetime();
        settings.baseSpeed = m_flowVisualizerMenu->getFlowBaseSpeed();
        settings.speedIntensityMultiplier = m_flowVisualizerMenu->getFlowSpeedIntensityMult();
        settings.baseSize = m_flowVisualizerMenu->getFlowBaseSize();
        settings.headScale = m_flowVisualizerMenu->getFlowHeadScale();
        settings.peakSizeMultiplier = m_flowVisualizerMenu->getFlowPeakSizeMult();
        settings.minSize = m_flowVisualizerMenu->getFlowMinSize();
        settings.growthPercentage = m_flowVisualizerMenu->getFlowGrowthPercent();
        settings.shrinkPercentage = m_flowVisualizerMenu->getFlowShrinkPercent();
        settings.randomWalkStrength = m_flowVisualizerMenu->getFlowRandomWalk();
        settings.scaleByLength = m_flowVisualizerMenu->isFlowLengthScaled();
        settings.lengthScaleMultiplier = m_flowVisualizerMenu->getFlowLengthScaleMultiplier();
        settings.scaleByThickness = m_flowVisualizerMenu->isFlowThicknessScaled();
        settings.thicknessScaleMultiplier = m_flowVisualizerMenu->getFlowThicknessScaleMultiplier();

        settings.coloringMode = m_flowVisualizerMenu->getFlowColoringMode();
        // ... etc for flow direction colors and gradients
        break;
    }
    }

    if (visualizer.displayMode == FieldVisualizerComponent::DisplayMode::Arrows) {
        const auto& settings = visualizer.arrowSettings;
        qDebug() << "[SETTINGS APPLIED] Mode: Arrows, Density:"
            << settings.density.x << "x" << settings.density.y << "x" << settings.density.z
            << "Scale:" << settings.vectorScale;
    }

    // Mark the component as dirty so the rendering system knows to update its GPU buffe
    for (ViewportWidget* viewport : m_viewports)
    {
        if (viewport) {
            viewport->update();
        }
    }

    visualizer.isGpuDataDirty = true;
    qDebug() << "Flow visualizer settings successfully applied to component.";
}

void MainWindow::updateVisualizerUI()
{
    auto& registry = m_scene->getRegistry();
    auto view = registry.view<const FieldVisualizerComponent>(); // View as const
    if (view.size() == 0) return;

    // Get the current state from the component
    const auto& visualizer = view.get<const FieldVisualizerComponent>(view.front());

    // Pass the component to the menu's new public setter function
    m_flowVisualizerMenu->updateControlsFromComponent(visualizer);
}

void MainWindow::onFlowVisualizerTransformChanged()
{
    if (!m_flowVisualizerMenu) return;
    auto& reg = m_scene->getRegistry();
    auto view = reg.view<TransformComponent, FieldVisualizerComponent>();
    if (view.size_hint() == 0) return;

    auto e = view.front();
    auto [xf, viz] = view.get<TransformComponent, FieldVisualizerComponent>(e);

    xf.translation = m_flowVisualizerMenu->getCentre();
    xf.rotation = glm::quat(glm::radians(m_flowVisualizerMenu->getEuler()));
    viz.isGpuDataDirty = true;          // in case centre moved
}

void MainWindow::onTestNewViewport()
{
    qDebug() << "--- Running Isolation Test ---";

    // Use the first camera for the test
    auto cameraEntity = m_scene->getRegistry().view<CameraComponent>().front();

    // Create a new viewport instance
    ViewportWidget* testViewport = new ViewportWidget(m_scene.get(), m_renderingSystem.get(), cameraEntity, nullptr);

    // IMPORTANT: Make sure the widget is destroyed when its window is closed
    testViewport->setAttribute(Qt::WA_DeleteOnClose);

    // Show it as a separate, top-level window, NOT in the dock manager
    testViewport->resize(800, 600);
    testViewport->show();
}

void MainWindow::updateViewportLayouts()
{
    for (ads::CDockWidget* dock : std::as_const(m_dockContainers))
    {
        // The *real* viewport is now the direct child of the dock widget
        auto* vp = qobject_cast<ViewportWidget*>(dock->widget());
        if (!vp)                                     // safety guard
            continue;

        const bool dockVisible = dock->isVisible();
        const bool dockIsTabbed = !dock->isFloating();

        // Enable painting only when the viewport is actually visible on-screen.
        // When the dock is hidden or floated we turn updates off to avoid
        // wasting GPU time (but we do not destroy anything).
        const bool shouldPaint = dockVisible && dockIsTabbed;
        vp->setUpdatesEnabled(shouldPaint);
        vp->setVisible(shouldPaint);
    }
}

// Modified addViewport function
void MainWindow::addViewport() {
    // 0) limit by palette size
    if (m_dockContainers.size() >= kPalette.size()) {
        statusBar()->showMessage("Maximum number of viewports reached.", 2000);
        return;
    }

    auto& reg = m_scene->getRegistry();

    // 1) build a set of all the palette indexes currently in use
    QSet<int> used;
    for (auto* dock : m_dockContainers) {
        if (auto* vp = qobject_cast<ViewportWidget*>(dock->widget())) {
            auto cam = vp->getCameraEntity();
            if (reg.all_of<ColorIndexComponent>(cam))
                used.insert(reg.get<ColorIndexComponent>(cam).paletteIndex);
        }
    }

    // 2) pick the smallest free index
    int colorIdx = 0;
    while (colorIdx < kPalette.size() && used.contains(colorIdx))
        ++colorIdx;

    // 3) grab that color
    const QColor   c = kPalette[colorIdx];
    glm::vec3      tint{ c.redF(), c.greenF(), c.blueF() };

    // 4) create camera & tag it
    entt::entity cam = SceneBuilder::createCamera(*m_scene, { 0.f, 2.f, 5.f + float(m_dockContainers.size()) }, tint);
    reg.emplace<ColorIndexComponent>(cam, colorIdx);

    // 5) make the viewport + dock it
    auto* vp = new ViewportWidget(m_scene.get(), m_renderingSystem.get(), cam, this);
    auto* dock = new ads::CDockWidget("");
    dock->setWidget(vp);
    m_dockContainers.append(dock);

    vp->setGizmoSystem(m_gizmoSystem.get());
    connect(vp, &ViewportWidget::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(vp, &ViewportWidget::assetDropped, this, &MainWindow::spawnMeshAssetAt);
    connect(vp, &ViewportWidget::contextMenuRequested, this,
            &MainWindow::onViewportContextMenu);

    ads::CDockAreaWidget* anchor = nullptr;
    if (m_dockContainers.size() > 1)
        anchor = m_dockContainers[m_dockContainers.size() - 2]->dockAreaWidget();

    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, dock, anchor);

    // 6) hook its close so we resync
    connect(dock, &ads::CDockWidget::closed, this, [this, dock]() {
        if (auto* vp = qobject_cast<ViewportWidget*>(dock->widget())) {
            destroyCameraRig(vp->getCameraEntity());
        }
        QTimer::singleShot(0, this, [this, dock]() {
            m_dockContainers.removeAll(dock);
            syncViewportManagerPopup();
            });
        });

    // 7) set the little X icon
    if (auto* area = dock->dockAreaWidget())
        if (auto* tb = area->titleBar())
            if (auto* btn = tb->button(ads::TitleBarButtonClose)) {
                btn->setIcon(QApplication::style()->standardIcon(QStyle::SP_TitleBarCloseButton));
                btn->setToolTip("Close");
                btn->setStyleSheet("border:none;background:transparent;");
            }

    // 8) refresh your popup/menu
    syncViewportManagerPopup();
}

void MainWindow::removeViewport() {
    if (!m_dockContainers.isEmpty()) {
        m_dockManager->removeDockWidget(m_dockContainers.last());
    }
}

void MainWindow::updateAllDockStyles()
{
    QString finalStylesheet;
    int dockCounter = 0; // To generate unique object names

    // 1. Find all CDockWidget children owned by the manager
    const auto dockWidgets = m_dockManager->findChildren<ads::CDockWidget*>();

    for (ads::CDockWidget* dock : dockWidgets)
    {
        // 2. Get the widget inside the dock and try to cast it to ViewportWidget
        if (auto* viewport = qobject_cast<ViewportWidget*>(dock->widget()))
        {
            // 3. Get the camera entity from the viewport
            entt::entity camEntity = viewport->getCameraEntity();
            if (camEntity != entt::null)
            {
                // 4. Generate a unique object name for the QSS selector
                const QString objectName = QString("viewport_dock_%1").arg(dockCounter++);

                // 5. Generate the CSS for this dock and append it to the master sheet
                //    (This uses the `generateCameraColourCss` function from the last step)
                finalStylesheet += generateCameraColourCss(dock, camEntity, objectName);
            }
        }
    }

    // 6. Apply the final, combined stylesheet to the manager
    if (!finalStylesheet.isEmpty())
    {
        m_dockManager->setStyleSheet(finalStylesheet);
    }
}

void MainWindow::setDockManagerBaseStyle()
{
    // This stylesheet tells the title bar and tab to get their colors
    // from dynamic properties that we will set on each widget later.
    // This DOES NOT override the default layout styles.

}

void MainWindow::applyCameraColorToDock(ads::CDockWidget* dock,
    entt::entity cam)
{

}

void MainWindow::onSceneReloadRequested(const QString& sceneName)
{
    qDebug() << "Main window received request to reload scene:" << sceneName; // Log the request.

    // 1. Load the new scene, which returns an owning unique_ptr.
    std::unique_ptr<Scene> newScene = db::DatabaseManager::instance().loadScene(sceneName); // Call the database manager to load the scene data.

    if (newScene) { // Check if the scene was loaded successfully.
        // 2. Replace the old scene with the new one.
        //    std::move transfers ownership to MainWindow's m_scene.
        //    The old scene is automatically deleted by the unique_ptr's destructor.
        m_scene = std::move(newScene); // The main scene pointer is now updated.

        // TODO: Update all dependent widgets (viewports, properties panel, etc.)
        // with the new scene pointer (m_scene.get()).
        // For example: m_propertiesPanel->setScene(m_scene.get());

        statusBar()->showMessage("Scene '" + sceneName + "' loaded successfully."); // Update the status bar.
        qDebug() << "Scene reloaded and is now managed by MainWindow.";
    }
    else {
        statusBar()->showMessage("Failed to load scene: " + sceneName, 5000); // Show an error in the status bar.
        QMessageBox::critical(this, "Load Error", "Failed to load scene: " + sceneName); // Show a critical error message box.
    }
}

void MainWindow::onShowViewportRequested(ads::CDockWidget* dock) {
    if (dock) {
        dock->setAsCurrentTab(); // This brings the tab to the front
        dock->raise();           // This raises the window if it's floating
    }
}

void MainWindow::onResetViewports() {
    const auto docks = m_dockContainers;  // copy
    for (auto* dock : docks) {
        m_dockManager->removeDockWidget(dock);
    }
    addViewport();
}

void MainWindow::syncViewportManagerPopup()
{
    // 1) Renumber the visible docks
    for (int i = 0; i < m_dockContainers.size(); ++i) {
        m_dockContainers[i]
            ->setWindowTitle(QStringLiteral("3D Viewport %1").arg(i + 1));
    }

    // 2) Update *your* popup (if it still exists)
    if (m_viewportManagerPopup) {
        m_viewportManagerPopup->updateUi(m_dockContainers, m_scene.get());
    }
}

// Add this new slot
void MainWindow::onViewportDockClosed(ads::CDockWidget* closedDock)
{
    if (!closedDock)
        return;

    // Debug: which dock & widget is being closed
    qDebug() << "[VIEWPORT] Closing dock:" << closedDock->windowTitle();

    if (auto* vp = qobject_cast<ViewportWidget*>(closedDock->widget()))
    {
        entt::entity cam = vp->getCameraEntity();
        qDebug() << "[VIEWPORT] Found ViewportWidget" << vp
            << "with camera entity =" << (uint32_t)cam;

        if (m_scene->getRegistry().all_of<ColorIndexComponent>(cam)) {
            int idx = m_scene->getRegistry().get<ColorIndexComponent>(cam).paletteIndex;
            m_colorInUse[idx] = false;
        }

        // 1) GPU cleanup
        m_renderingSystem->onViewportWillBeDestroyed(vp);

        // 2) Explicit camera+mesh destroy
        destroyCameraRig(cam);
    }

    // 3) Remove from the UI list and refresh
    m_dockContainers.removeAll(closedDock);
    
    syncViewportManagerPopup();
}

void MainWindow::destroyCameraRig(entt::entity cameraEntity)
{
    auto& reg = m_scene->getRegistry();
    if (!reg.valid(cameraEntity)) {
        qDebug() << "[CLEANUP] cameraEntity invalid:" << (uint32_t)cameraEntity;
        return;
    }

    // 1) Find the gizmo (mini?camera mesh) child of the camera
    entt::entity gizE = entt::null;
    for (auto entity : reg.view<ParentComponent>()) {
        if (reg.get<ParentComponent>(entity).parent == cameraEntity) {
            gizE = entity;
            qDebug() << "[CLEANUP] Found gizmo entity:" << (uint32_t)gizE;
            break;
        }
    }

    if (gizE != entt::null) {
        // 2) Find & destroy the LED child of the gizmo
        for (auto entity : reg.view<ParentComponent>()) {
            if (reg.get<ParentComponent>(entity).parent == gizE) {
                qDebug() << "[CLEANUP] Destroying LED entity:" << (uint32_t)entity;
                reg.destroy(entity);
            }
        }
        // 3) Destroy the gizmo itself
        qDebug() << "[CLEANUP] Destroying gizmo entity:" << (uint32_t)gizE;
        reg.destroy(gizE);
    }

    // 4) Finally destroy the camera entity
    qDebug() << "[CLEANUP] Destroying camera entity:" << (uint32_t)cameraEntity;
    reg.destroy(cameraEntity);
}

void MainWindow::applyTheme(const QString& theme)
{
    const bool light = (theme.compare(QLatin1String("light"), Qt::CaseInsensitive) == 0);
    const QString& style = light ? lightPanelStyle : sidePanelStyle;
    // Restyle ONLY the panel docks' contents. Do NOT touch the dock manager (its default ads
    // stylesheet draws the tab close 'X' buttons -- our panel sheet's QToolButton rule bloats
    // them) or the ribbon toolbar (our sheet darkened its dividers). Viewports (GL) untouched.
    for (auto it = m_panelDocks.constBegin(); it != m_panelDocks.constEnd(); ++it)
        if (it.value()) it.value()->setStyleSheet(style);
    for (auto it = m_menus.constBegin(); it != m_menus.constEnd(); ++it)
        if (it.value().dock) it.value().dock->setStyleSheet(style);
}

void MainWindow::registerPanelDock(const QString& title, ads::CDockWidget* dock)
{
    if (!dock) return;
    m_panelDocks.insert(title, dock);
    // Reverse-sync: reflect the dock's open/closed state on its toolbar button (covers the tab
    // 'x' close + programmatic toggles). setPanelButtonChecked blocks signals to avoid a loop.
    connect(dock, &ads::CDockWidget::viewToggled, this, [this, title](bool open) {
        if (m_fixedTopToolbar) m_fixedTopToolbar->setPanelButtonChecked(title, open);
    });
}

void MainWindow::handleMenuToggle(MenuType type, bool checked)
{
    if (checked)   showMenu(type);
    else           hideMenu(type);
}

void MainWindow::ensurePropertiesArea()
{
    // 1) If we already seeded the side-panel area, do nothing.
    if (m_propertiesArea)
        return;

    // 2) Drop in a dummy dock so ADS will create a CDockAreaWidget for us�
    auto* placeholder = new ads::CDockWidget(QString(), this);
    placeholder->setFeatures(ads::CDockWidget::NoDockWidgetFeatures);
    m_propertiesArea = m_dockManager->addDockWidget(
        ads::RightDockWidgetArea,
        placeholder
    );

    // 3) Immediately remove the dummy�area remains behind, ready for real tabs
    m_dockManager->removeDockWidget(placeholder);
    placeholder->deleteLater();
}

// In MainWindow.cpp

void MainWindow::showMenu(MenuType type)
{
    qDebug() << "\n--- showMenu for" << int(type);

    ads::CDockAreaWidget* existingArea = nullptr;
    for (auto it = m_menus.constBegin(); it != m_menus.constEnd(); ++it) {
        auto* d = it.value().dock;
        if (d && !d->isHidden()) {
            existingArea = d->dockAreaWidget();
            break;
        }
    }

    auto& entry = m_menus[type];
    if (!entry.menu) {
        entry.menu = MenuFactory::create(type, m_scene.get(), this);
        if (!entry.menu) {
            qWarning() << "Factory failed for" << int(type);
            return;
        }

        QString title = db::DatabaseManager::menuTypeToString(type);
        entry.dock = new ads::CDockWidget(title, this);
        entry.dock->setWidget(entry.menu->widget());
        entry.dock->setStyleSheet(sidePanelStyle);

        if (existingArea) {
            m_dockManager->addDockWidget(ads::CenterDockWidgetArea, entry.dock, existingArea);
        }
        else {
            m_dockManager->addDockWidget(ads::RightDockWidgetArea, entry.dock);
        }

        // *** START OF NEW CONNECTION LOGIC ***
        if (type == MenuType::ObjectProperties) {
            if (auto* propertiesPanel = dynamic_cast<ObjectPropertiesWidget*>(entry.menu->widget())) {
                // C3: live-apply rigid-body edits (esp. bodyType) to the running PhysX world, so a
                // flip-to-Dynamic continues from the live pose+velocity instead of waiting for a
                // stop()/play() that restores the authored pose. Mirrors the PhysicsPropertiesWidget
                // wiring above (entityComponentsChanged -> notifyEntityChanged).
                connect(propertiesPanel, &ObjectPropertiesWidget::entityComponentsChanged, this,
                        [this](entt::entity e) { if (m_simulation) m_simulation->notifyEntityChanged(e); });
            }
        }
        // *** END OF NEW CONNECTION LOGIC ***

        connect(entry.dock, &ads::CDockWidget::closed, this, [this, type]() {
            auto it = m_menus.find(type);
            if (it != m_menus.end()) {
                it->menu->shutdownAndSave();
                m_fixedTopToolbar->uncheckButtonForMenu(type);
                m_menus.remove(type);
            }
            if (type == MenuType::FlowVisualizer) m_flowVisualizerMenu = nullptr;  // avoid a dangling deref
            });

        if (db::DatabaseManager::instance().menuConfigExists(title))
            entry.menu->initializeFromDatabase();
        else
            entry.menu->initializeFresh();

        // WIRE the flow-visualizer menu to the live FieldVisualizerComponent. Previously the menu opened
        // but its master-visibility / arrow settings never reached the component (m_flowVisualizerMenu was
        // never assigned and settingsChanged was never connected) -- so there was no way to actually turn
        // the field visualizer on from the UI. Done AFTER initialize* so the getters read real control state.
        if (type == MenuType::FlowVisualizer) {
            if (auto* fvm = dynamic_cast<FlowVisualizerMenu*>(entry.menu->widget())) {
                m_flowVisualizerMenu = fvm;
                connect(fvm, &FlowVisualizerMenu::settingsChanged, this,
                        &MainWindow::onFlowVisualizerSettingsChanged, Qt::UniqueConnection);
                connect(fvm, &FlowVisualizerMenu::transformChanged, this,
                        &MainWindow::onFlowVisualizerTransformChanged, Qt::UniqueConnection);
                updateVisualizerUI();               // menu controls  <- live component state
                onFlowVisualizerSettingsChanged();  // live component <- menu state (incl. master visibility)
            }
        }
    }

    if (m_fixedTopToolbar) m_fixedTopToolbar->checkButtonForMenu(type);
    entry.dock->show();
    entry.dock->setAsCurrentTab();
}

void MainWindow::hideMenu(MenuType type)
{
    auto it = m_menus.find(type);
    if (it == m_menus.end()) return;
    auto& entry = it.value();
    // Save UI state, then hide
    entry.menu->shutdownAndSave();
    // 2) tear down the dock widget completely
    m_dockManager->removeDockWidget(entry.dock);
    // 3) finally erase from our map so both dock+menu are destroyed
    m_menus.erase(it);
}

void MainWindow::onSelectionChanged(const QVector<entt::entity>& selectedEntities, const Camera& camera)
{
    // This slot acts as the central hub.
    if (m_gizmoSystem) {
        m_gizmoSystem->update(selectedEntities, camera);
    }

    if (m_physicsPanel)
        m_physicsPanel->setEntity(selectedEntities.isEmpty() ? entt::null : selectedEntities.first());

    // Update the properties panel to show the FIRST selected object's properties.
    auto it = m_menus.find(MenuType::ObjectProperties);
    if (it != m_menus.end()) {
        if (auto* propertiesPanel = dynamic_cast<ObjectPropertiesWidget*>(it->menu->widget())) {
            // If the selection is empty, pass null. Otherwise, pass the first entity.
            entt::entity firstEntity = selectedEntities.isEmpty() ? entt::null : selectedEntities.first();
            propertiesPanel->setEntity(firstEntity);
        }
    }
}

ViewportWidget* MainWindow::primaryViewport() const
{
    // Prefer the focused viewport; else first available
    for (auto* dock : m_dockContainers) {
        if (auto* vp = qobject_cast<ViewportWidget*>(dock->widget())) {
            if (vp->hasFocus()) return vp;
        }
    }
    for (auto* dock : m_dockContainers) {
        if (auto* vp = qobject_cast<ViewportWidget*>(dock->widget())) {
            return vp;
        }
    }
    return nullptr;
}

entt::entity MainWindow::addObjectFromMenu(int primitive, const QString& baseName,
                                           const glm::vec3& pos, const glm::vec3& scale)
{
    auto& reg = m_scene->getRegistry();

    // Auto-number: Cube.001, Cube.002 ... (Blender style)
    int count = 1;
    for (auto e : reg.view<TagComponent>()) {
        const auto& t = reg.get<TagComponent>(e).tag;
        if (QString::fromStdString(t).startsWith(baseName + QLatin1Char('.'))) ++count;
    }
    const QString name = QStringLiteral("%1.%2").arg(baseName).arg(count, 3, 10, QLatin1Char('0'));

    entt::entity e = SceneBuilder::spawnPrimitive(*m_scene, primitive, pos, scale, name.toStdString());
    auto& mat = reg.emplace_or_replace<MaterialComponent>(e);
    mat.albedoColor = glm::vec3(0.62f, 0.62f, 0.66f);

    // Select the new object so the gizmo and properties pick it up.
    for (auto eSel : reg.view<SelectedComponent>()) reg.remove<SelectedComponent>(eSel);
    reg.emplace<SelectedComponent>(e);
    refreshGizmoAndProperties();
    return e;
}

entt::entity MainWindow::addLightFromMenu(int lightType, const glm::vec3& worldPos)
{
    auto& reg = m_scene->getRegistry();
    const auto type = static_cast<LightComponent::Type>(lightType);

    // Blender-style auto-numbering off a per-type base name (Point Light.001, ...).
    const QString baseName =
        type == LightComponent::Type::Point       ? QStringLiteral("Point Light")
      : type == LightComponent::Type::Directional ? QStringLiteral("Directional Light")
      : type == LightComponent::Type::Spot        ? QStringLiteral("Spot Light")
                                                  : QStringLiteral("Area Light");
    // Use max-suffix+1 (not a count) so deleting a middle light never yields a duplicate name.
    int maxN = 0;
    const QString prefix = baseName + QLatin1Char('.');
    for (auto e : reg.view<TagComponent>()) {
        const QString t = QString::fromStdString(reg.get<TagComponent>(e).tag);
        if (!t.startsWith(prefix)) continue;
        bool ok = false; const int n = t.mid(prefix.size()).toInt(&ok);
        if (ok && n > maxN) maxN = n;
    }
    const QString name = QStringLiteral("%1.%2").arg(baseName).arg(maxN + 1, 3, 10, QLatin1Char('0'));

    // Warm/cool defaults; the inspector's Light tab tunes everything afterwards.
    const glm::vec3 color = (type == LightComponent::Type::RectArea)
        ? glm::vec3(1.0f, 0.96f, 0.90f) : glm::vec3(1.0f, 0.95f, 0.88f);
    // Photometric defaults (brought to display by the EV exposure): lumens / candela / lux / nits.
    const float intensity =
        type == LightComponent::Type::Point       ? 40000.0f   // lumens
      : type == LightComponent::Type::Spot        ? 40000.0f   // candela
      : type == LightComponent::Type::Directional ? 8000.0f    // lux
                                                  : 6000.0f;   // nits (RectArea)

    entt::entity e = SceneBuilder::spawnLightEmitter(*m_scene, type, worldPos, color,
                                                     intensity, name.toStdString());

    // Select the new light so the gizmo + Light inspector pick it up.
    for (auto eSel : reg.view<SelectedComponent>()) reg.remove<SelectedComponent>(eSel);
    reg.emplace<SelectedComponent>(e);
    refreshGizmoAndProperties();
    return e;
}

entt::entity MainWindow::duplicateEntity(entt::entity src)
{
    auto& reg = m_scene->getRegistry();
    if (!reg.valid(src)) return entt::null;
    entt::entity dst = reg.create();

    // Copy renderable + transform (offset so the copy is visible) + the
    // material/physics description — never live PhysX handles; the physics
    // world rebuilds actors from components on Play.
    if (auto* mesh = reg.try_get<RenderableMeshComponent>(src))
        reg.emplace<RenderableMeshComponent>(dst, *mesh);
    if (auto* xf = reg.try_get<TransformComponent>(src)) {
        TransformComponent copy = *xf;
        copy.translation += glm::vec3(0.5f, 0.0f, 0.5f);
        reg.emplace<TransformComponent>(dst, copy);
    }
    if (auto* mat = reg.try_get<MaterialComponent>(src)) reg.emplace<MaterialComponent>(dst, *mat);
    if (auto* dir = reg.try_get<MaterialDirectoryTag>(src)) reg.emplace<MaterialDirectoryTag>(dst, *dir);
    if (reg.all_of<UVTexturedMaterialTag>(src)) reg.emplace<UVTexturedMaterialTag>(dst);
    if (reg.all_of<TriPlanarMaterialTag>(src)) reg.emplace<TriPlanarMaterialTag>(dst);
    if (reg.all_of<ParallaxMaterialTag>(src)) reg.emplace<ParallaxMaterialTag>(dst);
    if (auto* rb = reg.try_get<RigidBodyComponent>(src)) reg.emplace<RigidBodyComponent>(dst, *rb);
    if (auto* bc = reg.try_get<BoxCollider>(src)) reg.emplace<BoxCollider>(dst, *bc);
    if (auto* sc = reg.try_get<SphereCollider>(src)) reg.emplace<SphereCollider>(dst, *sc);
    if (auto* ac = reg.try_get<AutoCollisionComponent>(src)) reg.emplace<AutoCollisionComponent>(dst, *ac);
    // A duplicated light must keep emitting (copy the LightComponent), not just glow.
    if (auto* lc = reg.try_get<LightComponent>(src)) reg.emplace<LightComponent>(dst, *lc);
    if (reg.all_of<LightEmitterTag>(src)) reg.emplace<LightEmitterTag>(dst);
    if (auto* pe = reg.try_get<LightEmitterPrevEmissive>(src)) reg.emplace<LightEmitterPrevEmissive>(dst, *pe);

    QString base = QStringLiteral("Object");
    if (auto* tag = reg.try_get<TagComponent>(src))
        base = QString::fromStdString(tag->tag).section(QLatin1Char('.'), 0, 0);
    int count = 1;
    for (auto e : reg.view<TagComponent>())
        if (QString::fromStdString(reg.get<TagComponent>(e).tag).startsWith(base)) ++count;
    reg.emplace<TagComponent>(
        dst, QStringLiteral("%1.%2").arg(base).arg(count, 3, 10, QLatin1Char('0')).toStdString());

    for (auto eSel : reg.view<SelectedComponent>()) reg.remove<SelectedComponent>(eSel);
    reg.emplace<SelectedComponent>(dst);
    refreshGizmoAndProperties();
    return dst;
}

void MainWindow::onViewportContextMenu(const QPoint& globalPos, const glm::vec3& worldPos,
                                       entt::entity hit)
{
    auto& reg = m_scene->getRegistry();
    const bool hitIsObject = reg.valid(hit)
        && !reg.any_of<CameraComponent, GridComponent, GizmoHandleComponent>(hit);

    QMenu menu(this);
    if (hitIsObject) {
        const QString name = reg.all_of<TagComponent>(hit)
            ? QString::fromStdString(reg.get<TagComponent>(hit).tag)
            : QStringLiteral("Object");
        menu.addSection(name);
        menu.addAction(QStringLiteral("Duplicate"), [this, hit]() { duplicateEntity(hit); });
        menu.addAction(QStringLiteral("Rename…"), [this, hit, name]() {
            bool ok = false;
            const QString text = QInputDialog::getText(
                this, QStringLiteral("Rename"), QStringLiteral("Name:"),
                QLineEdit::Normal, name, &ok);
            if (ok && !text.isEmpty())
                m_scene->getRegistry().emplace_or_replace<TagComponent>(hit, text.toStdString());
        });
        menu.addAction(QStringLiteral("Focus camera"), [this, worldPos]() {
            if (ViewportWidget* vp = primaryViewport()) {
                Camera& cam = vp->getCamera();
                cam.focusOn(worldPos, glm::length(cam.getPosition() - worldPos));
            }
        });
        const bool isGlass = reg.all_of<GlassComponent>(hit);
        menu.addAction(isGlass ? QStringLiteral("Make Solid") : QStringLiteral("Make Glass"),
                       [this, hit, isGlass]() {
                           auto& r = m_scene->getRegistry();
                           if (!r.valid(hit)) return;
                           if (isGlass) r.remove<GlassComponent>(hit);
                           else r.emplace<GlassComponent>(hit);
                       });
        menu.addAction(QStringLiteral("Delete"), [this, hit]() {
            auto& r = m_scene->getRegistry();
            if (r.valid(hit)) r.destroy(hit);
            refreshGizmoAndProperties();
        });
        menu.addSeparator();
    }
    QMenu* addMenu = menu.addMenu(QStringLiteral("Add here"));
    auto addPrim = [this, addMenu, worldPos](const QString& label, Primitive prim, glm::vec3 scale) {
        addMenu->addAction(label, [this, label, prim, worldPos, scale]() {
            addObjectFromMenu(int(prim), label, worldPos + glm::vec3(0, 0.5f, 0), scale);
        });
    };
    addPrim(QStringLiteral("Cube"), Primitive::Cube, glm::vec3(1.0f));
    addPrim(QStringLiteral("Sphere"), Primitive::IcoSphere, glm::vec3(0.5f));
    addPrim(QStringLiteral("Cylinder"), Primitive::Cylinder, glm::vec3(0.5f, 1.0f, 0.5f));

    QMenu* lightMenu = menu.addMenu(QStringLiteral("Add light here"));
    struct CtxLightOpt { const char* label; LightComponent::Type type; };
    const CtxLightOpt ctxLights[] = {
        { "Point Light",       LightComponent::Type::Point },
        { "Spot Light",        LightComponent::Type::Spot },
        { "Area Light",        LightComponent::Type::RectArea },
        { "Directional Light", LightComponent::Type::Directional },
    };
    for (const auto& o : ctxLights) {
        const auto type = o.type;
        lightMenu->addAction(QString::fromLatin1(o.label), [this, type, worldPos]() {
            const float lift = (type == LightComponent::Type::RectArea
                             || type == LightComponent::Type::Directional) ? 2.0f : 0.5f;
            addLightFromMenu(int(type), worldPos + glm::vec3(0.0f, lift, 0.0f));
        });
    }

    QMenu* simMenu = menu.addMenu(QStringLiteral("Add simulation source"));
    simMenu->addAction(QStringLiteral("Water Tap"), [this, worldPos]() {
        spawnSimSourceAt(SimSource::WaterTap, worldPos + glm::vec3(0, 0.5f, 0));
    });
    simMenu->addAction(QStringLiteral("Water Drain (sink)"), [this, worldPos]() {
        spawnSimSourceAt(SimSource::WaterSink, worldPos + glm::vec3(0, 0.2f, 0));
    });
    simMenu->addAction(QStringLiteral("Smoke Emitter"), [this, worldPos]() {
        spawnSimSourceAt(SimSource::SmokeEmitter, worldPos + glm::vec3(0, 0.2f, 0));
    });
    simMenu->addAction(QStringLiteral("Fire Emitter"), [this, worldPos]() {
        spawnSimSourceAt(SimSource::FireEmitter, worldPos + glm::vec3(0, 0.2f, 0));
    });

    menu.exec(globalPos);
}

void MainWindow::spawnSimSourceAt(SimSource kind, const glm::vec3& worldPos)
{
    if (!m_scene) return;
    auto& reg = m_scene->getRegistry();
    // A small visible marker so the source can be selected, moved and tuned.
    const char* name = kind == SimSource::WaterTap     ? "WaterTap"
                     : kind == SimSource::WaterSink    ? "WaterDrain"
                     : kind == SimSource::SmokeEmitter ? "SmokeEmitter"
                                                       : "FireEmitter";
    entt::entity e = SceneBuilder::spawnPrimitive(*m_scene, int(Primitive::IcoSphere),
                                                  worldPos, glm::vec3(0.12f), name);
    auto& mat = reg.emplace_or_replace<MaterialComponent>(e);
    switch (kind) {
    case SimSource::WaterTap:
        mat.albedoColor = { 0.2f, 0.5f, 0.95f };
        { auto& em = reg.emplace<FluidEmitterComponent>(e);
          em.ratePerSecond = 1200.0f; em.initialSpeed = 1.5f; em.emitterRadius = 0.08f;
          em.particleLifetime = 18.0f; }
        break;
    case SimSource::WaterSink:
        mat.albedoColor = { 0.1f, 0.1f, 0.15f };
        reg.emplace<FluidSinkComponent>(e).halfExtents = glm::vec3(0.3f);
        break;
    case SimSource::SmokeEmitter:
        mat.albedoColor = { 0.7f, 0.7f, 0.75f };
        reg.emplace<SmokeEmitterComponent>(e);
        break;
    case SimSource::FireEmitter:
        mat.albedoColor = { 0.95f, 0.45f, 0.1f };
        mat.emissiveColor = { 1.0f, 0.4f, 0.05f };
        mat.emissiveStrength = 2.0f;
        { auto& sm = reg.emplace<SmokeEmitterComponent>(e);
          sm.fuelRate = 2.5f; sm.temperature = 1.0f; sm.densityRate = 1.2f;
          sm.color = { 0.2f, 0.2f, 0.2f }; }
        break;
    }
    for (auto eSel : reg.view<SelectedComponent>()) reg.remove<SelectedComponent>(eSel);
    reg.emplace<SelectedComponent>(e);
    refreshGizmoAndProperties();
    statusBar()->showMessage(QStringLiteral("Added %1").arg(name), 3000);
}

void MainWindow::spawnMeshAssetAt(const QString& path, const glm::vec3& worldPos)
{
    if (!m_scene) return;
    MeshID id = ResourceManager::instance().loadMesh(path);
    if (id == MeshID::None) {
        statusBar()->showMessage(QStringLiteral("Mesh load failed: %1").arg(path), 5000);
        return;
    }
    entt::entity e = SceneBuilder::spawnMeshInstance(*m_scene, id, worldPos);
    auto& reg = m_scene->getRegistry();
    reg.emplace_or_replace<TagComponent>(e, QFileInfo(path).baseName().toStdString());
    for (auto eSel : reg.view<SelectedComponent>()) reg.remove<SelectedComponent>(eSel);
    reg.emplace<SelectedComponent>(e);
    refreshGizmoAndProperties();
    statusBar()->showMessage(QStringLiteral("Spawned %1").arg(QFileInfo(path).fileName()), 4000);
}

void MainWindow::buildMenuBar()
{
    auto* bar = menuBar();
    bar->setStyleSheet(
        "QMenuBar { background-color: #2c313a; color: #d5d5d5; }"
        "QMenuBar::item:selected { background-color: #4a5260; }"
        "QMenu { background-color: #353b46; color: #d5d5d5; border: 1px solid #4a5260; }"
        "QMenu::item:selected { background-color: #0078d7; color: white; }"
        "QMenu::separator { height: 1px; background: #4a5260; margin: 4px 8px; }");

    // --- File ---
    QMenu* fileMenu = bar->addMenu(QStringLiteral("&File"));
    QAction* importAct = fileMenu->addAction(QStringLiteral("Import Mesh..."));
    importAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    connect(importAct, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Import Mesh"), QString(),
            QStringLiteral("Meshes (*.stl *.obj *.fbx *.dae *.ply *.gltf *.glb *.3ds);;All files (*)"));
        if (path.isEmpty()) return;
        spawnMeshAssetAt(path, glm::vec3(0.0f, 0.5f, 0.0f));
    });
    fileMenu->addSeparator();
    QAction* quitAct = fileMenu->addAction(QStringLiteral("Quit"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    // --- Edit ---
    QMenu* editMenu = bar->addMenu(QStringLiteral("&Edit"));
    QAction* deleteAct = editMenu->addAction(QStringLiteral("Delete Selected"));
    deleteAct->setShortcut(QKeySequence::Delete);
    connect(deleteAct, &QAction::triggered, this, [this]() {
        auto& reg = m_scene->getRegistry();
        std::vector<entt::entity> doomed;
        for (auto e : reg.view<SelectedComponent>()) {
            // Never delete cameras or grids through this path.
            if (reg.any_of<CameraComponent, GridComponent>(e)) continue;
            doomed.push_back(e);
        }
        if (doomed.empty()) return;
        reg.destroy(doomed.begin(), doomed.end());
        refreshGizmoAndProperties();
        statusBar()->showMessage(QStringLiteral("Deleted %1 object(s)").arg(doomed.size()), 3000);
    });

    // --- Add (Blender's Shift+A spirit) ---
    QMenu* addMenu = bar->addMenu(QStringLiteral("&Add"));
    QMenu* meshMenu = addMenu->addMenu(QStringLiteral("Mesh"));
    auto addPrim = [this, meshMenu](const QString& label, Primitive prim, glm::vec3 scale) {
        QAction* a = meshMenu->addAction(label);
        connect(a, &QAction::triggered, this, [this, label, prim, scale]() {
            addObjectFromMenu(int(prim), label, glm::vec3(0.0f, 0.5f, 0.0f), scale);
        });
    };
    addPrim(QStringLiteral("Cube"), Primitive::Cube, glm::vec3(1.0f));
    addPrim(QStringLiteral("Sphere"), Primitive::IcoSphere, glm::vec3(0.5f));
    addPrim(QStringLiteral("Cylinder"), Primitive::Cylinder, glm::vec3(0.5f, 1.0f, 0.5f));
    addPrim(QStringLiteral("Cone"), Primitive::Cone, glm::vec3(0.5f, 1.0f, 0.5f));
    addPrim(QStringLiteral("Torus"), Primitive::Torus, glm::vec3(0.6f));
    addPrim(QStringLiteral("Plane"), Primitive::Quad, glm::vec3(2.0f, 1.0f, 2.0f));

    // --- Light emitters (glowing primitive that also illuminates) ---
    addMenu->addSeparator();
    QMenu* lightMenu = addMenu->addMenu(QStringLiteral("Light"));
    struct LightMenuOpt { const char* label; LightComponent::Type type; float y; };
    const LightMenuOpt lightOpts[] = {
        { "Point Light",       LightComponent::Type::Point,       1.5f },
        { "Spot Light",        LightComponent::Type::Spot,        3.0f },
        { "Area Light",        LightComponent::Type::RectArea,    4.0f },
        { "Directional Light", LightComponent::Type::Directional, 4.0f },
    };
    for (const auto& o : lightOpts) {
        const auto type = o.type; const float y = o.y;
        lightMenu->addAction(QString::fromLatin1(o.label), this, [this, type, y]() {
            addLightFromMenu(int(type), glm::vec3(0.0f, y, 0.0f));
        });
    }

    addMenu->addSeparator();
    QAction* addEmitter = addMenu->addAction(QStringLiteral("Fluid Emitter"));
    connect(addEmitter, &QAction::triggered, this, [this]() {
        // Small marker sphere so the emitter is visible, pickable and movable.
        entt::entity e = addObjectFromMenu(int(Primitive::IcoSphere), QStringLiteral("Emitter"),
                                           glm::vec3(0.0f, 1.5f, 0.0f), glm::vec3(0.08f));
        auto& reg = m_scene->getRegistry();
        reg.emplace<FluidEmitterComponent>(e);
        auto& mat = reg.get<MaterialComponent>(e);
        mat.albedoColor = glm::vec3(0.15f, 0.55f, 0.95f);
        if (m_physicsPanel) m_physicsPanel->setEntity(e);
    });
    QAction* addVolume = addMenu->addAction(QStringLiteral("Fluid Volume"));
    connect(addVolume, &QAction::triggered, this, [this]() {
        entt::entity e = addObjectFromMenu(int(Primitive::IcoSphere), QStringLiteral("FluidVolume"),
                                           glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.08f));
        auto& reg = m_scene->getRegistry();
        reg.emplace<FluidVolumeComponent>(e);
        auto& mat = reg.get<MaterialComponent>(e);
        mat.albedoColor = glm::vec3(0.2f, 0.8f, 0.9f);
        if (m_physicsPanel) m_physicsPanel->setEntity(e);
    });
    // Simulation sources (emitters/sinks/gas) — same spawn path as the
    // viewport right-click menu, placed at the origin to be dragged.
    addMenu->addSeparator();
    addMenu->addAction(QStringLiteral("Water Drain (sink)"), this, [this]() {
        spawnSimSourceAt(SimSource::WaterSink, glm::vec3(0.0f, 0.3f, 0.0f));
    });
    addMenu->addAction(QStringLiteral("Smoke Emitter"), this, [this]() {
        spawnSimSourceAt(SimSource::SmokeEmitter, glm::vec3(0.0f, 0.2f, 0.0f));
    });
    addMenu->addAction(QStringLiteral("Fire Emitter"), this, [this]() {
        spawnSimSourceAt(SimSource::FireEmitter, glm::vec3(0.0f, 0.2f, 0.0f));
    });

    // --- View ---
    QMenu* viewMenu = bar->addMenu(QStringLiteral("&View"));
    QAction* showCollision = viewMenu->addAction(QStringLiteral("Show Collision Shapes"));
    showCollision->setCheckable(true);
    if (m_scene)
        showCollision->setChecked(
            m_scene->getRegistry().ctx().get<SceneProperties>().showCollisionShapes);
    showCollision->setToolTip(QStringLiteral(
        "Wireframe of the geometry the physics solver actually collides with:\n"
        "green = static, orange = dynamic, cyan = kinematic, red = AABB fallback"));
    connect(showCollision, &QAction::toggled, this, [this](bool on) {
        if (!m_scene) return;
        m_scene->getRegistry().ctx().get<SceneProperties>().showCollisionShapes = on;
    });

    QAction* showFeatureSel = viewMenu->addAction(QStringLiteral("Show Feature Selection Highlights"));
    showFeatureSel->setCheckable(true);
    if (m_scene)
        if (auto* st = m_scene->getRegistry().ctx().find<krs::sel::SelectionState>())
            showFeatureSel->setChecked(st->enabled);
    showFeatureSel->setToolTip(QStringLiteral(
        "Hover a CAD face to preview-highlight it (yellow); click to select (orange disk + axis arrow).\n"
        "Indicators are derived from the exact B-Rep feature the ray resolves to."));
    connect(showFeatureSel, &QAction::toggled, this, [this](bool on) {
        if (!m_scene) return;
        if (auto* st = m_scene->getRegistry().ctx().find<krs::sel::SelectionState>()) st->enabled = on;
    });

    // --- Physics visualization (Phase 3): recolour MPM particles by a field ---
    QMenu* vizMenu = viewMenu->addMenu(QStringLiteral("Physics Visualization"));
    auto* vizGroup = new QActionGroup(this);
    vizGroup->setExclusive(true);
    struct VizOpt { const char* name; MpmSystem::VizMode mode; };
    const VizOpt vizOpts[] = {
        { "Default (PBR)",          MpmSystem::VizMode::Default },
        { "Thermal (temperature)",  MpmSystem::VizMode::Thermal },
        { "Stress (von Mises)",     MpmSystem::VizMode::VonMises },
        { "Strain (deformation)",   MpmSystem::VizMode::Strain },
    };
    for (const auto& o : vizOpts) {
        QAction* a = vizMenu->addAction(QString::fromLatin1(o.name));
        a->setCheckable(true);
        vizGroup->addAction(a);
        if (o.mode == MpmSystem::VizMode::Default) a->setChecked(true);
        const MpmSystem::VizMode m = o.mode;
        connect(a, &QAction::triggered, this, [this, m]() {
            if (m_renderingSystem && m_renderingSystem->getMpmSystem()) {
                m_renderingSystem->getMpmSystem()->setVizMode(m); // one-shot auto-calibrates the range
                m_renderingSystem->renderAllViewports();          // re-render NOW so the swap is instant
            }
        });
    }

    buildEngineeringToolbar();  // Phase 4: visible top toolbar (menu bar is hidden)

    // --- Simulation ---
    QMenu* simMenu = bar->addMenu(QStringLiteral("&Simulation"));
    // NOTE: Space is taken by the viewport's gizmo-mode cycling, so the
    // simulation lives on P (Play), Shift+P (stoP) and Ctrl+P (steP).
    QAction* playAct = simMenu->addAction(QStringLiteral("Play / Pause"));
    playAct->setShortcut(Qt::Key_P);
    connect(playAct, &QAction::triggered, this, [this]() {
        if (!m_simulation) return;
        if (m_simulation->state() == SimulationState::Playing) m_simulation->pause();
        else m_simulation->play();
    });
    QAction* stopAct = simMenu->addAction(QStringLiteral("Stop && Reset"));
    stopAct->setShortcut(Qt::SHIFT | Qt::Key_P);
    connect(stopAct, &QAction::triggered, this, [this]() { if (m_simulation) m_simulation->stop(); });
    QAction* stepAct = simMenu->addAction(QStringLiteral("Step One Frame"));
    stepAct->setShortcut(Qt::CTRL | Qt::Key_P);
    connect(stepAct, &QAction::triggered, this, [this]() { if (m_simulation) m_simulation->singleStep(); });
}

void MainWindow::refreshGizmoAndProperties(ViewportWidget* vp)
{
    if (!vp) vp = primaryViewport();
    if (!vp || !m_scene || !m_gizmoSystem) return;

    // Rebuild current selection
    QVector<entt::entity> selected;
    auto& reg = m_scene->getRegistry();
    for (auto eSel : reg.view<SelectedComponent>()) selected.push_back(eSel);

    // Reuse your existing slot to update gizmo + properties panel
    onSelectionChanged(selected, vp->getCamera());

    // Trigger a redraw
    vp->update();
}

// ===========================================================================
// Phase 4: engineering toolbar + CAD / material / heat-source tooling.
// The menu bar is hidden, so these tools live on a visible QToolBar.
// ===========================================================================
entt::entity MainWindow::selectedEntity() const
{
    if (!m_scene) return entt::null;
    auto& reg = m_scene->getRegistry();
    for (auto e : reg.view<SelectedComponent>()) return e;
    return entt::null;
}

void MainWindow::buildEngineeringToolbar()
{
    QToolBar* tb = addToolBar(QStringLiteral("Engineering"));
    tb->setObjectName(QStringLiteral("EngineeringToolbar"));
    tb->setMovable(false);

    connect(tb->addAction(QStringLiteral("Import CAD (STEP)")), &QAction::triggered,
            this, &MainWindow::importStepFile);
    tb->addSeparator();

    // Visualization-mode dropdown -> the Phase 3 hot-swaps.
    tb->addWidget(new QLabel(QStringLiteral(" Visualize: ")));
    QComboBox* viz = new QComboBox(tb);
    viz->addItem(QStringLiteral("PBR (Default)"),      int(MpmSystem::VizMode::Default));
    viz->addItem(QStringLiteral("Thermal"),            int(MpmSystem::VizMode::Thermal));
    viz->addItem(QStringLiteral("Stress (von Mises)"), int(MpmSystem::VizMode::VonMises));
    viz->addItem(QStringLiteral("Strain"),             int(MpmSystem::VizMode::Strain));
    connect(viz, &QComboBox::currentIndexChanged, this, [this, viz](int) {
        if (m_renderingSystem && m_renderingSystem->getMpmSystem()) {
            m_renderingSystem->getMpmSystem()->setVizMode(MpmSystem::VizMode(viz->currentData().toInt()));
            m_renderingSystem->renderAllViewports();  // re-render NOW so the viz swap is instant
        }
    });
    tb->addWidget(viz);
    tb->addSeparator();

    connect(tb->addAction(QStringLiteral("Assign Material")), &QAction::triggered,
            this, &MainWindow::assignMaterialToSelection);
    connect(tb->addAction(QStringLiteral("Add Heat Source")), &QAction::triggered,
            this, &MainWindow::addHeatSourceToSelection);
    connect(tb->addAction(QStringLiteral("Inspect")), &QAction::triggered,
            this, &MainWindow::inspectSelection);

    // Right-aligned Settings entry (the menu bar is hidden, so this toolbar IS
    // the top bar). Opens the modeless, registry-driven Settings dialog.
    auto* spacer = new QWidget(tb);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);
    connect(tb->addAction(QStringLiteral("⚙  Settings")), &QAction::triggered, this, [this] {
        static QPointer<SettingsDialog> dlg;
        if (!dlg) dlg = new SettingsDialog(this);
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    });
}

void MainWindow::importStepFile()
{
    if (!krs::cad::available()) {
        QMessageBox::information(this, QStringLiteral("Import CAD"),
            QStringLiteral("STEP import requires an OpenCASCADE build (opencascade in vcpkg.json, KR_WITH_OCCT)."));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Import STEP"),
        QString(), QStringLiteral("STEP files (*.step *.stp);;All files (*)"));
    if (path.isEmpty() || !m_scene) return;
    // Scale per the user's CAD import unit (Settings: units/cadImportUnit).
    const float metersPerUnit = float(krs::units::cadMetersPerUnit());
    krs::cad::ImportResult r = krs::cad::importStep(*m_scene, path.toStdString(), metersPerUnit);
    QMessageBox::information(this, QStringLiteral("Import CAD"),
        QString::fromStdString(r.message) +
        QStringLiteral("\nSolids: %1   Faces: %2   Attachment frames: %3\nTotal B-Rep volume: %4 m^3")
            .arg(r.solids).arg(r.faces).arg(r.attachments).arg(r.totalVolume, 0, 'g', 4));
    refreshGizmoAndProperties();
}

void MainWindow::assignMaterialToSelection()
{
    entt::entity e = selectedEntity();
    if (e == entt::null || !m_scene) {
        QMessageBox::information(this, QStringLiteral("Assign Material"), QStringLiteral("Select an entity first."));
        return;
    }
    bool ok = false;
    const QString id = QInputDialog::getText(this, QStringLiteral("Assign Material"),
        QStringLiteral("Material name or Materials Project id\n(steel, aluminium, titanium, copper, tungsten, mp-13, ...):"),
        QLineEdit::Normal, QStringLiteral("steel"), &ok);
    if (!ok || id.isEmpty()) return;
    krs::materials::MatProps m = krs::materials::query(id.toStdString());
    if (!m.valid) {
        QMessageBox::warning(this, QStringLiteral("Assign Material"),
            QStringLiteral("Unknown material. Known offline: steel, iron, aluminium, titanium, copper, "
                           "tungsten, abs, concrete. Set MP_API_KEY (+ pip install mp-api) for live mp-id lookup."));
        return;
    }
    double E = 0.0, nu = 0.0;
    krs::materials::deriveElastic(m.bulkModulus, m.shearModulus, E, nu);
    auto& reg = m_scene->getRegistry();
    auto& mat = reg.get_or_emplace<MaterialComponent>(e);
    mat.physicalName = m.name; mat.density = float(m.density);
    mat.bulkModulus = float(m.bulkModulus); mat.shearModulus = float(m.shearModulus);
    mat.youngsModulus = float(E); mat.poissonRatio = float(nu);
    mat.specificHeat = float(m.specificHeat); mat.thermalConductivity = float(m.thermalConductivity);

    double vol = mat.volume_m3;                         // OCCT sets this at import
    if (vol <= 0.0) {                                   // else integrate the triangle mesh
        if (auto* rm = reg.try_get<RenderableMeshComponent>(e)) {
            std::vector<glm::vec3> pos; pos.reserve(rm->vertices.size());
            for (const auto& v : rm->vertices) pos.push_back(v.position);
            vol = krs::materials::meshVolume(pos, rm->indices);
            if (auto* xf = reg.try_get<TransformComponent>(e))
                vol *= double(xf->scale.x) * double(xf->scale.y) * double(xf->scale.z); // local->world scale
        }
    }
    mat.volume_m3 = float(vol);
    mat.massKg = float(m.density * vol);
    QMessageBox::information(this, QStringLiteral("Material assigned"),
        QStringLiteral("%1  (%2)\nDensity: %3 kg/m^3\nBulk K: %4 GPa   Shear G: %5 GPa\n"
                       "Young E: %6 GPa   Poisson: %7\nVolume: %8 m^3   Mass: %9 kg\n"
                       "c_p: %10 J/kg.K   k: %11 W/m.K")
            .arg(QString::fromStdString(m.name), QString::fromStdString(m.source))
            .arg(m.density, 0, 'f', 1).arg(m.bulkModulus / 1e9, 0, 'f', 1).arg(m.shearModulus / 1e9, 0, 'f', 1)
            .arg(E / 1e9, 0, 'f', 1).arg(nu, 0, 'f', 3).arg(vol, 0, 'g', 4).arg(mat.massKg, 0, 'g', 4)
            .arg(m.specificHeat, 0, 'f', 0).arg(m.thermalConductivity, 0, 'f', 2));
    refreshGizmoAndProperties();
}

void MainWindow::addHeatSourceToSelection()
{
    entt::entity e = selectedEntity();
    if (e == entt::null || !m_scene) {
        QMessageBox::information(this, QStringLiteral("Add Heat Source"), QStringLiteral("Select an entity first."));
        return;
    }
    auto& reg = m_scene->getRegistry();
    auto& hs = reg.get_or_emplace<HeatSourceComponent>(e);
    bool ok = false;
    double p = QInputDialog::getDouble(this, QStringLiteral("Heat Source"),
        QStringLiteral("Heat-generation power (W) injected into the radius\n(volumetric, Neumann; 0 = colour/glow only):"),
        hs.power, 0.0, 1.0e9, 1, &ok);
    if (ok) hs.power = float(p);
    double r = QInputDialog::getDouble(this, QStringLiteral("Heat Source"),
        QStringLiteral("Influence radius (m):"), hs.radius, 0.01, 10.0, 2, &ok);
    if (ok) hs.radius = float(r);
    double t = QInputDialog::getDouble(this, QStringLiteral("Heat Source"),
        QStringLiteral("Nominal temperature (deg C) for the emissive glow:"), hs.temperature, -50.0, 2000.0, 1, &ok);
    if (ok) hs.temperature = float(t);
    refreshGizmoAndProperties();
}

void MainWindow::inspectSelection()
{
    entt::entity e = selectedEntity();
    if (e == entt::null || !m_scene) {
        QMessageBox::information(this, QStringLiteral("Inspect"), QStringLiteral("Select an entity first."));
        return;
    }
    auto& reg = m_scene->getRegistry();
    QString s = QStringLiteral("Selected entity\n\n");
    if (auto* mat = reg.try_get<MaterialComponent>(e)) {
        s += QStringLiteral("Material: %1\n  density %2 kg/m^3,  K %3 GPa,  G %4 GPa\n"
                            "  E %5 GPa,  nu %6\n  volume %7 m^3,  mass %8 kg\n"
                            "  c_p %9 J/kg.K,  k %10 W/m.K\n\n")
                 .arg(mat->physicalName.empty() ? QStringLiteral("(unassigned)") : QString::fromStdString(mat->physicalName))
                 .arg(mat->density, 0, 'f', 1).arg(mat->bulkModulus / 1e9, 0, 'f', 1).arg(mat->shearModulus / 1e9, 0, 'f', 1)
                 .arg(mat->youngsModulus / 1e9, 0, 'f', 1).arg(mat->poissonRatio, 0, 'f', 3)
                 .arg(mat->volume_m3, 0, 'g', 4).arg(mat->massKg, 0, 'g', 4)
                 .arg(mat->specificHeat, 0, 'f', 0).arg(mat->thermalConductivity, 0, 'f', 2);
    } else s += QStringLiteral("Material: (none)\n\n");
    if (auto* hs = reg.try_get<HeatSourceComponent>(e))
        s += QStringLiteral("Heat source: %1 W, %2 deg C nominal, radius %3 m (%4)\n")
                 .arg(hs->power, 0, 'f', 1).arg(hs->temperature, 0, 'f', 0).arg(hs->radius, 0, 'f', 2)
                 .arg(hs->active ? QStringLiteral("active") : QStringLiteral("inactive"));
    else s += QStringLiteral("Heat source: (none)\n");
    if (auto* att = reg.try_get<AttachmentComponent>(e))
        s += QStringLiteral("\nCAD attachment frames: %1 cylindrical feature(s)").arg(int(att->frames.size()));
    QMessageBox::information(this, QStringLiteral("Inspect"), s);
}
