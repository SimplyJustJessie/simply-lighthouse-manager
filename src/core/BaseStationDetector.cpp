#include "BaseStationDetector.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

#include "BlueZClient.h"
#include "StationClassifier.h"

BaseStationDetector::BaseStationDetector() : initialized(false)
{
}

BaseStationDetector::~BaseStationDetector()
{
    Shutdown();
}

bool BaseStationDetector::Initialize()
{
    if (initialized)
    {
        return true;
    }

    client = std::make_unique<bluez::Client>();
    if (!client->IsValid())
    {
        client.reset();
        return false;
    }

    adapterPath = client->DefaultAdapter();
    if (adapterPath.empty())
    {
        client.reset();
        return false;
    }

    initialized = true;
    return true;
}

void BaseStationDetector::Shutdown()
{
    client.reset();
    adapterPath.clear();
    initialized = false;
}

bool BaseStationDetector::IsBluetoothAvailable() const
{
    return initialized;
}

void BaseStationDetector::CollectStations(std::vector<BaseStationInfo>& stations)
{
    for (const auto& [path, interfaces] : client->GetManagedObjects())
    {
        auto it = interfaces.find("org.bluez.Device1");
        if (it == interfaces.end())
        {
            continue;
        }

        const bluez::Properties& props = it->second;
        const std::string* address = props.GetString("Address");
        if (!address)
        {
            continue;
        }

        std::string name;
        if (const std::string* n = props.GetString("Name"))
        {
            name = *n;
        }
        else if (const std::string* alias = props.GetString("Alias"))
        {
            name = *alias;
        }

        std::vector<std::string> uuids;
        auto uuidIt = props.stringArrays.find("UUIDs");
        if (uuidIt != props.stringArrays.end())
        {
            uuids = uuidIt->second;
        }

        auto station = ClassifyStation(*address, name, uuids);
        if (!station)
        {
            continue;
        }

        bool known = std::any_of(stations.begin(), stations.end(),
                                 [&](const BaseStationInfo& s) { return s.address == station->address; });
        if (!known)
        {
            stations.push_back(*station);
        }
    }
}

std::vector<BaseStationInfo> BaseStationDetector::ScanForBaseStations(
    int timeoutSeconds, const std::function<bool()>& shouldCancel)
{
    std::vector<BaseStationInfo> stations;

    if (!initialized && !Initialize())
    {
        std::cerr << "Bluetooth adapter not available\n";
        return stations;
    }

    // Stations BlueZ already knows about (paired or recently seen).
    CollectStations(stations);

    // Live LE discovery to pick up stations that are advertising.
    bluez::DiscoveryGuard discovery(*client, adapterPath);
    if (!discovery.Active())
    {
        std::cerr << "Bluetooth discovery could not be started; "
                     "listing already-known devices only\n";
        return stations;
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (shouldCancel && shouldCancel())
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        CollectStations(stations);
    }

    return stations;
}
