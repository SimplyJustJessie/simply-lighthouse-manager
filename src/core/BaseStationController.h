#pragma once

#include "BaseStationDetector.h"
#include <string>
#include <cstdint>
#include <memory>

enum class BaseStationCommand
{
    Wake = 0x01,
    Sleep = 0x00,
    Standby = 0x02
};

class BaseStationController
{
public:
    BaseStationController();
    ~BaseStationController();
    
    bool Connect(const BaseStationInfo& station);
    void Disconnect();
    
    bool SendCommand(BaseStationCommand command);
    bool Wake();
    bool Sleep();
    bool Standby();
    bool SendWakePacket();
    
    bool IsConnected() const { return connected; }
    const BaseStationInfo& GetStationInfo() const { return stationInfo; }
    
private:
    BaseStationInfo stationInfo;
    bool connected;
    
    static constexpr const char* V2_SERVICE_UUID = "00001523-1212-efde-1523-785feabcd124";
    static constexpr const char* V2_POWER_CHAR_UUID = "00001525-1212-efde-1523-785feabcd124";
    
    static constexpr const char* V1_SERVICE_UUID = "0000cb00-0000-1000-8000-00805f9b34fb";
    static constexpr const char* V1_POWER_CHAR_UUID = "0000cb01-0000-1000-8000-00805f9b34fb";
    
    struct DBusConnWrapper;
    std::unique_ptr<DBusConnWrapper> dbusConn;
    
    bool EnsureConnection();
    bool ConnectToDevice();
    bool WaitForServicesResolved();
    std::string GetDevicePath();
    std::string FindServicePath(const std::string& serviceUuid);
    std::string FindCharacteristicPath(const std::string& servicePath, const std::string& charUuid);
    bool WriteCharacteristicValue(const std::string& charPath, const uint8_t* data, size_t dataLen);
    bool WriteV2PowerCharacteristic(uint8_t value);
    bool WriteV1PowerCharacteristic(const uint8_t* data, size_t dataLen);
};
