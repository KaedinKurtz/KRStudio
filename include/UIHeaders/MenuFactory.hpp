#pragma once

#include "IMenu.hpp"
#include "FlowVisualizerMenu.hpp"
#include "RealSenseConfigMenu.hpp"
#include "DatabasePanel.hpp"
#include "gridPropertiesWidget.hpp"
#include "PropertiesPanel.hpp" // assuming this is a grid properties menu
#include <memory>

#include "components.hpp"
#include <entt/entt.hpp>

// forward declarations so the factory knows about each menu type
class Scene;
class FlowVisualizerMenu;
class RealSenseConfigMenu;
class DatabasePanel;
class gridPropertiesWidget;
class PropertiesPanel; // assuming this is a grid properties menu

enum class MenuType {
    FlowVisualizer,
    RealSense,
    GridProperties,    // if you implement a GridPropertiesMenu
    Database,
    // add more as you go
};

/// A simple factory that knows how to build each IMenu subclass.
/// NOTE: DatabasePanel needs a Scene* plus parent, so we accept both here.
struct MenuFactory
{
    static std::unique_ptr<IMenu> create(MenuType type, Scene* scene, QWidget* parent)
    {
        switch (type)
        {
        case MenuType::FlowVisualizer:
            // FlowVisualizerMenu(QWidget* parent)
            return std::unique_ptr<IMenu>(new FlowVisualizerMenu(parent));

        case MenuType::RealSense:
            // RealSenseConfigMenu(QWidget* parent)
            return std::unique_ptr<IMenu>(new RealSenseConfigMenu(parent));

        case MenuType::GridProperties:
            // GridPropertiesMenu when implemented
            return std::unique_ptr<IMenu>(new PropertiesPanel(scene, parent));

        case MenuType::Database:
            // DatabasePanel(Scene* scene, QWidget* parent)
            return std::unique_ptr<IMenu>(new DatabasePanel(scene, parent));

        default:
            return nullptr;
        }
    }
};
