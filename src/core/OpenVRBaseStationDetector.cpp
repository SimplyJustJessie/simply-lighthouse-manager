#include "OpenVRBaseStationDetector.h"
#include <openvr.h>
#include <iostream>

std::vector<BaseStationInfo> OpenVRBaseStationDetector::DetectViaOpenVR()
{
    std::vector<BaseStationInfo> stations;
    
    vr::EVRInitError initError = vr::VRInitError_None;
    vr::IVRSystem* vrSystem = vr::VR_Init(&initError, vr::VRApplication_Background);
    
    if (initError != vr::VRInitError_None)
    {
        std::cerr << "OpenVR not available: " << vr::VR_GetVRInitErrorAsEnglishDescription(initError) << "\n";
        return stations;
    }
    
    char buffer[vr::k_unMaxPropertyStringSize] = {};
    
    for (uint32_t id = 0; id < vr::k_unMaxTrackedDeviceCount; ++id)
    {
        vr::ETrackedPropertyError err = vr::TrackedProp_Success;
        auto deviceClass = vrSystem->GetTrackedDeviceClass(id);
        
        if (deviceClass == vr::TrackedDeviceClass_TrackingReference)
        {
            BaseStationInfo station;
            station.id = std::to_string(id);
            
            vrSystem->GetStringTrackedDeviceProperty(id, vr::Prop_ModelNumber_String, buffer, sizeof(buffer), &err);
            if (err == vr::TrackedProp_Success)
            {
                station.name = std::string(buffer);
            }
            
            vrSystem->GetStringTrackedDeviceProperty(id, vr::Prop_SerialNumber_String, buffer, sizeof(buffer), &err);
            if (err == vr::TrackedProp_Success)
            {
                station.serial = std::string(buffer);
                if (station.id.empty())
                {
                    station.id = station.serial;
                }
            }
            
            vrSystem->GetStringTrackedDeviceProperty(id, vr::Prop_TrackingSystemName_String, buffer, sizeof(buffer), &err);
            if (err == vr::TrackedProp_Success)
            {
                std::string system(buffer);
                station.isBaseStation1 = (system.find("lighthouse") != std::string::npos);
            }
            
            if (!station.name.empty() || !station.serial.empty())
            {
                stations.push_back(station);
            }
        }
    }
    
    vr::VR_Shutdown();
    return stations;
}

