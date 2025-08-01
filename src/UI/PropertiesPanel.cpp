#include "PropertiesPanel.hpp"
#include "ui_propertiespanel.h"
#include "Scene.hpp"
#include "components.hpp"
#include "gridPropertiesWidget.hpp"
#include <QAbstractButton>
#include <entt/entity/storage.hpp>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

#include "DatabaseManager.hpp"

PropertiesPanel::PropertiesPanel(Scene* scene, QWidget* parent) :
    QWidget(parent),
    ui(std::make_unique<Ui::PropertiesPanel>()),
    m_scene(scene)
{
    m_scene->getRegistry()
         .storage<GridComponent>()
         .reserve(1);

    ui->setupUi(this);
    m_gridLayout = ui->gridLayout;

    // This is your original, correct signal/slot connection.
    m_scene->getRegistry().on_construct<GridComponent>().connect<&PropertiesPanel::onGridAdded>(this);
    m_scene->getRegistry().on_destroy<GridComponent>().connect<&PropertiesPanel::onGridRemoved>(this);

    connect(ui->addGridButton, &QAbstractButton::clicked, this, [this]() {
        auto& registry = m_scene->getRegistry();
        auto newGrid = registry.create();
        registry.emplace<TransformComponent>(newGrid);
        registry.emplace<GridComponent>(newGrid);
        });

    // This loop correctly handles grids that already exist when the panel is created.
    auto initialGridsView = m_scene->getRegistry().view<GridComponent>();
    for (auto entity : initialGridsView)
    {
        onGridAdded(m_scene->getRegistry(), entity);
    }
}

PropertiesPanel::~PropertiesPanel()
{
    disconnectRegistry();
}

void PropertiesPanel::initializeFresh()
{
    auto& reg = m_scene->getRegistry();
    reg.storage<GridComponent>().reserve(1);

    // create one grid if none exist
    auto view = reg.view<GridComponent>();
    if (view.begin() == view.end()) {
        auto e = reg.create();
        reg.emplace<TransformComponent>(e);
        reg.emplace<GridComponent>(e);
    }

    // now drive the UI via your existing onGridAdded hook:
    for (auto e : reg.view<GridComponent>())
        onGridAdded(reg, e);
}

void PropertiesPanel::onGridRemoved(entt::registry& registry, entt::entity entity)
{
    if (m_entityWidgetMap.count(entity)) {
        QWidget* widget = m_entityWidgetMap[entity];
        m_entityWidgetMap.erase(entity);
        m_gridLayout->removeWidget(widget);
        widget->deleteLater();
    }
}

void PropertiesPanel::ensureGridIsInitialized(entt::entity gridEntity)
{
    auto& registry = m_scene->getRegistry();
    if (!registry.valid(gridEntity)) return;

    if (!registry.all_of<TagComponent>(gridEntity)) {
        static int gridCount = 1;
        registry.emplace<TagComponent>(gridEntity, "Grid " + std::to_string(gridCount++));
    }

    auto& gridComp = registry.get<GridComponent>(gridEntity);
    if (gridComp.levels.empty()) {
        // FINAL FIX: Use parentheses () for the glm::vec3 constructor, not curly braces {}.
        gridComp.levels.emplace_back(0.001f, glm::vec3(0.5f, 0.5f, 0.5f), 0.0f, 0.5f);
        gridComp.levels.emplace_back(0.01f, glm::vec3(0.5f, 0.5f, 0.5f), 0.5f, 2.0f);
        gridComp.levels.emplace_back(0.1f, glm::vec3(0.5f, 0.5f, 0.5f), 2.0f, 10.0f);
        gridComp.levels.emplace_back(1.0f, glm::vec3(0.5f, 0.5f, 0.5f), 10.0f, 50.0f);
        gridComp.levels.emplace_back(10.0f, glm::vec3(0.5f, 0.5f, 0.5f), 50.0f, 200.0f);
    }
}

// This function correctly calls the two helper methods.
void PropertiesPanel::onGridAdded(entt::registry& registry, entt::entity entity)
{
    ensureGridIsInitialized(entity);
    addGridEditor(entity);
}

// This function remains unchanged.
void PropertiesPanel::addGridEditor(entt::entity entity)
{
    if (m_entityWidgetMap.find(entity) != m_entityWidgetMap.end()) return;
    
        auto* widget = new gridPropertiesWidget(m_scene, entity, this);
    m_gridLayout->addWidget(widget);
    m_entityWidgetMap[entity] = widget;
}

void PropertiesPanel::clearAllGrids()
{
    auto& registry = m_scene->getRegistry();
    // destroy every grid entity
    std::vector<entt::entity> toDestroy;
    for (auto ent : registry.view<GridComponent>())
        toDestroy.push_back(ent);

    for (auto ent : toDestroy)
        if (registry.valid(ent))
            registry.destroy(ent);
}

 QJsonArray PropertiesPanel::toJsonColor(const glm::vec3& c) {
     QJsonArray a;
     a.append(c.r);
     a.append(c.g);
     a.append(c.b);
     return a;
 }

 // helper: JSON array  glm color
 glm::vec3 PropertiesPanel::fromJsonColor(const QJsonArray& a) {
     return { float(a[0].toDouble()),
              float(a[1].toDouble()),
              float(a[2].toDouble()) };
 }


 void PropertiesPanel::shutdownAndSave()
 {
     // --- build JSON ---
     QJsonArray gridsArr;
     auto& reg = m_scene->getRegistry();

     for (auto ent : reg.view<GridComponent>())
     {
         QJsonObject obj;
         obj["tag"] = QString::fromStdString(reg.get<TagComponent>(ent).tag);
         const auto& gc = reg.get<GridComponent>(ent);
         obj["baseLineWidthPixels"] = gc.baseLineWidthPixels;
         obj["isDotted"] = gc.isDotted;
         obj["snappingEnabled"] = gc.snappingEnabled;
         obj["masterVisible"] = gc.masterVisible;
         obj["xAxisColor"] = toJsonColor(gc.xAxisColor);
         obj["zAxisColor"] = toJsonColor(gc.zAxisColor);

         QJsonArray levels;
         for (auto& lvl : gc.levels) {
             QJsonObject L;
             L["spacing"] = lvl.spacing;
             L["fadeInCameraDistanceStart"] = lvl.fadeInCameraDistanceStart;
             L["fadeInCameraDistanceEnd"] = lvl.fadeInCameraDistanceEnd;
             L["color"] = toJsonColor(lvl.color);
             levels.append(L);
         }
         obj["levels"] = levels;
         gridsArr.append(obj);
     }

     QJsonObject root;
     root["grids"] = gridsArr;
     QString serialized = QJsonDocument(root).toJson(QJsonDocument::Compact);

     // --- save to DB ---
     qDebug() << "[GRID_DEBUG] shutdownAndSave(): saving" << gridsArr.size() << "grids";
     db::DatabaseManager::instance()
         .saveMenuState("GridProperties", serialized);

     // --- then destroy all so toggling off removes them from scene ---
     clearAllGrids();
 }


 void PropertiesPanel::initializeFromDatabase()
 {
     qDebug() << "[GRID_DEBUG] initializeFromDatabase()";
     QString blob = db::DatabaseManager::instance()
         .loadMenuState("GridProperties");

     if (blob.isEmpty()) {
         qDebug() << "[GRID_DEBUG] no saved state, falling back to fresh";
         initializeFresh();
         return;
     }

     QJsonParseError err;
     auto doc = QJsonDocument::fromJson(blob.toUtf8(), &err);
     if (err.error != QJsonParseError::NoError || !doc.isObject()) {
         qWarning() << "[GRID_DEBUG] JSON parse error:" << err.errorString();
         initializeFresh();
         return;
     }

     // Clear out any remnants
     clearAllGrids();

     auto root = doc.object();
     auto arr = root["grids"].toArray();
     auto& reg = m_scene->getRegistry();

     // --- FIX: Disconnect the signal handler before restoring ---
     reg.on_construct<GridComponent>().disconnect<&PropertiesPanel::onGridAdded>(this);

     qDebug() << "[GRID_DEBUG] restoring" << arr.size() << "grids from JSON";
     for (auto v : arr) {
         auto o = v.toObject();
         auto ent = reg.create();
         reg.emplace<TransformComponent>(ent);
         reg.emplace<TagComponent>(ent, o["tag"].toString().toStdString());

         // Now emplace will not trigger the onGridAdded handler
         auto& gc = reg.emplace<GridComponent>(ent);
         gc.baseLineWidthPixels = float(o["baseLineWidthPixels"].toDouble());
         gc.isDotted = o["isDotted"].toBool();
         gc.snappingEnabled = o["snappingEnabled"].toBool();
         gc.masterVisible = o["masterVisible"].toBool();
         gc.xAxisColor = fromJsonColor(o["xAxisColor"].toArray());
         gc.zAxisColor = fromJsonColor(o["zAxisColor"].toArray());

         gc.levels.clear();
         for (auto lv : o["levels"].toArray()) {
             auto L = lv.toObject();
             float sp = float(L["spacing"].toDouble());
             float s0 = float(L["fadeInCameraDistanceStart"].toDouble());
             float s1 = float(L["fadeInCameraDistanceEnd"].toDouble());
             auto  col = fromJsonColor(L["color"].toArray());
             gc.levels.emplace_back(sp, col, s0, s1);
         }

         // Manually add the editor now that the component is fully loaded
         addGridEditor(ent);
     }

     // --- FIX: Reconnect the signal handler for future user actions ---
     reg.on_construct<GridComponent>().connect<&PropertiesPanel::onGridAdded>(this);
 }

 void PropertiesPanel::disconnectRegistry()
 {
     auto& reg = m_scene->getRegistry();
     // these are idempotent (disconnecting when not connected is safe)
     reg.on_construct<GridComponent>()
         .disconnect<&PropertiesPanel::onGridAdded>(this);
     reg.on_destroy<GridComponent>()
         .disconnect<&PropertiesPanel::onGridRemoved>(this);
 }