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

#include <QtNodes/NodeDelegateModelRegistry>
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/DataFlowGraphicsScene>
#include <QtNodes/GraphicsView>
#include <QtNodes/NodeData>
#include <QtNodes/ConnectionStyle>
#include <QtNodes/NodeStyle>
#include <QtNodes/GraphicsViewStyle>

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

    // --- Create a single test cube in the center of the scene ---
    {
        auto cubeEntity = registry.create();
        registry.emplace<TagComponent>(cubeEntity, "Test Cube");
        registry.emplace<TransformComponent>(cubeEntity).translation = glm::vec3(0.0f, 0.5f, 0.0f);
        registry.emplace<BoundingBoxComponent>(cubeEntity);
        registry.emplace<FieldSourceTag>(cubeEntity);

        auto& pointEffector = registry.emplace<MeshEffectorComponent>(cubeEntity);
        pointEffector.strength = -1.0f;
        pointEffector.distance = 3.0f;

        auto& mesh = registry.emplace<RenderableMeshComponent>(cubeEntity);
        const std::vector<float>& raw = Mesh::getLitCubeVertices();
        constexpr std::size_t stride = 6;
        mesh.vertices.reserve(raw.size() / stride);
        for (std::size_t i = 0; i < raw.size(); i += stride)
        {
            glm::vec3 pos{ raw[i],     raw[i + 1], raw[i + 2] };
            glm::vec3 normal{ raw[i + 3],   raw[i + 4], raw[i + 5] };
            mesh.vertices.emplace_back(pos, normal);
        }
        mesh.indices = Mesh::getLitCubeIndices();
    }

    // --- Create Splines ---
    {
        using SB = SceneBuilder;
        auto& reg = m_scene->getRegistry();
        auto CREntity = SB::makeCR(reg, { {-2,0,-1}, {-2,0, 1}, { 2,0, 1}, { 2,0,-1} }, { 0.9f,0.9f,0.9f,1 }, { 0.9f,0.3f,0.3f,1 }, 18.0f);
        reg.emplace<PulsingSplineTag>(CREntity);
        auto ParamEntity = SB::makeParam(reg, [](float t) { return glm::vec3(2 * std::cos(6.28f * 3 * t), 6 * t, 2 * std::sin(6.28f * 3 * t)); }, { 0.9f,0.9f,0.9f,1 }, { 0.2f,0.6f,1.0f,1 }, 12.0f);
        reg.emplace<PulsingSplineTag>(ParamEntity);
        auto LinearEntity = SB::makeLinear(reg, { {-5, 0.1, -5}, {-5, 2, -5}, {0, 2, -5}, {0, 0.1, -5} }, { 0.9f,0.9f,0.9f,1 }, { 0.1f, 1.0f, 0.2f, 1 }, 18.0f);
        reg.emplace<PulsingSplineTag>(LinearEntity);
        auto bezierEntity = SB::makeBezier(reg, { {5, 0.1, -5}, {5, 4, -5}, {2, 4, -5}, {2, 0.1, -5} }, { 0.9f,0.9f,0.9f,1 }, { 1.0f, 0.9f, 0.2f, 1 }, 18.0f);
        reg.emplace<PulsingSplineTag>(bezierEntity);
        // auto& splineEffector = reg.emplace<SplineEffectorComponent>(bezierEntity);
        // splineEffector.strength = -1.0f; // Example: make it an attractor
        // splineEffector.radius = 02.00f;
    }

    // --- Create Field Visualizers and Effectors ---
    {
        auto& reg = m_scene->getRegistry();
        auto visualizerEntity = reg.create();
        reg.emplace<TagComponent>(visualizerEntity, "Field Visualizer");
        reg.emplace<TransformComponent>(visualizerEntity);
        auto& visualizer = reg.emplace<FieldVisualizerComponent>(visualizerEntity);
        visualizer.bounds = { glm::vec3(-5.0f, -2.5f, -5.0f), glm::vec3(10.0f, 2.5f, 5.0f) };
        visualizer.displayMode = FieldVisualizerComponent::DisplayMode::Arrows;
        auto& registry = m_scene->getRegistry();
        QTimer::singleShot(0, this, [this, &registry] {
            auto view = registry.view<FieldVisualizerComponent>();
            if (!view.empty())
                m_flowVisualizerMenu->updateControlsFromComponent(
                    firstComponent<FieldVisualizerComponent>(registry));
            });
        // Set properties on the correct sub-struct

        visualizer.arrowSettings.density = { 15, 5, 15 };
        visualizer.arrowSettings.vectorScale = 0.5f;
        visualizer.arrowSettings.headScale = 0.4f;
        visualizer.arrowSettings.intensityMultiplier = 1.0f;
        visualizer.arrowSettings.cullingThreshold = 0.01f;
        visualizer.arrowSettings.coloringMode = FieldVisualizerComponent::ColoringMode::Intensity;
        visualizer.bounds.max = { 4, 2, 4 };
        visualizer.bounds.min = { -4, -2, -4 };
        visualizer.flowSettings.particleCount = 5000;
        visualizer.flowSettings.baseSpeed = 0.15f;
        visualizer.flowSettings.baseSize = 0.30f; // Was flowScale
        visualizer.flowSettings.randomWalkStrength = 0.1f; // Was flowRandomWalk
        visualizer.flowSettings.lifetime = 7.0f;

        auto windSource = reg.create();
        reg.emplace<TagComponent>(windSource, "Wind Source");
        reg.emplace<TransformComponent>(windSource);
        reg.emplace<FieldSourceTag>(windSource);
        auto& directional = reg.emplace<DirectionalEffectorComponent>(windSource);
        directional.direction = { 1.0f, 0.0f, 0.50f };
        directional.strength = 0.60f;


        auto repulsorSource = reg.create();
        reg.emplace<TagComponent>(repulsorSource, "Repulsor");
        auto& repulsorTransform = reg.emplace<TransformComponent>(repulsorSource);
        repulsorTransform.translation = { 5.0f, 1.0f, 0.0f };
        reg.emplace<FieldSourceTag>(repulsorSource);
        auto& point = reg.emplace<PointEffectorComponent>(repulsorSource);
        point.strength = 10.0f;
        point.radius = 4.0f;
        point.falloff = PointEffectorComponent::FalloffType::Linear;
    }

    // --- Create initial cameras for the viewports ---
    const QColor& c0 = kPalette[0];
    glm::vec3 tint0{ c0.redF(), c0.greenF(), c0.blueF() };

    auto cameraEntity1 = SceneBuilder::createCamera(
        registry,
        { 0, 2, 5 },
        tint0
    );

    // mark slot 0 taken
    m_colorInUse[0] = true;
    registry.emplace<ColorIndexComponent>(cameraEntity1, 0);

    // --- 2. Create the SINGLE Shared Rendering System ---
    m_renderingSystem = std::make_unique<RenderingSystem>(nullptr);

    // ---------------------------------------------------------------------------
// 3.  Core UI layout & dock setup – NO FLOATING VIEWPORTS
// ---------------------------------------------------------------------------
    m_centralContainer = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(m_centralContainer);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // fixed toolbar
    m_fixedTopToolbar = new StaticToolbar(this);
    mainLayout->addWidget(m_fixedTopToolbar, 0);

    // 3a) Grab the toolbar’s embedded popup:
    auto* popup = m_fixedTopToolbar->viewportManagerPopup();
    m_viewportManagerPopup = popup;
    // 3b) Wire up its buttons ? your slots:
    connect(popup, &ViewportManagerPopup::addViewportRequested, this, &MainWindow::addViewport);
    connect(popup, &ViewportManagerPopup::removeViewportRequested, this, &MainWindow::removeViewport);
    connect(popup, &ViewportManagerPopup::resetViewportsRequested, this, &MainWindow::onResetViewports);
    connect(popup, &ViewportManagerPopup::showViewportRequested, this, &MainWindow::onShowViewportRequested);

    // 3c) Also re?sync the initial state of the menu:
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

    //  ?? 3.  add the dock *first* – now it really belongs to an area ??????????
    auto* viewportArea1 =
        m_dockManager->addDockWidget(ads::LeftDockWidgetArea, viewportDock1);

    //  ?? 4.  colour the title?bar ?????????????????????????????????????????????
    if (auto* area = viewportDock1->dockAreaWidget()) {
        if (auto* tb = area->titleBar()) {
            // ?A. fetch the camera’s tint  (01 floats)
            const glm::vec3 camTint =
                m_scene->getRegistry()
                .get<CameraComponent>(cameraEntity1).tint;

            // ?B. make it 20?% darker for the bar
            glm::vec3 darkTint = camTint * 0.8f;

            QColor bg; bg.setRgbF(darkTint.r, darkTint.g, darkTint.b);
            QColor fg = Qt::white;                     // force white text

            // ?C. remove borders + apply colours
            tb->setStyleSheet(QStringLiteral(
                "background:%1;"
                "color:%2;"
                "border:0;"
            ).arg(bg.name(), fg.name()));
        }
    }

    // ---------------------------------------------------------------------------
    // keep track of the docks so your later loops still work
    // ---------------------------------------------------------------------------
    m_dockContainers << viewportDock1;

    applyCameraColorToDock(viewportDock1, cameraEntity1);

    connect(viewportDock1, &ads::CDockWidget::closed, this, [this, viewportDock1]() {
        // 1) destroy the camera/rig
        if (auto* vp = qobject_cast<ViewportWidget*>(viewportDock1->widget())) {
            destroyCameraRig(vp->getCameraEntity());
        }
        // 2) once Qt has actually deleted the widget, remove it from our list and refresh
        QTimer::singleShot(0, this, [this, viewportDock1]() {
            m_dockContainers.removeAll(viewportDock1);
            syncViewportManagerPopup();
            });
        });

    // Create the properties panel and dock it to the RIGHT of the SECOND viewport.
    PropertiesPanel* propertiesPanel = new PropertiesPanel(m_scene.get(), this); // Creates the properties panel widget.
    propertiesPanel->setMinimumWidth(700); // Sets the minimum width of the properties panel. The dock widget will respect this.
    ads::CDockWidget* propertiesDock = new ads::CDockWidget("Grid(s)"); // Creates the properties dock widget.
    propertiesDock->setWidget(propertiesPanel); // Sets the properties panel as the content of the dock widget.
    propertiesDock->setStyleSheet(sidePanelStyle); // Applies your custom style.
    ads::CDockAreaWidget* propertiesArea = m_dockManager->addDockWidget(ads::RightDockWidgetArea, propertiesDock); // Docks the properties panel to the right OF viewport 2, creating our third column.

    // Create the database panel and dock it to the right
    DatabasePanel* databasePanel = new DatabasePanel(m_scene.get(), this);
    ads::CDockWidget* databaseDock = new ads::CDockWidget("Database");
    databaseDock->setWidget(databasePanel);
    databaseDock->setStyleSheet(sidePanelStyle);
    m_dockManager->addDockWidget(ads::RightDockWidgetArea, databaseDock, propertiesArea);

    // Connect the database panel's scene reload signal
    connect(databasePanel, &DatabasePanel::requestSceneReload, this, &MainWindow::onSceneReloadRequested);

    // --- Set Proportions for the three main dock areas ---

    ads::CDockSplitter* mainSplitter = viewportArea1->parentSplitter(); // Gets the custom splitter containing our dock areas.
    if (mainSplitter) // Checks if the splitter is valid.
    {
        // We give it a list of integers representing the initial sizes for a 25:50:25 ratio.
        mainSplitter->setSizes({ 1, 2, 1 });
    }

    // Create the flow visualizer menu and add it as a TAB to the properties area.
    m_flowVisualizerMenu = new FlowVisualizerMenu(this); // Creates the flow visualizer menu widget.
    ads::CDockWidget* flowMenuDock = new ads::CDockWidget("Field Visualizer"); // Creates the flow visualizer dock widget.
    m_flowVisualizerMenu->setMinimumWidth(650); // Sets the minimum width for this widget as well.
    flowMenuDock->setWidget(m_flowVisualizerMenu); // Sets the menu as the content of the dock widget.
    flowMenuDock->setStyleSheet(sidePanelStyle); // Applies your custom style.
    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, flowMenuDock, propertiesArea); // Adds the flow menu as a tab in the properties dock area.

    // ===================================================================
    // --- Add the RealSense Configuration Menu ---
    // ===================================================================
    RealSenseConfigMenu* realSenseMenu = new RealSenseConfigMenu(this); // Creates the menu widget.
    ads::CDockWidget* realSenseDock = new ads::CDockWidget("RealSense Camera"); // Creates the dock container.
    realSenseMenu->setMinimumWidth(650); // Ensures consistent width with other panels.
    realSenseDock->setWidget(realSenseMenu); // Sets the menu as the content of the dock widget.
    realSenseDock->setStyleSheet(sidePanelStyle); // Applies your existing custom style.

    // Adds the RealSense menu as a new tab in the same properties dock area.
    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, realSenseDock, propertiesArea);
    // ===================================================================


// ===================================================================
// --- Node Editor Setup (with combined Catalog and Editor) ---
// ===================================================================

// --- 1. Create the backend model and scene (no changes here) ---
    auto nodeRegistry = std::make_shared<QtNodes::NodeDelegateModelRegistry>(); // The registry for all node types.

    for (auto const& [typeId, desc] : NodeFactory::instance().getRegisteredNodeTypes()) // Loop through all nodes from your factory.
    {
        nodeRegistry->registerModel<NodeDelegate>( // Register a delegate model for each node type.
            [typeId]() { // A lambda that creates a delegate instance.
                return std::make_unique<NodeDelegate>(typeId); // Create a delegate with its unique type ID.
            },
            QString::fromStdString(desc.category) // Assign the node to its correct category in the context menu.
        );
    }

    auto graphModel = std::make_shared<QtNodes::DataFlowGraphModel>(nodeRegistry); // The model that holds the graph data.
    m_nodeScene = new CustomDataFlowScene(*graphModel, this); // Use your custom scene class.
    m_nodeView = new DroppableGraphicsView(m_nodeScene, this); // Your custom view that handles dropping nodes.

    // --- 2. Create the combined editor widget ---
    auto* nodeEditorContainer = new QWidget(); // This is the main widget that will hold everything.
    auto* splitter = new QSplitter(Qt::Horizontal, nodeEditorContainer); // The splitter that creates the left/right columns.

    // --- 3. Create and configure the Node Catalog (Left Side) ---
    NodeCatalogWidget* catalog = new NodeCatalogWidget(splitter); // Create the catalog widget as a child of the splitter.
    catalog->setMaximumWidth(300); // Enforce the maximum width of 200 pixels on the catalog.
    catalog->setMinimumWidth(200);
    splitter->addWidget(catalog); // Add the catalog to the left side of the splitter (index 0).

    // --- 4. Configure the Node Editor View (Right Side) ---
    splitter->addWidget(m_nodeView); // Add the existing node editor view to the right side of the splitter (index 1).

    // --- 5. Set the splitter's size ratio ---
    splitter->setStretchFactor(0, 1); // Set the stretch factor for the left widget (catalog, index 0) to 1.
    splitter->setStretchFactor(1, 3); // Set the stretch factor for the right widget (editor, index 1) to 5.

    // --- 6. Set up the layout for the container ---
    auto* containerLayout = new QHBoxLayout(nodeEditorContainer); // Create a horizontal layout for the container.
    containerLayout->setContentsMargins(0, 0, 0, 0); // Remove any extra padding around the layout.
    containerLayout->addWidget(splitter); // Place the splitter into the layout.
    nodeEditorContainer->setLayout(containerLayout); // Apply the layout to our main container widget.

    // --- 7. Create and add the final dock widget ---
    auto* combinedNodeDock = new ads::CDockWidget("Node Editor"); // Create a single dock widget for the combined editor.
    combinedNodeDock->setWidget(nodeEditorContainer); // Place our container widget (with the splitter) inside the dock.
    combinedNodeDock->setStyleSheet(sidePanelStyle); // Apply your custom styling.
    m_dockManager->addDockWidget(ads::RightDockWidgetArea, combinedNodeDock, propertiesArea); // Add the new dock to the right of the properties panel.

    // --- 8. Connect the node creation signal (no changes here) ---
    connect(graphModel.get(), &QtNodes::AbstractGraphModel::nodeCreated,
        this, [this, graphModel](QtNodes::NodeId nodeId) {

            // Get the delegate and attach the backend.
            auto* delegate = graphModel->delegateModel<NodeDelegate>(nodeId);
            if (!delegate) return;

            auto backendNodePtr = NodeFactory::instance().createNode(delegate->name().toStdString());
            if (!backendNodePtr) return;

            delegate->setBackendNode(std::move(backendNodePtr));

            // Emit the signal that tells the scene to update itself. This will
            // trigger calls to recomputeSize() and embeddedWidget(),
            // correctly creating and displaying your controls.
            Q_EMIT graphModel->nodeUpdated(nodeId);
        });

    connect(m_dockManager,
        &ads::CDockManager::dockWidgetRemoved,
        this,
        &MainWindow::syncViewportManagerPopup);

    // ===================================================================

    //========================= Start the SLAM Manager =========================
    // 1. Create the manager instances.
    // SlamManager is a QObject, so we give it a parent for memory management.
    m_slamManager = new SlamManager(this);
    // RealSenseManager is not a QObject, so we use a unique_ptr for lifetime management.
    m_realSenseManager = std::make_unique<RealSenseManager>();

    // 2. Give the SLAM manager a pointer to the rendering system.
    // This allows the SLAM backend to directly update the point cloud visualization.
    m_slamManager->setRenderingSystem(m_renderingSystem.get());

    // 3. Set up the polling timer for the RealSense camera.
    // The RealSense SDK is configured to be polled for new frames rather than using callbacks.
    m_rsPollTimer = new QTimer(this); // Parented to MainWindow for memory management.
    m_rsPollTimer->setInterval(33);   // Poll at ~30 FPS, matching typical depth camera rates.

    // 4. Create the point cloud calculation object from the RealSense SDK.
    m_pointCloud = std::make_unique<rs2::pointcloud>();

    // 5. Connect signals and slots for the data pipeline.
    // A. Connect the polling timer's timeout to a lambda that gets and processes frames.
    connect(m_rsPollTimer, &QTimer::timeout, this, [this]() {
        rs2::frameset frames;
        // Poll the pipeline for a new set of frames; this is a non-blocking call.
        if (m_realSenseManager->pollFrames(frames)) {
            auto depth = frames.get_depth_frame(); // Get the depth frame from the set.
            auto color = frames.get_color_frame(); // Get the color frame from the set.

            if (depth && color) {
                // Align the point cloud to the color frame's perspective.
                m_pointCloud->map_to(color);
                // Generate the point cloud vertices from the depth data.
                rs2::points points = m_pointCloud->calculate(depth);

                // Forward the processed data to the SLAM system. This is the entry point
                // that sends data to the frontend thread for tracking and mapping.
                // This slot also forwards the point cloud to the RenderingSystem for visualization.
                m_slamManager->onPointCloudReady(points, color);
            }
        }
        });

    // B. Connect the UI "Start" button to actually starting the physical camera.
    // Note: Assumes 'realSenseMenu' emits 'startClicked' and 'stopClicked' signals.
    connect(realSenseMenu,
        &RealSenseConfigMenu::startStreamingRequested,
        this,
        [this](const std::string& serial,
            const std::vector<StreamProfile>& profiles) {
                if (m_realSenseManager->startStreaming(serial, profiles)) {
                    statusBar()->showMessage("Streaming started");
                    m_rsPollTimer->start();
                }
                else {
                    QMessageBox::critical(this, "Error", "Failed to start stream.");
                }
        });

    // C. Connect the UI "Stop" button to stopping the camera and the polling timer.
    connect(realSenseMenu, &RealSenseConfigMenu::onStopStreamingClicked, this, [this]() {
        m_rsPollTimer->stop(); // Stop asking for new frames.
        m_realSenseManager->stopStreaming(); // Tell the camera to stop sending data.
        statusBar()->showMessage("RealSense streaming stopped.");
        });

    // 6. Start the SLAM system's background threads (Frontend/Backend).
    // These threads will now wait for data to be processed by the connect() statements above.
    m_slamManager->start();

    // Connect the SLAM manager to the RealSense menu for point cloud updates
    //===========================================================================


    connect(m_flowVisualizerMenu, &FlowVisualizerMenu::settingsChanged,
        this, &MainWindow::onFlowVisualizerSettingsChanged);


    connect(m_flowVisualizerMenu, &FlowVisualizerMenu::settingsChanged,
        this, &MainWindow::onFlowVisualizerSettingsChanged);

    connect(m_flowVisualizerMenu, &FlowVisualizerMenu::transformChanged,
        this, &MainWindow::onFlowVisualizerTransformChanged);

    // --- 5. FINAL, ROBUST INITIALIZATION AND RENDER LOOP START ---

    // 1. Create the timer, but DO NOT START IT YET.
    m_masterRenderTimer = new QTimer(this);
    connect(m_masterRenderTimer, &QTimer::timeout, this, &MainWindow::onMasterRender);

    // The glContextReady connection for the first viewport to initialize the renderer
    connect(viewport1, &ViewportWidget::glContextReady, this, [this, viewport1]() {
        if (!m_renderingSystem->isInitialized()) {
            qDebug() << "[LIFECYCLE] Primary viewport context is ready. Initializing RenderingSystem.";
            viewport1->makeCurrent();
            auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(viewport1->context());
            if (gl) {
                m_renderingSystem->initializeResourcesForContext(gl, m_scene.get());
            }
            else {
                qFatal("Could not get GL functions to initialize RenderingSystem.");
            }
            viewport1->doneCurrent();
            qDebug() << "[LIFECYCLE] RenderingSystem is initialized. Starting master render timer.";
            m_masterRenderTimer->start(16);
        }
        });


    // --- Connect Signals for Updating Viewport Layouts ---
    // We connect to signals that tell us when the user might have changed
    // which tab is visible or moved a dock widget.
    connect(m_dockManager, &ads::CDockManager::focusedDockWidgetChanged, this, &MainWindow::updateViewportLayouts);

    // We still need topLevelChanged for each dock widget to handle docking/undocking
    connect(viewportDock1, &ads::CDockWidget::topLevelChanged, this, &MainWindow::updateViewportLayouts);

    // --- 6. Other Signal/Slot Connections ---
    connect(m_fixedTopToolbar, &StaticToolbar::loadRobotClicked, this, &MainWindow::onLoadRobotClicked);
    connect(viewportDock1, &ads::CDockWidget::topLevelChanged, this, [viewport1, viewportDock1](bool isFloating) {
        if (isFloating) {
            // The widget has just been undocked.
            // Schedule a hide/show cycle on the dock widget itself to force a full refresh.
            QTimer::singleShot(10, viewportDock1, [viewportDock1]() {
                viewportDock1->hide();
                viewportDock1->show();
                });
        }
        else {
            // When the widget is redocked...
            QTimer::singleShot(0, viewport1, [=]() { // Use a default capture [=]
                viewport1->update();
                });
        }
        });

    connect(m_flowVisualizerMenu, &FlowVisualizerMenu::settingsChanged, this, &MainWindow::onFlowVisualizerSettingsChanged);
    connect(m_flowVisualizerMenu, &FlowVisualizerMenu::testViewportRequested, this, &MainWindow::onTestNewViewport);

    syncViewportManagerPopup();
    updateVisualizerUI();
    onFlowVisualizerSettingsChanged();

    // --- 7. Final Window Setup ---
    if (menuBar()) {
        menuBar()->setVisible(false);
    }
    resize(1600, 900);
    setWindowTitle("KR Studio ALPHA V0.602");
    setWindowIcon(QIcon(":/icons/kRLogoSquare.png"));
    statusBar()->showMessage("Ready.");

    disableFloatingForAllDockWidgets();
    QTimer::singleShot(0, this, &MainWindow::updateViewportLayouts);
}

void MainWindow::onMasterRender()
{
    // --- 1. LOGIC UPDATES (No context needed) ---
    if (m_renderingSystem && m_renderingSystem->isInitialized())
    {
        const float deltaTime = static_cast<float>(m_masterRenderTimer->interval()) / 1000.0f;

        // --- FIX ---
        // These functions no longer take the registry as an argument.
        m_renderingSystem->updateSceneLogic(deltaTime);
        m_renderingSystem->updateCameraTransforms();

        // This function is still correct as it's a static method on ViewportWidget.
        ViewportWidget::propagateTransforms(m_scene->getRegistry());
    }

    // --- 2. SCHEDULE REPAINT (The Qt Way) ---
    for (ads::CDockWidget* dockWidget : m_dockManager->dockWidgetsMap())
    {
        ViewportWidget* vp = qobject_cast<ViewportWidget*>(dockWidget->widget());
        if (vp) {
            vp->update();
        }
    }
}
// The destructor orchestrates a clean shutdown.
MainWindow::~MainWindow()
{
         // Stop any ongoing render loops
        m_masterRenderTimer->stop();
    
             // 1) Remove every viewport dock. This triggers onViewportDockClosed()
             //    for each, which in turn calls destroyCameraRig() and GPU cleanup.
        while (!m_dockContainers.isEmpty()) {
        m_dockManager->removeDockWidget(m_dockContainers.last());
        
    }
    
             // 2) (Optional) If you still have any shared GPU resources left,
             //    shut them down now using a valid OpenGL context:
        if (auto* ctx = QOpenGLContext::currentContext()) {
        if (auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(ctx)) {
            m_renderingSystem->shutdown(gl);
            
        }
        
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
    entt::entity cam = SceneBuilder::createCamera(
        reg,
        { 0.f, 2.f, 5.f + float(m_dockContainers.size()) },
        tint
    );
    reg.emplace<ColorIndexComponent>(cam, colorIdx);

    // 5) make the viewport + dock it
    auto* vp = new ViewportWidget(m_scene.get(), m_renderingSystem.get(), cam, this);
    auto* dock = new ads::CDockWidget("");
    dock->setWidget(vp);
    m_dockContainers.append(dock);

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