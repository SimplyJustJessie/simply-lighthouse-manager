#include "BluetoothLEDetector.h"
#include <iostream>
#include <cstdio>
#include <thread>
#include <chrono>
#include <algorithm>
#include <sstream>

std::vector<BaseStationInfo> BluetoothLEDetector::ScanForLighthouses(int timeoutSeconds)
{
    std::vector<BaseStationInfo> stations;
    
    
    std::string scan_cmd = "timeout " + std::to_string(timeoutSeconds) + " bluetoothctl scan on 2>&1";
    FILE* scan_pipe = popen(scan_cmd.c_str(), "r");
    
    if (scan_pipe)
    {
        char buffer[512];
        while (fgets(buffer, sizeof(buffer), scan_pipe) != nullptr)
        {
        }
        pclose(scan_pipe);
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    system("bluetoothctl scan off > /dev/null 2>&1");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    FILE* devices_pipe = popen("bluetoothctl devices 2>/dev/null", "r");
    if (!devices_pipe)
    {
        std::cerr << "Failed to query bluetoothctl devices\n";
        return stations;
    }
    
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
            
            BaseStationInfo station = ParseDeviceInfo(address, name);
            if (!station.id.empty())
            {
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
    
    return stations;
}

BaseStationInfo BluetoothLEDetector::ParseDeviceInfo(const std::string& address, const std::string& name)
{
    BaseStationInfo station;
    station.address = address;
    station.name = name.empty() ? "Unknown Device" : name;
    
    std::string name_upper = name;
    std::transform(name_upper.begin(), name_upper.end(), name_upper.begin(), ::toupper);
    
    if (name.find("LHB-") == 0)
    {
        station.isBaseStation1 = true;
        if (name.length() >= 12)
        {
            station.id = name.substr(4, 8);
        }
        else
        {
            station.id = address.substr(12);
        }
        station.serial = station.id;
    }
    else if (name.find("LHR-") == 0)
    {
        station.isBaseStation1 = false;
        if (name.length() >= 12)
        {
            station.id = name.substr(4, 8);
        }
        else
        {
            station.id = address.substr(12);
        }
        station.serial = station.id;
    }
    else if (name.find("HTC BS") == 0 || name.find("VIVE BS") == 0 || 
             name.find("HTC Vive") == 0)
    {
        station.isBaseStation1 = true;
        if (name.length() > 7)
        {
            size_t id_start = name.find_last_of(" ");
            if (id_start != std::string::npos && name.length() > id_start + 8)
            {
                station.id = name.substr(id_start + 1, 8);
            }
            else
            {
                station.id = address.substr(12);
            }
        }
        else
        {
            station.id = address.substr(12);
        }
        station.serial = station.id;
    }
    else if (name.find("Lighthouse") != std::string::npos || 
             name.find("Base Station") != std::string::npos)
    {
        station.isBaseStation1 = false;
        station.id = address.substr(12);
        station.serial = station.id;
    }
    else if (address.length() >= 17)
    {
        std::string mac_suffix = address.substr(12);
        if (mac_suffix.find("00:21") == 0 || mac_suffix.find("00:22") == 0)
        {
            station.isBaseStation1 = true;
            station.id = mac_suffix;
            station.serial = station.id;
        }
    }
    
    return station;
}

bool BluetoothLEDetector::CheckAdvertisementData(const std::string& address, const std::string& name, const std::string& serviceData)
{
    if (serviceData.find(LIGHTHOUSE_SERVICE_UUID_SHORT) != std::string::npos)
    {
        return true;
    }
    
    if (name.find("LHB-") == 0 || name.find("LHR-") == 0)
    {
        return true;
    }
    
    if (name.find("HTC") != std::string::npos || name.find("VIVE") != std::string::npos)
    {
        return true;
    }
    
    return false;
}

