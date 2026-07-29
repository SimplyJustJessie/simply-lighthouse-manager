#pragma once

#include <cstdint>
#include <functional>
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

    // shouldAbort, when provided, is checked between retry attempts and ends
    // the operation early (reported as failure) - used so a SteamVR quit
    // during the wake phase does not stall shutdown on a slow station.
    bool Connect(const BaseStationInfo& station, const std::function<bool()>& shouldAbort = {});
    void Disconnect();

    // retryRounds bounds the outer retry loop; the default favors
    // reliability, exit paths pass a small value to stay within SteamVR's
    // shutdown grace period.
    bool SendCommand(BaseStationCommand command, int retryRounds = 10,
                     const std::function<bool()>& shouldAbort = {});
    bool Wake(int retryRounds = 10, const std::function<bool()>& shouldAbort = {});
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
    bool ConnectToDevice(const std::function<bool()>& shouldAbort = {});
    bool WaitForServicesResolved();
    std::string FindServicePath(const std::string& serviceUuid);
    std::string FindCharacteristicPath(const std::string& servicePath, const std::string& charUuid);
    bool WriteCharacteristicValue(const std::string& charPath, const uint8_t* data, size_t dataLen);
    bool WriteV2PowerCharacteristic(uint8_t value);
};
