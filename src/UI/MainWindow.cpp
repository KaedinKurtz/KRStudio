#include "MainWindow.hpp"
#include "StaticToolbar.hpp"
#include "PropertiesPanel.hpp"
#include "Scene.hpp"
#include "ViewportWidget.hpp"
#include "components.hpp" 
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
#include "FlowVisualizerMenu.hpp"
#include "Helpers.hpp"   
#include "entt/entt.hpp"
#include "RealSenseConfigMenu.hpp"
#include "SlamManager.hpp"
#include "CustomDataFlowScene.hpp"
#include "ExecutionControlWidget.hpp"
#include "DatabasePanel.hpp"
#include "DatabaseManager.hpp"
#include "ViewportManagerPopup.hpp"
#include "MaterialLoader.hpp"
#include "MeshUtils.hpp"
#include "ResourceManager.hpp"
#include "GizmoSystem.hpp"

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

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
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
#include <DockAreaTitleBar.h> 
#include <QRandomGenerator>
#include <QColor>
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

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
    m_colorInUse(kPalette.size(), false)
{
    setNodeEditorStyle();
    // --- 1. Create the Scene and Initial Entities ---
    m_scene = std::make_unique<Scene>();
    auto& registry = m_scene->getRegistry();

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

    {
        QString assetDir = QCoreApplication::applicationDirPath() + QLatin1String("/../assets/");
        QString lamboPath = assetDir + "lambo.stl";

        // 1) Load once -> stable MeshID
        MeshID lamboId = ResourceManager::instance().loadMesh(lamboPath);
        if (lamboId == MeshID::None) {
            qWarning() << "[Spawner] Failed to load mesh:" << lamboPath;
            return;
        }

        auto& registry = m_scene->getRegistry();

        // --- A) SINGLE INSTANCE ---
        TransformComponent t;
        t.translation = glm::vec3(-1.0f, -5.0f, -1.0f);
        t.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // identity (w,x,y,z)
        t.scale = glm::vec3(1.0f);

        entt::entity e = SceneBuilder::spawnMeshInstance(*m_scene, lamboId, t);
        if (registry.valid(e)) {
            registry.emplace_or_replace<MaterialDirectoryTag>(e, "D:/Textures/Blender/synthetic-bl/carbon-fiber-smooth-bl");
            registry.emplace_or_replace<ParallaxMaterialTag>(e);
            // SceneBuilder already tags UV vs TriPlanar for you; add TriPlanar here only if you want to force it.

            auto& mat = registry.emplace_or_replace<MaterialComponent>(e);
            mat.heightScale = 0.1f;
        }

        // --- B) GRID OF INSTANCES ---
        std::vector<entt::entity> ents =
            SceneBuilder::spawnMeshInstanceRandom(*m_scene, lamboId, /*countX*/4, /*minBounds*/glm::vec3(-10), /*MaxBounds*/glm::vec3(10));

        for (entt::entity ge : ents) {
            if (!registry.valid(ge)) continue;

            registry.emplace_or_replace<MaterialDirectoryTag>(ge, "D:/Textures/Blender/synthetic-bl/carbon-fiber-smooth-bl");
            registry.emplace_or_replace<ParallaxMaterialTag>(ge);

            auto& mat = registry.emplace_or_replace<MaterialComponent>(ge);
            mat.heightScale = 0.1f;
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

    // Grab the toolbar’s embedded popup
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
        mainSplitter->setSizes({ 1, 2, 1 });
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
    auto* containerLayout = new QHBoxLayout(nodeEditorContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->addWidget(splitter);
    nodeEditorContainer->setLayout(containerLayout);
    auto* combinedNodeDock = new ads::CDockWidget("Node Editor");
    combinedNodeDock->setWidget(nodeEditorContainer);
    combinedNodeDock->setStyleSheet(sidePanelStyle);
    m_dockManager->addDockWidget(ads::RightDockWidgetArea, combinedNodeDock, m_propertiesArea);

    connect(graphModel.get(), &QtNodes::AbstractGraphModel::nodeCreated,
        this, [this, graphModel](QtNodes::NodeId nodeId) {
            auto* delegate = graphModel->delegateModel<NodeDelegate>(nodeId);
            if (!delegate) return;
            auto backendNodePtr = NodeFactory::instance().createNode(delegate->name().toStdString());
            if (!backendNodePtr) return;
            delegate->setBackendNode(std::move(backendNodePtr));
            Q_EMIT graphModel->nodeUpdated(nodeId);
        });

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
    m_masterRenderTimer = new QTimer(this);
    connect(m_masterRenderTimer, &QTimer::timeout, this, [this]() {
        // The new render loop is just one simple call.
        if (m_renderingSystem) {
            m_renderingSystem->renderFrame();
        }
        });

    m_renderingSystem->initialize(m_scene.get());
    m_masterRenderTimer->start(0); // Start rendering as fast as possible.


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

    m_gizmoSystem->onAfterCommandApplied = [this] {
        this->refreshGizmoAndProperties(); // picks primary viewport automatically
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

    // 2) Drop in a dummy dock so ADS will create a CDockAreaWidget for us…
    auto* placeholder = new ads::CDockWidget(QString(), this);
    placeholder->setFeatures(ads::CDockWidget::NoDockWidgetFeatures);
    m_propertiesArea = m_dockManager->addDockWidget(
        ads::RightDockWidgetArea,
        placeholder
    );

    // 3) Immediately remove the dummy—area remains behind, ready for real tabs
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
                // When the properties panel is created, connect it to ALL existing viewports.
                for (auto* dock : m_dockContainers) {
                    if (auto* viewport = qobject_cast<ViewportWidget*>(dock->widget())) {
                        // This connection is now valid because the slot and signal match via the onSelectionChanged hub
                        // We will connect it in the onSelectionChanged function instead.
                    }
                }
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
            });

        if (db::DatabaseManager::instance().menuConfigExists(title))
            entry.menu->initializeFromDatabase();
        else
            entry.menu->initializeFresh();
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
