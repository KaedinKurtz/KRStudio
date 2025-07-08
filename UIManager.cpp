#include "UIManager.hpp"
#include "Robot.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <vector>
#include <string>

UIManager::UIManager(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    // --- FIX: Enable Docking and Viewports ---
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Optional: for multi-viewport support

    ImGui::StyleColorsDark();

    // --- FIX: Tell ImGui NOT to install its own callbacks ---
    ImGui_ImplGlfw_InitForOpenGL(window, false);
    ImGui_ImplOpenGL3_Init("#version 330");
}

UIManager::~UIManager() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void UIManager::beginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Create the main dockspace where all windows can be docked
    setupDockspace();
}

void UIManager::endFrame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void UIManager::renderMainMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Exit")) {
                // In a real app, you'd set a flag to close the window gracefully
                // For now, we can just print a message.
                printf("Exit clicked.\n");
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            // Add options to show/hide UI panels here
            ImGui::MenuItem("Diagnostics Panel", NULL, nullptr);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void UIManager::renderDiagnostics(Robot& robot) {
    ImGui::Begin("Diagnostics & Controls"); // This is now a dockable window

    // All the content from the previous renderRobotDiagnostics function goes here
    if (ImGui::CollapsingHeader("Identification", ImGuiTreeNodeFlags_DefaultOpen)) { /* ... content ... */ }
    if (ImGui::CollapsingHeader("Operational Status", ImGuiTreeNodeFlags_DefaultOpen)) { /* ... content ... */ }
    if (ImGui::CollapsingHeader("Kinematics")) { /* ... content ... */ }
    if (ImGui::CollapsingHeader("Diagnostics")) { /* ... content ... */ }
    if (ImGui::CollapsingHeader("System Control & Errors", ImGuiTreeNodeFlags_DefaultOpen)) { /* ... content ... */ }

    ImGui::End();
}


void UIManager::setupDockspace() {
    // Make the main window a dockspace
    static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("DockSpace", nullptr, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

    ImGui::End();
}

// --- Helper Implementations ---
const char* UIManager::stateToString(Robot::State state) {
    switch (state) {
    case Robot::State::IDLE: return "IDLE";
    case Robot::State::MOVING: return "MOVING";
    case Robot::State::WORKING: return "WORKING";
    case Robot::State::ERROR: return "ERROR";
    case Robot::State::BOOTING: return "BOOTING";
    case Robot::State::SHUTDOWN: return "SHUTDOWN";
    case Robot::State::ESTOPPED: return "EMERGENCY STOPPED";
    default: return "UNKNOWN";
    }
}

const char* UIManager::modeToString(Robot::OperatingMode mode) {
    switch (mode) {
    case Robot::OperatingMode::MANUAL: return "MANUAL";
    case Robot::OperatingMode::AUTONOMOUS: return "AUTONOMOUS";
    case Robot::OperatingMode::TEACH: return "TEACH";
    default: return "UNKNOWN";
    }
}