#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "BaseStationDetector.h"

namespace bluez
{
class Client;
}

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

    // retryRounds bounds the outer retry loop; the default favors
    // reliability, exit paths pass a small value to stay within SteamVR's
    // shutdown grace period.
    bool SendCommand(BaseStationCommand command, int retryRounds = 10);
    bool Wake();
    bool Sleep(int retryRounds = 10);
    bool Standby();
    bool SendWakePacket();

    bool IsConnected() const { return connected; }
    const BaseStationInfo& GetStationInfo() const { return stationInfo; }

private:
    BaseStationInfo stationInfo;
    std::string devicePath;
    bool connected;

    static constexpr const char* V2_POWER_CHAR_UUID = "00001525-1212-efde-1523-785feabcd124";
    static constexpr const char* V1_POWER_CHAR_UUID = "0000cb01-0000-1000-8000-00805f9b34fb";

    std::unique_ptr<bluez::Client> client;

    bool EnsureConnection();
    bool ConnectToDevice();
    bool WaitForServicesResolved();
    std::string FindServicePath(const std::string& serviceUuid);
    std::string FindCharacteristicPath(const std::string& servicePath, const std::string& charUuid);
    bool WriteCharacteristicValue(const std::string& charPath, const uint8_t* data, size_t dataLen);
    bool WriteV2PowerCharacteristic(uint8_t value);
};
