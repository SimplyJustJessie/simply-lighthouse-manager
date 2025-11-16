#include "SteamVRMonitor.h"
#include <openvr.h>
#include <iostream>

SteamVRMonitor::SteamVRMonitor() : vrSystem(nullptr), monitoring(false), steamVRRunning(false)
{
}

SteamVRMonitor::~SteamVRMonitor()
{
    Shutdown();
}

bool SteamVRMonitor::Initialize()
{
    vr::EVRInitError initError = vr::VRInitError_None;
    vrSystem = vr::VR_Init(&initError, vr::VRApplication_Background);
    
    if (initError != vr::VRInitError_None)
    {
        vrSystem = nullptr;
        return false;
    }
    
    return true;
}

void SteamVRMonitor::Shutdown()
{
    StopMonitoring();
    if (vrSystem)
    {
        vr::VR_Shutdown();
        vrSystem = nullptr;
    }
}

void SteamVRMonitor::StartMonitoring()
{
    if (monitoring)
        return;
    
    monitoring = true;
    monitorThread = std::thread(&SteamVRMonitor::MonitorLoop, this);
}

void SteamVRMonitor::StopMonitoring()
{
    if (!monitoring)
        return;
    
    monitoring = false;
    if (monitorThread.joinable())
    {
        monitorThread.join();
    }
}

bool SteamVRMonitor::IsSteamVRRunning() const
{
    std::lock_guard<std::mutex> lock(stateMutex);
    return steamVRRunning;
}

void SteamVRMonitor::SetStateChangeCallback(StateChangeCallback callback)
{
    stateCallback = callback;
}

std::vector<std::string> SteamVRMonitor::GetDetectedBaseStations() const
{
    std::vector<std::string> stations;
    
    if (!vrSystem)
        return stations;
    
    for (uint32_t id = 0; id < vr::k_unMaxTrackedDeviceCount; ++id)
    {
        auto deviceClass = vrSystem->GetTrackedDeviceClass(id);
        if (deviceClass == vr::TrackedDeviceClass_TrackingReference)
        {
            char buffer[vr::k_unMaxPropertyStringSize];
            vr::ETrackedPropertyError err;
            vrSystem->GetStringTrackedDeviceProperty(id, vr::Prop_SerialNumber_String, buffer, sizeof(buffer), &err);
            if (err == vr::TrackedProp_Success)
            {
                stations.push_back(std::string(buffer));
            }
        }
    }
    
    return stations;
}

void SteamVRMonitor::MonitorLoop()
{
    bool lastState = false;
    
    while (monitoring)
    {
        bool currentState = CheckSteamVRStatus();
        
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            steamVRRunning = currentState;
        }
        
        if (currentState != lastState)
        {
            NotifyStateChange(currentState);
            lastState = currentState;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

bool SteamVRMonitor::CheckSteamVRStatus()
{
    if (!vrSystem)
    {
        vr::EVRInitError initError = vr::VRInitError_None;
        vrSystem = vr::VR_Init(&initError, vr::VRApplication_Background);
        if (initError != vr::VRInitError_None)
        {
            vrSystem = nullptr;
            return false;
        }
    }
    
    vr::VREvent_t event;
    while (vrSystem->PollNextEvent(&event, sizeof(event)))
    {
        if (event.eventType == vr::VREvent_Quit)
        {
            return false;
        }
    }
    
    return true;
}

void SteamVRMonitor::NotifyStateChange(bool isRunning)
{
    if (stateCallback)
    {
        stateCallback(isRunning);
    }
}

