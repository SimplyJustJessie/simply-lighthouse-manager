#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

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

namespace bluez
{
class Client;
}

class BaseStationDetector
{
public:
    BaseStationDetector();
    ~BaseStationDetector();

    bool Initialize();
    void Shutdown();

    // Lists base stations BlueZ already knows, then runs an LE discovery for
    // up to timeoutSeconds picking up stations as they advertise. A provided
    // shouldCancel callback is checked about once a second and ends the scan
    // early (returning what was found so far). onProgress, when provided, is
    // invoked with the stations found so far - immediately after the known
    // devices are listed and again after each discovery tick.
    std::vector<BaseStationInfo> ScanForBaseStations(
        int timeoutSeconds = 10, const std::function<bool()>& shouldCancel = {},
        const std::function<void(const std::vector<BaseStationInfo>&)>& onProgress = {});

    // Stations BlueZ already knows (paired or previously seen) - instant, no
    // radio discovery.
    std::vector<BaseStationInfo> ListKnownStations();

    bool IsBluetoothAvailable() const;

private:
    std::unique_ptr<bluez::Client> client;
    std::string adapterPath;
    bool initialized;

    void CollectStations(std::vector<BaseStationInfo>& stations);
};
