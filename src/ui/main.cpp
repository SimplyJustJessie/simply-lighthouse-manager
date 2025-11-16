#include <iostream>
#include <vector>
#include <string>
#include <set>
#include <unistd.h>
#include <limits.h>
#include "../core/BaseStationDetector.h"
#include "../core/BaseStationController.h"
#include "../core/SteamVRMonitor.h"
#include <thread>
#include <chrono>
#include <csignal>
#include <openvr.h>

static bool running = true;

void signalHandler(int)
{
    running = false;
}

void RegisterManifest(bool verbose = false);

void PrintUsage(const char* programName)
{
    std::cout << "Lighthouse Manager - Linux Edition\n";
    std::cout << "Usage: " << programName << " [COMMAND] [OPTIONS]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  --list, --scan           Scan and list detected base stations\n";
    std::cout << "  --enable, --wake <id>    Wake up a base station (by ID or address)\n";
    std::cout << "  --disable, --sleep <id>  Put a base station to sleep (by ID or address)\n";
    std::cout << "  --standby <id>           Put a base station to standby (by ID or address)\n";
    std::cout << "  --auto                   Automatically manage base stations based on SteamVR status\n";
    std::cout << "  --register-manifest      Register application manifest with SteamVR\n";
    std::cout << "  --check-registration     Check if application is registered with SteamVR\n";
    std::cout << "  --help, -h               Display this help message\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << programName << " --list\n";
    std::cout << "  " << programName << " --scan\n";
    std::cout << "  " << programName << " --wake 699A51BC\n";
    std::cout << "  " << programName << " --sleep D4:1D:FE:B1:FE:E8\n";
    std::cout << "  " << programName << " --auto\n";
    std::cout << "\nNote: Base station ID can be:\n";
    std::cout << "  - 8-character ID (e.g., 699A51BC)\n";
    std::cout << "  - MAC address (e.g., D4:1D:FE:B1:FE:E8)\n";
    std::cout << "  - Partial name match\n";
}

void ListBaseStations()
{
    BaseStationDetector detector;
    if (!detector.Initialize())
    {
        std::cerr << "Failed to initialize Bluetooth adapter\n";
        std::cerr << "Make sure Bluetooth is enabled and working\n";
        return;
    }
    
    auto stations = detector.ScanForBaseStations(15);
    
    if (stations.empty())
    {
        std::cout << "No base stations found\n";
        return;
    }
    
    std::cout << "Found " << stations.size() << " base station(s):\n";
    for (size_t i = 0; i < stations.size(); ++i)
    {
        const auto& station = stations[i];
           std::cout << "[" << i << "] " << station.name << "\n";
           std::cout << "     ID: " << station.id << "\n";
           std::cout << "     Address: " << station.address << "\n";
           std::cout << "     Type: " << (station.isBaseStation1 ? "1.0" : "2.0") << "\n";
    }
}

void ControlBaseStation(const std::string& stationId, BaseStationCommand command)
{
    BaseStationDetector detector;
    if (!detector.Initialize())
    {
        std::cerr << "Failed to initialize Bluetooth\n";
        return;
    }
    
    auto stations = detector.ScanForBaseStations(10);
    
    if (stations.empty())
    {
        std::cerr << "No base stations found. Cannot control.\n";
        return;
    }
    
    BaseStationInfo* targetStation = nullptr;
    for (auto& station : stations)
    {
        if (station.id == stationId || station.address == stationId || 
            station.serial == stationId || station.name.find(stationId) != std::string::npos)
        {
            targetStation = &station;
            break;
        }
    }
    
    if (!targetStation)
    {
        std::cerr << "Base station not found: " << stationId << "\n";
        std::cerr << "\nAvailable base stations:\n";
        for (size_t i = 0; i < stations.size(); ++i)
        {
            std::cerr << "  [" << i << "] " << stations[i].name 
                      << " (ID: " << stations[i].id << ")\n";
        }
        return;
    }
    
    BaseStationController controller;
    if (!controller.Connect(*targetStation))
    {
        std::cerr << "Failed to connect to base station\n";
        return;
    }
    
    bool success = false;
    const char* commandName = "";
    
    switch (command)
    {
        case BaseStationCommand::Wake:
            success = controller.Wake();
            commandName = "Wake";
            break;
        case BaseStationCommand::Sleep:
            success = controller.Sleep();
            commandName = "Sleep";
            break;
        case BaseStationCommand::Standby:
            success = controller.Standby();
            commandName = "Standby";
            break;
    }
    
    if (success)
    {
        std::cout << "✓ " << commandName << " command sent\n";
    }
    else
    {
        std::cerr << "✗ Failed to send " << commandName << " command\n";
    }
    
    controller.Disconnect();
}

void AutoManage()
{
    RegisterManifest();
    
    BaseStationDetector detector;
    if (!detector.Initialize())
    {
        std::cerr << "Failed to initialize Bluetooth\n";
        return;
    }
    
    SteamVRMonitor monitor;
    if (!monitor.Initialize())
    {
        std::cerr << "Failed to initialize SteamVR monitor\n";
        return;
    }
    
    std::vector<BaseStationInfo> managedStations;
    std::vector<std::unique_ptr<BaseStationController>> controllers;
    
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
                        controllers.push_back(std::move(controller));
                        managedStations.push_back(station);
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
            if (controllers.empty())
            {
                break;
            }
            
            bool allSucceeded = true;
            std::vector<std::unique_ptr<BaseStationController>> failedControllers;
            std::vector<BaseStationInfo> failedStations;
            
            for (size_t i = 0; i < controllers.size(); i++)
            {
                if (controllers[i]->Sleep())
                {
                    controllers[i]->Disconnect();
                }
                else
                {
                    allSucceeded = false;
                    failedControllers.push_back(std::move(controllers[i]));
                    failedStations.push_back(managedStations[i]);
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
            
            if (allSucceeded)
            {
                controllers.clear();
                managedStations.clear();
                break;
            }
            
            controllers = std::move(failedControllers);
            managedStations = std::move(failedStations);
            
            if (retry < maxRetries - 1)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
            else
            {
                for (auto& controller : controllers)
                {
                    controller->Disconnect();
                }
                controllers.clear();
                managedStations.clear();
            }
        }
    };
    
    bool lastSteamVRState = false;
    bool stationsWoken = false;
    
    monitor.SetStateChangeCallback([&](bool isRunning) {
        if (isRunning == lastSteamVRState)
            return;
        
        if (isRunning)
        {
            if (!stationsWoken)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(2000));
                WakeAllStations();
                stationsWoken = true;
            }
            lastSteamVRState = true;
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            if (!monitor.IsSteamVRRunning())
            {
                SleepAllStations();
                stationsWoken = false;
            }
            lastSteamVRState = false;
        }
    });
    
    std::chrono::steady_clock::time_point lastPeriodicWake = std::chrono::steady_clock::now();
    
    if (monitor.IsSteamVRRunning())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        WakeAllStations();
        lastSteamVRState = true;
        stationsWoken = true;
        lastPeriodicWake = std::chrono::steady_clock::now();
    }
    
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    monitor.StartMonitoring();
    
    while (running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        if (stationsWoken)
        {
            bool steamVRRunning = monitor.IsSteamVRRunning();
            
            if (steamVRRunning)
            {
                auto now = std::chrono::steady_clock::now();
                auto timeSinceLastPeriodicWake = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPeriodicWake).count();
                
                if (timeSinceLastPeriodicWake >= 5000)
                {
                    for (auto& controller : controllers)
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
    monitor.StopMonitoring();
    monitor.Shutdown();
}

bool IsLaunchedBySteamVR()
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

void RegisterManifest(bool verbose)
{
    vr::EVRInitError initError = vr::VRInitError_None;
    vr::IVRSystem* vrSystem = vr::VR_Init(&initError, vr::VRApplication_Utility);
    
    if (initError != vr::VRInitError_None)
    {
        if (verbose)
        {
            std::cerr << "Failed to initialize OpenVR: " << vr::VR_GetVRInitErrorAsEnglishDescription(initError) << "\n";
        }
        return;
    }
    
    if (!vrSystem)
    {
        if (verbose)
        {
            std::cerr << "Failed to initialize VR system\n";
        }
        return;
    }
    
    vr::IVRApplications* vrApplications = vr::VRApplications();
    if (!vrApplications)
    {
        if (verbose)
        {
            std::cerr << "Failed to get VRApplications interface\n";
        }
        vr::VR_Shutdown();
        return;
    }
    
    const char* appKey = "lighthouse-manager-linux";
    bool alreadyInstalled = vrApplications->IsApplicationInstalled(appKey);
    
    if (alreadyInstalled)
    {
        if (verbose)
        {
            std::cout << "Manifest already registered\n";
        }
    }
    else
    {
        char exePath[PATH_MAX];
        ssize_t count = readlink("/proc/self/exe", exePath, PATH_MAX);
        if (count == -1)
        {
            if (verbose)
            {
                std::cerr << "Failed to determine executable path\n";
            }
            vr::VR_Shutdown();
            return;
        }
        
        exePath[count] = '\0';
        std::string exeDir = std::string(exePath);
        size_t lastSlash = exeDir.find_last_of('/');
        if (lastSlash != std::string::npos)
        {
            exeDir = exeDir.substr(0, lastSlash);
        }
        
        std::string manifestPath = exeDir + "/manifest.vrmanifest";
        
        if (verbose)
        {
            std::cout << "Registering manifest: " << manifestPath << "\n";
        }
        
        vr::EVRApplicationError appError = vrApplications->AddApplicationManifest(manifestPath.c_str());
        if (appError != vr::VRApplicationError_None)
        {
            if (verbose)
            {
                std::cerr << "Failed to add manifest: " << vrApplications->GetApplicationsErrorNameFromEnum(appError) << "\n";
                std::cerr << "Manifest path: " << manifestPath << "\n";
                std::cerr << "App key: " << appKey << "\n";
            }
            vr::VR_Shutdown();
            return;
        }
        
        if (verbose)
        {
            std::cout << "Manifest registered successfully\n";
        }
    }
    
    vr::EVRApplicationError autoLaunchError = vrApplications->SetApplicationAutoLaunch(appKey, true);
    if (autoLaunchError != vr::VRApplicationError_None)
    {
        if (verbose)
        {
            std::cerr << "Warning: Failed to enable auto-launch: " << vrApplications->GetApplicationsErrorNameFromEnum(autoLaunchError) << "\n";
        }
    }
    else if (verbose)
    {
        std::cout << "Auto-launch enabled\n";
        std::cout << "App key: " << appKey << "\n";
    }
    
    vr::VR_Shutdown();
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        if (IsLaunchedBySteamVR())
        {
            RegisterManifest(false);
            AutoManage();
            return 0;
        }
        
        RegisterManifest(false);
        PrintUsage(argv[0]);
        return 1;
    }
    
    std::string command = argv[1];
    
    if (command == "--help" || command == "-h")
    {
        PrintUsage(argv[0]);
        return 0;
    }
    else if (command == "--list" || command == "--scan")
    {
        ListBaseStations();
    }
    else if (command == "--enable" || command == "--wake")
    {
        if (argc < 3)
        {
            std::cerr << "Error: Base station ID required\n";
            PrintUsage(argv[0]);
            return 1;
        }
        ControlBaseStation(argv[2], BaseStationCommand::Wake);
    }
    else if (command == "--disable" || command == "--sleep")
    {
        if (argc < 3)
        {
            std::cerr << "Error: Base station ID required\n";
            PrintUsage(argv[0]);
            return 1;
        }
        ControlBaseStation(argv[2], BaseStationCommand::Sleep);
    }
    else if (command == "--standby")
    {
        if (argc < 3)
        {
            std::cerr << "Error: Base station ID required\n";
            PrintUsage(argv[0]);
            return 1;
        }
        ControlBaseStation(argv[2], BaseStationCommand::Standby);
    }
    else if (command == "--auto")
    {
        AutoManage();
    }
    else if (command == "--register-manifest")
    {
        RegisterManifest(true);
    }
    else if (command == "--check-registration")
    {
        vr::EVRInitError initError = vr::VRInitError_None;
        vr::IVRSystem* vrSystem = vr::VR_Init(&initError, vr::VRApplication_Utility);
        
        if (initError != vr::VRInitError_None)
        {
            std::cerr << "Failed to initialize OpenVR: " << vr::VR_GetVRInitErrorAsEnglishDescription(initError) << "\n";
            std::cerr << "Make sure SteamVR is running\n";
            return 1;
        }
        
        vr::IVRApplications* vrApplications = vr::VRApplications();
        if (!vrApplications)
        {
            std::cerr << "Failed to get VRApplications interface\n";
            vr::VR_Shutdown();
            return 1;
        }
        
        const char* appKey = "lighthouse-manager-linux";
        bool isInstalled = vrApplications->IsApplicationInstalled(appKey);
        
        std::cout << "Registration Status:\n";
        std::cout << "  App Key: " << appKey << "\n";
        std::cout << "  Registered: " << (isInstalled ? "Yes" : "No") << "\n";
        
        if (isInstalled)
        {
            bool autoLaunch = vrApplications->GetApplicationAutoLaunch(appKey);
            std::cout << "  Auto-launch: " << (autoLaunch ? "Enabled" : "Disabled") << "\n";
            
            char workingDir[1024];
            vr::EVRApplicationError error;
            uint32_t size = vrApplications->GetApplicationPropertyString(appKey, vr::VRApplicationProperty_WorkingDirectory_String, workingDir, sizeof(workingDir), &error);
            if (error == vr::VRApplicationError_None && size > 0)
            {
                std::cout << "  Working Directory: " << workingDir << "\n";
            }
        }
        else
        {
            std::cout << "\nTo register, run:\n";
            std::cout << "  lighthouse-manager --register-manifest\n";
        }
        
        vr::VR_Shutdown();
        return isInstalled ? 0 : 1;
    }
    else if (command == "--gui")
    {
        std::cout << "GUI mode not yet implemented. Use CLI mode for now.\n";
        return 1;
    }
    else
    {
        std::cerr << "Unknown command: " << command << "\n\n";
        PrintUsage(argv[0]);
        return 1;
    }
    
    return 0;
}

