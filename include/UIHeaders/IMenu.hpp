// IMenu.hpp
#pragma once

#include <QWidget>

class IMenu
{
public:
    virtual ~IMenu() = default;

    /// Called exactly once when the menu is first created
    virtual void initializeFresh() = 0;

    /// Called when reopening an existing menu: load last-saved state
    virtual void initializeFromDatabase() = 0;

    /// Called just before hiding/destroying to save its current state
    virtual void shutdownAndSave() = 0;

    /// Expose the QWidget so we can stick it into a CDockWidget
    virtual QWidget* widget() = 0;
};
