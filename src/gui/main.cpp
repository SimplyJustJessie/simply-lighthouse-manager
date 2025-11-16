#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "../core/BaseStationDetector.h"
#include "../core/BaseStationController.h"
#include "../core/SteamVRMonitor.h"
#include <openvr.h>
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <mutex>
#include <future>
#include <map>
#include <set>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <limits.h>
#include <csignal>
#include <atomic>

static GLFWwindow* glfwWindow = nullptr;
static std::vector<BaseStationInfo> detectedStations;
static std::mutex stationsMutex;
static bool isScanning = false;
static std::string statusMessage = "Ready";
static bool statusError = false;

static std::atomic<bool> running(true);
static std::unique_ptr<SteamVRMonitor> steamVRMonitor;
static std::thread autoManageThread;
static std::vector<std::unique_ptr<BaseStationController>> managedControllers;
static std::mutex controllersMutex;

void ScanForStations()
{
    if (isScanning) return;
    
    isScanning = true;
    statusMessage = "Scanning for base stations...";
    statusError = false;
    
    std::thread scanThread([]() {
        BaseStationDetector detector;
        if (!detector.Initialize())
        {
            std::lock_guard<std::mutex> lock(stationsMutex);
            statusMessage = "Failed to initialize Bluetooth";
            statusError = true;
            isScanning = false;
            return;
        }
        
        auto stations = detector.ScanForBaseStations(10);
        
        {
            std::lock_guard<std::mutex> lock(stationsMutex);
            detectedStations = stations;
            if (stations.empty())
            {
                statusMessage = "No base stations found";
                statusError = true;
            }
            else
            {
                statusMessage = "Found " + std::to_string(stations.size()) + " base station(s)";
                statusError = false;
            }
            isScanning = false;
        }
    });
    
    scanThread.detach();
}

void ControlStation(const BaseStationInfo& station, BaseStationCommand command)
{
    // Return immediately - don't block the GUI thread
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
    
    // Quick status update without blocking
    {
        std::lock_guard<std::mutex> lock(stationsMutex);
        statusMessage = "Sending " + commandName + " to " + station.name + "...";
        statusError = false;
    }
    
    // Launch control in background thread - detach immediately
    std::thread([station, command, commandName]() {
        BaseStationController controller;
        
        {
            std::lock_guard<std::mutex> lock(stationsMutex);
            statusMessage = "Connecting to " + station.name + "...";
        }
        
        if (!controller.Connect(station))
        {
            std::lock_guard<std::mutex> lock(stationsMutex);
            statusMessage = "Failed to connect to " + station.name;
            statusError = true;
            return;
        }
        
        {
            std::lock_guard<std::mutex> lock(stationsMutex);
            statusMessage = "Sending " + commandName + " command...";
        }
        
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
        
        {
            std::lock_guard<std::mutex> lock(stationsMutex);
            if (success)
            {
                statusMessage = "✓ " + commandName + " command sent successfully to " + station.name;
                statusError = false;
            }
            else
            {
                statusMessage = "✗ Failed to send " + commandName + " command to " + station.name;
                statusError = true;
            }
        }
        
    }).detach(); // Detach immediately - don't wait
}

void BuildUI()
{
    int windowWidth, windowHeight;
    glfwGetWindowSize(glfwWindow, &windowWidth, &windowHeight);
    
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)windowWidth, (float)windowHeight));
    
    ImGui::Begin("Lighthouse Manager", nullptr, 
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | 
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_MenuBar);
    
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Scan for Base Stations", nullptr, false, !isScanning))
            {
                ScanForStations();
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
    ImGui::Separator();
    ImGui::Spacing();
    
    if (ImGui::Button("Scan for Base Stations", ImVec2(-1, 0)))
    {
        ScanForStations();
    }
    
    ImGui::Spacing();
    
    if (isScanning)
    {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Scanning...");
    }
    else
    {
        if (statusError)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", statusMessage.c_str());
        }
        else
        {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%s", statusMessage.c_str());
        }
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    std::vector<BaseStationInfo> stationsCopy;
    {
        std::lock_guard<std::mutex> lock(stationsMutex);
        stationsCopy = detectedStations;
    }
    
    if (stationsCopy.empty())
    {
        ImGui::Text("No base stations detected.");
        ImGui::Text("Click 'Scan for Base Stations' to search.");
    }
    else
    {
        ImGui::Text("Detected Base Stations: %zu", stationsCopy.size());
        ImGui::Spacing();
        
        if (ImGui::BeginTable("Stations", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 200.0f);
            ImGui::TableHeadersRow();
            
            for (size_t i = 0; i < stationsCopy.size(); ++i)
            {
                const auto& station = stationsCopy[i];
                
                ImGui::TableNextRow();
                
                ImGui::TableNextColumn();
                ImGui::Text("%s", station.name.c_str());
                
                ImGui::TableNextColumn();
                ImGui::Text("%s", station.id.c_str());
                
                ImGui::TableNextColumn();
                ImGui::Text("%s", station.address.c_str());
                
                ImGui::TableNextColumn();
                ImGui::Text("%s", station.isBaseStation1 ? "1.0" : "2.0");
                
                ImGui::TableNextColumn();
                ImGui::PushID((int)i);
                
                static std::map<std::string, std::chrono::steady_clock::time_point> lastActionTime;
                auto now = std::chrono::steady_clock::now();
                bool canAct = true;
                
                auto it = lastActionTime.find(station.address);
                if (it != lastActionTime.end())
                {
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second);
                    canAct = elapsed.count() > 1000;
                }
                
                if (ImGui::SmallButton("Wake"))
                {
                    if (canAct)
                    {
                        lastActionTime[station.address] = now;
                        ControlStation(station, BaseStationCommand::Wake);
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Sleep"))
                {
                    if (canAct)
                    {
                        lastActionTime[station.address] = now;
                        ControlStation(station, BaseStationCommand::Sleep);
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Standby"))
                {
                    if (canAct)
                    {
                        lastActionTime[station.address] = now;
                        ControlStation(station, BaseStationCommand::Standby);
                    }
                }
                
                ImGui::PopID();
            }
            
            ImGui::EndTable();
        }
    }
    
    ImGui::Separator();
    
    float creditsHeight = ImGui::GetTextLineHeightWithSpacing();
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - creditsHeight - ImGui::GetStyle().WindowPadding.y);
    
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
    
    // Ensure window starts small and resizable
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    
    // Set window class name for Hyprland window rules
    // This allows Hyprland to identify and apply rules to this window
    glfwWindowHintString(GLFW_X11_CLASS_NAME, "lighthouse-manager");
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "lighthouse-manager");
    
    // Small initial window size
    const int windowWidth = 600;
    const int windowHeight = 400;
    glfwWindow = glfwCreateWindow(windowWidth, windowHeight, "Lighthouse Manager", nullptr, nullptr);
    if (!glfwWindow)
    {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return false;
    }
    
    glfwMakeContextCurrent(glfwWindow);
    glfwSwapInterval(1);
    
    // Set minimum window size
    glfwSetWindowSizeLimits(glfwWindow, 500, 350, GLFW_DONT_CARE, GLFW_DONT_CARE);
    
    // Center window on creation (helps on Wayland/Hyprland)
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (monitor)
    {
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (mode)
        {
            int x = (mode->width - windowWidth) / 2;
            int y = (mode->height - windowHeight) / 2;
            glfwSetWindowPos(glfwWindow, x, y);
        }
    }
    
    // Force floating window on Hyprland using hyprctl
    // Wait a bit for the window to be registered with Hyprland
    std::thread([windowWidth, windowHeight]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // Check if hyprctl is available (indicates Hyprland)
        FILE* testPipe = popen("command -v hyprctl >/dev/null 2>&1 && echo yes", "r");
        bool hyprctlAvailable = false;
        if (testPipe) {
            char result[16] = {0};
            if (fgets(result, sizeof(result), testPipe) && strstr(result, "yes")) {
                hyprctlAvailable = true;
            }
            pclose(testPipe);
        }
        
        if (!hyprctlAvailable) {
            return; // Not running on Hyprland
        }
        
        // Try to find window by class name and set to floating
        // Method 1: Use jq if available
        std::string findCmd = "hyprctl clients -j 2>/dev/null | jq -r '.[] | select(.class == \"lighthouse-manager\") | .address' 2>/dev/null | head -1";
        FILE* findPipe = popen(findCmd.c_str(), "r");
        std::string address;
        
        if (findPipe) {
            char addr[64] = {0};
            if (fgets(addr, sizeof(addr), findPipe)) {
                size_t len = strlen(addr);
                if (len > 0 && addr[len-1] == '\n') {
                    addr[len-1] = '\0';
                }
                if (strlen(addr) > 0) {
                    address = addr;
                }
            }
            pclose(findPipe);
        }
        
        // Method 2: Fallback - parse hyprctl output without jq
        if (address.empty()) {
            std::string findCmd2 = "hyprctl clients 2>/dev/null | grep -A 20 'lighthouse-manager' | grep 'address:' | head -1 | awk '{print $2}'";
            FILE* findPipe2 = popen(findCmd2.c_str(), "r");
            if (findPipe2) {
                char addr[64] = {0};
                if (fgets(addr, sizeof(addr), findPipe2)) {
                    size_t len = strlen(addr);
                    if (len > 0 && addr[len-1] == '\n') {
                        addr[len-1] = '\0';
                    }
                    if (strlen(addr) > 0) {
                        address = addr;
                    }
                }
                pclose(findPipe2);
            }
        }
        
        if (!address.empty()) {
            // Set window to floating
            std::string floatCmd = "hyprctl dispatch togglefloating address:" + address + " >/dev/null 2>&1";
            system(floatCmd.c_str());
            
            // Set window size
            std::string sizeCmd = "hyprctl dispatch resizeactive exact " + 
                                std::to_string(windowWidth) + " " + std::to_string(windowHeight) + " >/dev/null 2>&1";
            system(sizeCmd.c_str());
        } else {
            // Last resort: try to float active window (if it's ours)
            system("hyprctl dispatch togglefloating active >/dev/null 2>&1");
        }
    }).detach();
    
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
    
    const char* glsl_version = "#version 330";
    if (!ImGui_ImplOpenGL3_Init(glsl_version))
    {
        std::cerr << "Failed to initialize ImGui OpenGL3\n";
        return false;
    }
    
    return true;
}

void Shutdown()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    
    if (glfwWindow)
    {
        glfwDestroyWindow(glfwWindow);
    }
    glfwTerminate();
}

bool IsSteamVRAvailable()
{
    vr::EVRInitError initError = vr::VRInitError_None;
    vr::IVRSystem* vrSystem = vr::VR_Init(&initError, vr::VRApplication_Background);
    
    if (initError == vr::VRInitError_None && vrSystem)
    {
        vr::VR_Shutdown();
        return true;
    }
    
    return false;
}

bool IsLaunchedBySteamVR()
{
    if (!IsSteamVRAvailable())
    {
        return false;
    }
    return true;
}

void RegisterManifest()
{
    if (!IsSteamVRAvailable())
    {
        return;
    }
    
    vr::EVRInitError initError = vr::VRInitError_None;
    vr::IVRSystem* vrSystem = vr::VR_Init(&initError, vr::VRApplication_Utility);
    
    if (initError != vr::VRInitError_None || !vrSystem)
    {
        return;
    }
    
    vr::IVRApplications* vrApplications = vr::VRApplications();
    if (!vrApplications)
    {
        vr::VR_Shutdown();
        return;
    }
    
    const char* appKey = "lighthouse-manager-linux";
    bool alreadyInstalled = vrApplications->IsApplicationInstalled(appKey);
    
    if (!alreadyInstalled)
    {
        char exePath[PATH_MAX];
        ssize_t count = readlink("/proc/self/exe", exePath, PATH_MAX);
        if (count != -1)
        {
            exePath[count] = '\0';
            std::string exeDir = std::string(exePath);
            size_t lastSlash = exeDir.find_last_of('/');
            if (lastSlash != std::string::npos)
            {
                exeDir = exeDir.substr(0, lastSlash);
            }
            
            std::string manifestPath = exeDir + "/manifest.vrmanifest";
            vrApplications->AddApplicationManifest(manifestPath.c_str());
        }
    }
    
    vrApplications->SetApplicationAutoLaunch(appKey, true);
    vr::VR_Shutdown();
}

void StartAutoManagement()
{
    if (!IsSteamVRAvailable())
    {
        return;
    }
    
    BaseStationDetector detector;
    if (!detector.Initialize())
    {
        return;
    }
    
    steamVRMonitor = std::make_unique<SteamVRMonitor>();
    if (!steamVRMonitor->Initialize())
    {
        steamVRMonitor.reset();
        return;
    }
    
    auto WakeAllStations = [&]() {
        const int maxRetries = 5;
        const int scanTimeout = 20;
        std::set<std::string> alreadyWoken;
        std::set<std::string> allFoundStations;
        
        for (int retry = 0; retry < maxRetries; retry++)
        {
            if (retry > 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            }
            
            auto stations = detector.ScanForBaseStations(scanTimeout);
            
            for (const auto& station : stations)
            {
                allFoundStations.insert(station.address);
            }
            
            if (stations.empty())
            {
                continue;
            }
            
            std::lock_guard<std::mutex> lock(controllersMutex);
            
            bool anyNewSuccess = false;
            for (const auto& station : stations)
            {
                if (alreadyWoken.find(station.address) != alreadyWoken.end())
                {
                    continue;
                }
                
                auto controller = std::make_unique<BaseStationController>();
                if (controller->Connect(station))
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    if (controller->Wake())
                    {
                        managedControllers.push_back(std::move(controller));
                        alreadyWoken.insert(station.address);
                        anyNewSuccess = true;
                    }
                    else
                    {
                        controller->Disconnect();
                    }
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
            
            if (retry >= maxRetries - 1 || (allFoundStations.size() > 0 && alreadyWoken.size() >= allFoundStations.size()))
            {
                break;
            }
        }
    };
    
    auto SleepAllStations = [&]() {
        const int maxRetries = 3;
        
        for (int retry = 0; retry < maxRetries; retry++)
        {
            std::lock_guard<std::mutex> lock(controllersMutex);
            
            if (managedControllers.empty())
            {
                break;
            }
            
            bool allSucceeded = true;
            std::vector<std::unique_ptr<BaseStationController>> failedControllers;
            
            for (auto& controller : managedControllers)
            {
                if (controller->Sleep())
                {
                    controller->Disconnect();
                }
                else
                {
                    allSucceeded = false;
                    failedControllers.push_back(std::move(controller));
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
            
            if (allSucceeded)
            {
                managedControllers.clear();
                break;
            }
            
            managedControllers = std::move(failedControllers);
            
            if (retry < maxRetries - 1)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
            else
            {
                for (auto& controller : managedControllers)
                {
                    controller->Disconnect();
                }
                managedControllers.clear();
            }
        }
    };
    
    bool lastSteamVRState = false;
    bool stationsWoken = false;
    std::chrono::steady_clock::time_point lastPeriodicWake = std::chrono::steady_clock::now();
    
    steamVRMonitor->SetStateChangeCallback([&](bool isRunning) {
        if (isRunning == lastSteamVRState)
            return;
        
        if (isRunning)
        {
            if (!stationsWoken)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                WakeAllStations();
                stationsWoken = true;
                lastPeriodicWake = std::chrono::steady_clock::now();
            }
            lastSteamVRState = true;
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            if (!steamVRMonitor->IsSteamVRRunning())
            {
                SleepAllStations();
                stationsWoken = false;
                running = false;
                if (glfwWindow)
                {
                    glfwSetWindowShouldClose(glfwWindow, true);
                }
            }
            lastSteamVRState = false;
        }
    });
    
    if (steamVRMonitor->IsSteamVRRunning())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        WakeAllStations();
        lastSteamVRState = true;
        stationsWoken = true;
        lastPeriodicWake = std::chrono::steady_clock::now();
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    steamVRMonitor->StartMonitoring();
    
    while (running && steamVRMonitor)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        if (stationsWoken)
        {
            bool steamVRRunning = steamVRMonitor->IsSteamVRRunning();
            
            if (steamVRRunning)
            {
                auto now = std::chrono::steady_clock::now();
                auto timeSinceLastPeriodicWake = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPeriodicWake).count();
                
                if (timeSinceLastPeriodicWake >= 5000)
                {
                    std::lock_guard<std::mutex> lock(controllersMutex);
                    for (auto& controller : managedControllers)
                    {
                        if (controller && controller->IsConnected())
                        {
                            controller->SendWakePacket();
                        }
                    }
                    lastPeriodicWake = now;
                }
            }
        }
    }
    
    SleepAllStations();
    
    if (steamVRMonitor)
    {
        steamVRMonitor->StopMonitoring();
        steamVRMonitor->Shutdown();
        steamVRMonitor.reset();
    }
}

int main(int argc, char* argv[])
{
    bool steamVRAvailable = IsSteamVRAvailable();
    bool launchedBySteamVR = false;
    
    if (steamVRAvailable)
    {
        RegisterManifest();
        launchedBySteamVR = IsLaunchedBySteamVR();
        
        if (launchedBySteamVR)
        {
            autoManageThread = std::thread(StartAutoManagement);
        }
    }
    
    if (!InitGLFW())
    {
        running = false;
        if (autoManageThread.joinable())
        {
            autoManageThread.join();
        }
        return 1;
    }
    
    if (!InitImGui())
    {
        Shutdown();
        running = false;
        if (autoManageThread.joinable())
        {
            autoManageThread.join();
        }
        return 1;
    }
    
    ScanForStations();
    
    vr::IVRSystem* vrSystemForEvents = nullptr;
    if (launchedBySteamVR && steamVRAvailable)
    {
        vr::EVRInitError initError = vr::VRInitError_None;
        vrSystemForEvents = vr::VR_Init(&initError, vr::VRApplication_Overlay);
        if (initError != vr::VRInitError_None)
        {
            vrSystemForEvents = nullptr;
        }
    }
    
    while (!glfwWindowShouldClose(glfwWindow) && running)
    {
        glfwPollEvents();
        
        if (vrSystemForEvents)
        {
            vr::VREvent_t vrEvent;
            while (vrSystemForEvents->PollNextEvent(&vrEvent, sizeof(vrEvent)))
            {
                if (vrEvent.eventType == vr::VREvent_Quit)
                {
                    if (glfwWindow)
                    {
                        glfwSetWindowShouldClose(glfwWindow, true);
                    }
                    break;
                }
            }
        }
        
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        
        BuildUI();
        
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
    
    running = false;
    
    if (autoManageThread.joinable())
    {
        autoManageThread.join();
    }
    
    if (vrSystemForEvents)
    {
        vr::VR_Shutdown();
    }
    
    Shutdown();
    return 0;
}

