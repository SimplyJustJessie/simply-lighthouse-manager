#pragma once

#include <string>
#include <vector>
#include <memory>

struct BaseStationInfo
{
    std::string id;
    std::string name;
    std::string address;
    std::string serial;
    bool isBaseStation1;
    bool isConnected;
    bool isPowered;
    
    BaseStationInfo() : isBaseStation1(false), isConnected(false), isPowered(false) {}
};

class BaseStationDetector
{
public:
    BaseStationDetector();
    ~BaseStationDetector();
    
    bool Initialize();
    void Shutdown();
    
    std::vector<BaseStationInfo> ScanForBaseStations(int timeoutSeconds = 10);
    bool ConnectToBaseStation(const std::string& address);
    void DisconnectFromBaseStation(const std::string& address);
    bool CheckBaseStationStatus(BaseStationInfo& station);
    
    bool IsBluetoothAvailable() const;
    
private:
    int bluetoothAdapter;
    bool initialized;
    
    bool CheckBluetoothAdapter();
    BaseStationInfo ParseAdvertisementData(const std::string& address, const void* data, size_t length);
};

