#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "BaseStationDetector.h"

namespace bluez
{
class Client;
}

enum class ChannelSetResult
{
    Confirmed,           // station is advertising the new channel
    WrittenUnconfirmed,  // write accepted, not seen yet (applies on restart)
    Failed,
};

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

    // Sets the RF channel (1-16, Base Station 2.0 only). Stations latch a new
    // channel when they restart, so this power-cycles the station and then
    // waits for it to advertise the new channel. A station that does not
    // advertise it within the timeout yields WrittenUnconfirmed rather than a
    // false success - the change usually appears after the next restart.
    ChannelSetResult SetChannel(int channel);

    bool IsConnected() const { return connected; }
    const BaseStationInfo& GetStationInfo() const { return stationInfo; }

private:
    BaseStationInfo stationInfo;
    std::string devicePath;
    bool connected;

    static constexpr const char* V2_POWER_CHAR_UUID = "00001525-1212-efde-1523-785feabcd124";
    static constexpr const char* V2_CHANNEL_CHAR_UUID = "00001524-1212-efde-1523-785feabcd124";
    static constexpr const char* V1_POWER_CHAR_UUID = "0000cb01-0000-1000-8000-00805f9b34fb";

    std::unique_ptr<bluez::Client> client;

    bool EnsureConnection();
    bool ConnectToDevice(const std::function<bool()>& shouldAbort = {});
    bool WaitForServicesResolved();
    std::string FindServicePath(const std::string& serviceUuid);
    std::string FindCharacteristicPath(const std::string& servicePath, const std::string& charUuid);
    bool WriteCharacteristicValue(const std::string& charPath, const uint8_t* data, size_t dataLen);
    bool WriteV2PowerCharacteristic(uint8_t value);
    std::string FindChannelCharacteristic();
    bool PowerCycle();
    bool WaitForAdvertisedChannel(int expected, int seconds);
};
