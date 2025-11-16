#pragma once

#include "BaseStationDetector.h"
#include <string>
#include <vector>

class BluetoothLEDetector
{
public:
    static std::vector<BaseStationInfo> ScanForLighthouses(int timeoutSeconds = 10);
    
private:
    static constexpr const char* LIGHTHOUSE_SERVICE_UUID = "00001523-1212-efde-1523-785feabcd123";
    static constexpr const char* LIGHTHOUSE_SERVICE_UUID_SHORT = "1523";
    
    static bool CheckAdvertisementData(const std::string& address, const std::string& name, const std::string& serviceData);
    static BaseStationInfo ParseDeviceInfo(const std::string& address, const std::string& name);
};

