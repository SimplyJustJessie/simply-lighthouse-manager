// Lighthouse Manager GUI - manual base station control and auto-manage
// configuration. Holds NO persistent OpenVR context; the SteamVR-session
// lifecycle lives in the headless CLI service (`lighthouse-manager --auto`).
// All Bluetooth/OpenVR work runs on a single joinable worker thread; shared
// state is only touched under GuiState::m or via atomics.

#define GL_SILENCE_DEPRECATION
#include <GL/gl.h>
#include <GLFW/glfw3.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui.h"

#include "../core/BaseStationController.h"
#include "../core/BaseStationDetector.h"
#include "../core/Config.h"
#include "../core/SteamVRWatcher.h"
#include "../core/VRRegistration.h"

namespace
{

struct GuiState
{
    std::mutex m;
    // guarded by m:
    std::string statusMessage = "Ready";
    bool statusError = false;
    std::vector<BaseStationInfo> stations;
    vrreg::Status regStatus;
    bool regStatusKnown = false;

    // UI-thread only:
    Config config;
    bool configDirty = false;

    // atomics:
    std::atomic<bool> scanning{false};
    std::atomic<bool> steamvrRunning{false};
    std::atomic<bool> quitting{false};

    void SetStatus(const std::string& message, bool isError)
    {
        std::lock_guard<std::mutex> lock(m);
        statusMessage = message;
        statusError = isError;
    }
};

// One worker thread executing queued jobs in order. Exceptions become status
// messages instead of std::terminate. The destructor drops queued-but-unrun
// jobs and joins, so no work outlives main().
class WorkerQueue
{
public:
    explicit WorkerQueue(GuiState& state) : state(state), worker([this] { Run(); }) {}

    ~WorkerQueue()
    {
        {
            std::lock_guard<std::mutex> lock(m);
            stop = true;
            jobs.clear();
        }
        cv.notify_all();
        worker.join();
    }

    void Post(std::function<void()> job)
    {
        {
            std::lock_guard<std::mutex> lock(m);
            if (stop)
            {
                return;
            }
            jobs.push_back(std::move(job));
            ++pendingCount;
        }
        cv.notify_one();
    }

    bool Busy() const { return pendingCount.load() > 0; }

private:
    void Run()
    {
        for (;;)
        {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(m);
                cv.wait(lock, [this] { return stop || !jobs.empty(); });
                if (stop)
                {
                    return;
                }
                job = std::move(jobs.front());
                jobs.pop_front();
            }

            try
            {
                job();
            }
            catch (const std::exception& e)
            {
                state.SetStatus(std::string("Internal error: ") + e.what(), true);
            }
            catch (...)
            {
                state.SetStatus("Internal error in background task", true);
            }
            --pendingCount;
        }
    }

    GuiState& state;
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::function<void()>> jobs;
    bool stop = false;
    std::atomic<int> pendingCount{0};
    std::thread worker;
};

GLFWwindow* glfwWindow = nullptr;

void PostScan(GuiState& state, WorkerQueue& worker)
{
    if (state.scanning)
    {
        return;
    }
    state.scanning = true;
    state.SetStatus("Scanning for base stations...", false);

    worker.Post(
        [&state]()
        {
            BaseStationDetector detector;
            if (!detector.Initialize())
            {
                state.SetStatus("Failed to initialize Bluetooth", true);
                state.scanning = false;
                return;
            }

            auto stations = detector.ScanForBaseStations(
                10, [&state] { return state.quitting.load(); });

            {
                std::lock_guard<std::mutex> lock(state.m);
                state.stations = stations;
                if (stations.empty())
                {
                    state.statusMessage = "No base stations found";
                    state.statusError = true;
                }
                else
                {
                    state.statusMessage =
                        "Found " + std::to_string(stations.size()) + " base station(s)";
                    state.statusError = false;
                }
            }
            state.scanning = false;
        });
}

void PostControl(GuiState& state, WorkerQueue& worker, const BaseStationInfo& station,
                 BaseStationCommand command)
{
    std::string commandName;
    switch (command)
    {
        case BaseStationCommand::Wake:
            commandName = "Wake";
            break;
        case BaseStationCommand::Sleep:
            commandName = "Sleep";
            break;
        case BaseStationCommand::Standby:
            commandName = "Standby";
            break;
    }

    state.SetStatus("Queued " + commandName + " for " + station.name, false);

    worker.Post(
        [&state, station, command, commandName]()
        {
            state.SetStatus("Connecting to " + station.name + "...", false);

            BaseStationController controller;
            if (!controller.Connect(station))
            {
                state.SetStatus("Failed to connect to " + station.name, true);
                return;
            }

            state.SetStatus("Sending " + commandName + " to " + station.name + "...", false);

            bool success = false;
            switch (command)
            {
                case BaseStationCommand::Wake:
                    success = controller.Wake();
                    break;
                case BaseStationCommand::Sleep:
                    success = controller.Sleep();
                    break;
                case BaseStationCommand::Standby:
                    success = controller.Standby();
                    break;
            }

            controller.Disconnect();

            if (success)
            {
                state.SetStatus("✓ " + commandName + " sent to " + station.name, false);
            }
            else
            {
                state.SetStatus("✗ Failed to send " + commandName + " to " + station.name, true);
            }
        });
}

void PostRegistrationCheck(GuiState& state, WorkerQueue& worker)
{
    worker.Post(
        [&state]()
        {
            if (!SteamVRWatcher::IsVrServerProcessRunning())
            {
                return;
            }
            vrreg::Status status = vrreg::CheckRegistration();
            std::lock_guard<std::mutex> lock(state.m);
            state.regStatus = status;
            state.regStatusKnown = true;
        });
}

void PostRegister(GuiState& state, WorkerQueue& worker, bool enable)
{
    worker.Post(
        [&state, enable]()
        {
            std::string error;
            bool ok = enable ? vrreg::RegisterManifest(true, false, &error)
                             : vrreg::DisableAutoLaunch(false, &error);
            if (ok)
            {
                state.SetStatus(enable ? "Auto-start with SteamVR enabled"
                                       : "Auto-start with SteamVR disabled",
                                false);
            }
            else
            {
                state.SetStatus("Registration failed: " +
                                    (error.empty() ? std::string("unknown error") : error),
                                true);
            }

            vrreg::Status status = vrreg::CheckRegistration();
            std::lock_guard<std::mutex> lock(state.m);
            state.regStatus = status;
            state.regStatusKnown = true;
        });
}

void SaveConfig(GuiState& state)
{
    if (state.config.Save())
    {
        state.configDirty = false;
        state.SetStatus("Configuration saved - the auto service applies it within ~15s", false);
    }
    else
    {
        state.SetStatus("Failed to save " + Config::DefaultPath(), true);
    }
}

void BuildUI(GuiState& state, WorkerQueue& worker)
{
    int windowWidth, windowHeight;
    glfwGetWindowSize(glfwWindow, &windowWidth, &windowHeight);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)windowWidth, (float)windowHeight));

    ImGui::Begin("Lighthouse Manager", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_MenuBar);

    const bool busy = worker.Busy();
    const bool scanning = state.scanning.load();
    const bool steamvrUp = state.steamvrRunning.load();

    // Locked copies for this frame.
    std::string statusMessage;
    bool statusError;
    std::vector<BaseStationInfo> stations;
    vrreg::Status regStatus;
    bool regStatusKnown;
    {
        std::lock_guard<std::mutex> lock(state.m);
        statusMessage = state.statusMessage;
        statusError = state.statusError;
        stations = state.stations;
        regStatus = state.regStatus;
        regStatusKnown = state.regStatusKnown;
    }

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Scan for Base Stations", nullptr, false, !scanning))
            {
                PostScan(state, worker);
            }
            if (ImGui::MenuItem("Exit", "Esc"))
            {
                glfwSetWindowShouldClose(glfwWindow, true);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    ImGui::Spacing();
    ImGui::Text("Lighthouse Manager - Linux Edition");
    ImGui::SameLine(ImGui::GetWindowWidth() - 160);
    if (steamvrUp)
    {
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "SteamVR: running");
    }
    else
    {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "SteamVR: stopped");
    }
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::BeginDisabled(scanning);
    if (ImGui::Button("Scan for Base Stations", ImVec2(-1, 0)))
    {
        PostScan(state, worker);
    }
    ImGui::EndDisabled();

    ImGui::Spacing();

    if (scanning)
    {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Scanning...");
    }
    else if (statusError)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", statusMessage.c_str());
    }
    else
    {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%s", statusMessage.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (stations.empty())
    {
        ImGui::Text("No base stations detected.");
        ImGui::Text("Click 'Scan for Base Stations' to search.");
    }
    else
    {
        ImGui::Text("Detected Base Stations: %zu", stations.size());
        ImGui::Spacing();

        if (ImGui::BeginTable("Stations", 6,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("Auto", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 190.0f);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < stations.size(); ++i)
            {
                const auto& station = stations[i];

                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text("%s", station.name.c_str());

                ImGui::TableNextColumn();
                ImGui::Text("%s", station.id.c_str());

                ImGui::TableNextColumn();
                ImGui::Text("%s", station.address.c_str());

                ImGui::TableNextColumn();
                ImGui::Text("%s", station.isBaseStation1 ? "1.0" : "2.0");

                ImGui::PushID((int)i);

                ImGui::TableNextColumn();
                bool managed = state.config.IsManaged(station);
                if (ImGui::Checkbox("##auto", &managed))
                {
                    state.config.SetManaged(station.address, station.name, managed);
                    state.configDirty = true;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                {
                    ImGui::SetTooltip("Wake/sleep this station automatically\nwith SteamVR");
                }

                ImGui::TableNextColumn();
                ImGui::BeginDisabled(busy);
                if (ImGui::SmallButton("Wake"))
                {
                    PostControl(state, worker, station, BaseStationCommand::Wake);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Sleep"))
                {
                    PostControl(state, worker, station, BaseStationCommand::Sleep);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Standby"))
                {
                    PostControl(state, worker, station, BaseStationCommand::Standby);
                }
                ImGui::EndDisabled();

                ImGui::PopID();
            }

            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Auto-management");

    bool manageAll = state.config.manageMode == Config::ManageMode::All;
    if (ImGui::Checkbox("Manage newly discovered stations automatically", &manageAll))
    {
        state.config.manageMode = manageAll ? Config::ManageMode::All
                                            : Config::ManageMode::Selected;
        state.configDirty = true;
    }

    ImGui::BeginDisabled(!state.configDirty);
    if (ImGui::Button("Save configuration"))
    {
        SaveConfig(state);
    }
    ImGui::EndDisabled();
    if (state.configDirty)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "unsaved changes");
    }

    ImGui::Spacing();

    if (!regStatusKnown)
    {
        ImGui::Text("SteamVR auto-start: unknown");
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                           "Start SteamVR to check the registration.");
    }
    else if (!regStatus.registered)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Not registered with SteamVR");
        ImGui::BeginDisabled(!steamvrUp || busy);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.80f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.55f, 0.95f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.40f, 0.70f, 1.0f));
        if (ImGui::Button("Register with SteamVR (enable auto-start)", ImVec2(-1, 0)))
        {
            PostRegister(state, worker, true);
        }
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();
        if (!steamvrUp)
        {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                               "(start SteamVR to register)");
        }
    }
    else
    {
        ImGui::Text("SteamVR auto-start: %s", regStatus.autoLaunch ? "enabled" : "disabled");
        ImGui::BeginDisabled(!steamvrUp || busy);
        if (regStatus.autoLaunch)
        {
            if (ImGui::Button("Disable auto-start"))
            {
                PostRegister(state, worker, false);
            }
        }
        else
        {
            if (ImGui::Button("Enable auto-start"))
            {
                PostRegister(state, worker, true);
            }
        }
        ImGui::EndDisabled();
        if (!steamvrUp)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "(requires SteamVR running)");
        }
    }

    ImGui::Separator();

    float creditsHeight = ImGui::GetTextLineHeightWithSpacing();
    float creditsY = ImGui::GetWindowHeight() - creditsHeight - ImGui::GetStyle().WindowPadding.y;
    if (creditsY > ImGui::GetCursorPosY())
    {
        ImGui::SetCursorPosY(creditsY);
    }

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Based on ");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "OVR Lighthouse Manager");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), " by ");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "kurotu");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), " | Linux port by ");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "@xi-ve");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), " | fork by ");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "@simplyyjessie");

    ImGui::End();
}

bool InitGLFW()
{
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW\n";
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);

    // Stable identifiers for window-manager rules. On Hyprland, users can
    // float the window with: windowrule = float, class:lighthouse-manager
    glfwWindowHintString(GLFW_X11_CLASS_NAME, "lighthouse-manager");
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "lighthouse-manager");
#ifdef GLFW_WAYLAND_APP_ID
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, "lighthouse-manager");
#endif

    const int windowWidth = 640;
    const int windowHeight = 520;
    glfwWindow = glfwCreateWindow(windowWidth, windowHeight, "Lighthouse Manager",
                                  nullptr, nullptr);
    if (!glfwWindow)
    {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(glfwWindow);
    glfwSwapInterval(1);
    glfwSetWindowSizeLimits(glfwWindow, 520, 400, GLFW_DONT_CARE, GLFW_DONT_CARE);

    return true;
}

bool InitImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplGlfw_InitForOpenGL(glfwWindow, true))
    {
        std::cerr << "Failed to initialize ImGui GLFW\n";
        return false;
    }

    if (!ImGui_ImplOpenGL3_Init("#version 330"))
    {
        std::cerr << "Failed to initialize ImGui OpenGL3\n";
        return false;
    }

    return true;
}

void ShutdownGraphics()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (glfwWindow)
    {
        glfwDestroyWindow(glfwWindow);
        glfwWindow = nullptr;
    }
    glfwTerminate();
}

}  // namespace

int main(int, char**)
{
    if (!InitGLFW())
    {
        return 1;
    }

    if (!InitImGui())
    {
        ShutdownGraphics();
        return 1;
    }

    // Declaration order matters: worker's destructor joins its thread before
    // state is destroyed, so jobs can safely capture &state.
    GuiState state;
    state.config.Load();
    {
        WorkerQueue worker(state);

        state.steamvrRunning = SteamVRWatcher::IsVrServerProcessRunning();
        PostScan(state, worker);
        PostRegistrationCheck(state, worker);

        auto lastSteamVRCheck = std::chrono::steady_clock::now();

        while (!glfwWindowShouldClose(glfwWindow))
        {
            glfwPollEvents();

            auto now = std::chrono::steady_clock::now();
            if (now - lastSteamVRCheck >= std::chrono::seconds(2))
            {
                lastSteamVRCheck = now;
                bool wasRunning = state.steamvrRunning.exchange(
                    SteamVRWatcher::IsVrServerProcessRunning());
                if (!wasRunning && state.steamvrRunning)
                {
                    PostRegistrationCheck(state, worker);
                }
            }

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            BuildUI(state, worker);

            ImGui::Render();

            int display_w, display_h;
            glfwGetFramebufferSize(glfwWindow, &display_w, &display_h);
            glViewport(0, 0, display_w, display_h);
            glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            glfwSwapBuffers(glfwWindow);

            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                glfwSetWindowShouldClose(glfwWindow, true);
            }
        }

        state.quitting = true;
        // worker destructor: drops queued jobs, joins the running one.
    }

    ShutdownGraphics();
    return 0;
}
