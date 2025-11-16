#pragma once

#include <openvr.h>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

class SteamVRMonitor
{
public:
    using StateChangeCallback = std::function<void(bool isRunning)>;
    
    SteamVRMonitor();
    ~SteamVRMonitor();
    
    bool Initialize();
    void Shutdown();
    
    void StartMonitoring();
    void StopMonitoring();
    
    bool IsSteamVRRunning() const;
    
    void SetStateChangeCallback(StateChangeCallback callback);
    
    std::vector<std::string> GetDetectedBaseStations() const;
    
private:
    vr::IVRSystem* vrSystem;
    std::atomic<bool> monitoring;
    std::thread monitorThread;
    mutable std::mutex stateMutex;
    bool steamVRRunning;
    StateChangeCallback stateCallback;
    
    void MonitorLoop();
    bool CheckSteamVRStatus();
    void NotifyStateChange(bool isRunning);
};

