#pragma once


struct GLFWwindow;

class UIManager {
public:
    // Tell ImGui not to install callbacks, and enable Docking
    UIManager(GLFWwindow* window);
    ~UIManager();

    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;

    void beginFrame();
    void endFrame();

    // --- New Modular UI Rendering Functions ---
    void renderMainMenuBar();
    // We can add more here later, like renderSceneHierarchy() or renderTimeline()

private:
    void setupDockspace(); // Helper to create the main docking layout

};
