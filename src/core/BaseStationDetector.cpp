#include "BaseStationDetector.h"
#include "BaseStationController.h"
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <ctime>
#include <cstdio>
#include <thread>
#include <chrono>
#include <algorithm>
#include <future>
#include <dbus/dbus.h>
#include <fstream>
#include <set>

BaseStationDetector::BaseStationDetector() : bluetoothAdapter(-1), initialized(false)
{
}

BaseStationDetector::~BaseStationDetector()
{
    Shutdown();
}

bool BaseStationDetector::Initialize()
{
    if (initialized)
        return true;
    
    if (!CheckBluetoothAdapter())
    {
        std::cerr << "No Bluetooth adapter found\n";
        return false;
    }
    
    initialized = true;
    return true;
}

void BaseStationDetector::Shutdown()
{
    if (bluetoothAdapter >= 0)
    {
        hci_close_dev(bluetoothAdapter);
        bluetoothAdapter = -1;
    }
    initialized = false;
}

bool BaseStationDetector::CheckBluetoothAdapter()
{
    int dev_id = hci_get_route(nullptr);
    if (dev_id < 0)
    {
        return false;
    }
    
    int socket = hci_open_dev(dev_id);
    if (socket < 0)
    {
        return false;
    }
    
    bluetoothAdapter = socket;
    return true;
}

std::vector<BaseStationInfo> BaseStationDetector::ScanForBaseStations(int timeoutSeconds)
{
    std::vector<BaseStationInfo> stations;
    
    if (!initialized && !Initialize())
    {
        std::cerr << "Bluetooth adapter not available\n";
        return stations;
    }
    
    FILE* paired_pipe = popen("bluetoothctl devices 2>&1", "r");
    if (paired_pipe)
    {
        char buffer[512];
        while (fgets(buffer, sizeof(buffer), paired_pipe) != nullptr)
        {
            std::string line(buffer);
            
            if (line.find("Device") == 0)
            {
                size_t addr_start = line.find_first_not_of(" \t", 7);
                if (addr_start == std::string::npos) continue;
                
                size_t addr_end = line.find_first_of(" \t", addr_start);
                if (addr_end == std::string::npos) continue;
                
                std::string address = line.substr(addr_start, addr_end - addr_start);
                
                size_t name_start = line.find_first_not_of(" \t", addr_end);
                std::string name;
                if (name_start != std::string::npos)
                {
                    size_t name_end = line.find_first_of("\n\r", name_start);
                    name = line.substr(name_start, name_end - name_start);
                }
                
                if (name.find("LHB-") == 0 || name.find("LHR-") == 0 ||
                    name.find("HTC") != std::string::npos || name.find("VIVE") != std::string::npos ||
                    name.find("Lighthouse") != std::string::npos)
                {
                    BaseStationInfo station;
                    station.address = address;
                    station.name = name.empty() ? "Base Station " + address.substr(12) : name;
                    
                    if (name.find("LHB-") == 0)
                    {
                        station.isBaseStation1 = true;
                        station.id = (name.length() >= 12) ? name.substr(4, 8) : address.substr(12);
                    }
                    else if (name.find("LHR-") == 0)
                    {
                        station.isBaseStation1 = false;
                        station.id = (name.length() >= 12) ? name.substr(4, 8) : address.substr(12);
                    }
                    else
                    {
                        station.isBaseStation1 = (name.find("HTC") != std::string::npos || name.find("VIVE") != std::string::npos);
                        station.id = address.substr(12);
                    }
                    
                    station.serial = station.id;
                    
                    bool found = false;
                    for (const auto& existing : stations)
                    {
                        if (existing.address == station.address)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        stations.push_back(station);
                    }
                }
            }
        }
        pclose(paired_pipe);
    }
    
    std::set<std::string> activelyScanningAddresses;
    std::string scan_cmd = "timeout " + std::to_string(timeoutSeconds) + " bluetoothctl scan on 2>&1";
    FILE* scan_pipe = popen(scan_cmd.c_str(), "r");
    
    if (scan_pipe)
    {
        char buffer[512];
        while (fgets(buffer, sizeof(buffer), scan_pipe) != nullptr)
        {
            std::string line(buffer);
            
            if (line.find("Device") == 0)
            {
                size_t addr_start = line.find_first_not_of(" \t", 7);
                if (addr_start == std::string::npos) continue;
                
                size_t addr_end = line.find_first_of(" \t", addr_start);
                if (addr_end == std::string::npos) continue;
                
                std::string address = line.substr(addr_start, addr_end - addr_start);
                
                size_t name_start = line.find_first_not_of(" \t", addr_end);
                std::string name;
                if (name_start != std::string::npos)
                {
                    size_t name_end = line.find_first_of("\n\r", name_start);
                    name = line.substr(name_start, name_end - name_start);
                }
                
                bool isLighthouse = false;
                if (name.find("LHB-") == 0 || name.find("LHR-") == 0 ||
                    name.find("HTC") != std::string::npos || name.find("VIVE") != std::string::npos ||
                    name.find("Lighthouse") != std::string::npos ||
                    name.find("Base Station") != std::string::npos)
                {
                    isLighthouse = true;
                }
                else if (address.length() >= 17)
                {
                    std::string mac_suffix = address.substr(12);
                    if (mac_suffix.find("00:21") == 0 || mac_suffix.find("00:22") == 0)
                    {
                        isLighthouse = true;
                    }
                }
                
                if (isLighthouse)
                {
                    activelyScanningAddresses.insert(address);
                    
                    BaseStationInfo station;
                    station.address = address;
                    station.name = name.empty() ? "Base Station " + address.substr(12) : name;
                    
                    if (name.find("LHB-") == 0)
                    {
                        station.isBaseStation1 = true;
                        station.id = (name.length() >= 12) ? name.substr(4, 8) : address.substr(12);
                    }
                    else if (name.find("LHR-") == 0)
                    {
                        station.isBaseStation1 = false;
                        station.id = (name.length() >= 12) ? name.substr(4, 8) : address.substr(12);
                    }
                    else
                    {
                        station.isBaseStation1 = (name.find("HTC") != std::string::npos || name.find("VIVE") != std::string::npos);
                        station.id = address.substr(12);
                    }
                    
                    station.serial = station.id;
                    
                    bool found = false;
                    for (const auto& existing : stations)
                    {
                        if (existing.address == station.address)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        stations.push_back(station);
                    }
                }
            }
        }
        pclose(scan_pipe);
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    system("bluetoothctl scan off > /dev/null 2>&1");
    FILE* devices_pipe = popen("bluetoothctl devices 2>&1", "r");
    if (devices_pipe)
    {
        char buffer[512];
        while (fgets(buffer, sizeof(buffer), devices_pipe) != nullptr)
        {
            std::string line(buffer);
            
            if (line.find("Device") == 0)
            {
                size_t addr_start = line.find_first_not_of(" \t", 7);
                if (addr_start == std::string::npos) continue;
                
                size_t addr_end = line.find_first_of(" \t", addr_start);
                if (addr_end == std::string::npos) continue;
                
                std::string address = line.substr(addr_start, addr_end - addr_start);
                
                size_t name_start = line.find_first_not_of(" \t", addr_end);
                std::string name;
                if (name_start != std::string::npos)
                {
                    size_t name_end = line.find_first_of("\n\r", name_start);
                    name = line.substr(name_start, name_end - name_start);
                }
                
                bool isLighthouse = false;
                if (name.find("LHB-") == 0 || name.find("LHR-") == 0 ||
                    name.find("HTC") != std::string::npos || name.find("VIVE") != std::string::npos ||
                    name.find("Lighthouse") != std::string::npos)
                {
                    isLighthouse = true;
                }
                
                if (isLighthouse)
                {
                    BaseStationInfo station;
                    station.address = address;
                    station.name = name.empty() ? "Base Station " + address.substr(12) : name;
                    
                    if (name.find("LHB-") == 0)
                    {
                        station.isBaseStation1 = true;
                        station.id = (name.length() >= 12) ? name.substr(4, 8) : address.substr(12);
                    }
                    else if (name.find("LHR-") == 0)
                    {
                        station.isBaseStation1 = false;
                        station.id = (name.length() >= 12) ? name.substr(4, 8) : address.substr(12);
                    }
                    else
                    {
                        station.isBaseStation1 = (name.find("HTC") != std::string::npos || name.find("VIVE") != std::string::npos);
                        station.id = address.substr(12);
                    }
                    
                    station.serial = station.id;
                    
                    bool found = false;
                    for (const auto& existing : stations)
                    {
                        if (existing.address == station.address)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        stations.push_back(station);
                    }
                }
            }
        }
        pclose(devices_pipe);
    }
    
    
    return stations;
}

BaseStationInfo BaseStationDetector::ParseAdvertisementData(const std::string& address, const void* data, size_t length)
{
    BaseStationInfo station;
    station.address = address;
    
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
    size_t pos = 0;
    
    while (pos < length)
    {
        if (pos + 1 >= length)
            break;
        
        uint8_t ad_len = bytes[pos];
        if (ad_len == 0 || pos + ad_len >= length)
            break;
        
        uint8_t ad_type = bytes[pos + 1];
        
        if (ad_type == 0x09)
        {
            std::string name(reinterpret_cast<const char*>(bytes + pos + 2), ad_len - 1);
            station.name = name;
            
            if (name.find("LHB-") == 0 || name.find("LHR-") == 0)
            {
                station.isBaseStation1 = (name.find("LHB-") == 0);
                if (name.length() >= 8)
                {
                    station.id = name.substr(4, 8);
                }
            }
            else if (name.find("HTC BS") == 0 || name.find("VIVE BS") == 0)
            {
                station.isBaseStation1 = true;
                if (name.length() > 7)
                {
                    station.id = name.substr(name.length() - 8);
                }
            }
        }
        else if (ad_type == 0xFF)
        {
            if (ad_len >= 3 && bytes[pos + 2] == 0x15 && bytes[pos + 3] == 0x23)
            {
                if (station.name.empty())
                {
                    station.name = "Base Station " + address.substr(12);
                }
                if (station.id.empty())
                {
                    station.id = address.substr(12);
                }
                station.isBaseStation1 = false;
            }
        }
        
        pos += ad_len + 1;
    }
    
    return station;
}

bool BaseStationDetector::ConnectToBaseStation(const std::string& address)
{
    return true;
}

bool BaseStationDetector::CheckBaseStationStatus(BaseStationInfo& station)
{
    // Status detection removed - GATT characteristics are write-only
    // Cannot reliably determine lighthouse power state
    return true;
}

void BaseStationDetector::DisconnectFromBaseStation(const std::string& address)
{
}

bool BaseStationDetector::IsBluetoothAvailable() const
{
    return initialized;
}

