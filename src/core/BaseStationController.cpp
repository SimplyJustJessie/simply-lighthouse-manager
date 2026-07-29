#include "BaseStationController.h"

#include <dbus/dbus.h>
#include <cctype>
#include <chrono>
#include <iostream>
#include <thread>

#include "BlueZClient.h"
#include "StationClassifier.h"

namespace
{

std::string ToLowerCopy(std::string s)
{
    for (char& c : s)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

}  // namespace

BaseStationController::BaseStationController() : connected(false)
{
    client = std::make_unique<bluez::Client>();
}

BaseStationController::~BaseStationController()
{
    Disconnect();
}

bool BaseStationController::Connect(const BaseStationInfo& station,
                                    const std::function<bool()>& shouldAbort)
{
    stationInfo = station;

    if (station.address.empty())
    {
        std::cerr << "Error: Base station address is empty\n";
        return false;
    }

    if (!client->IsValid())
    {
        std::cerr << "Error: D-Bus connection not available\n";
        return false;
    }

    devicePath = client->FindDeviceByAddress(station.address);
    if (devicePath.empty())
    {
        std::cerr << "Error: BlueZ does not know device " << station.address
                  << " (run a scan first)\n";
        return false;
    }

    connected = ConnectToDevice(shouldAbort);
    if (connected)
    {
        // Keep the station in BlueZ's persistent storage so future runs can
        // connect immediately without waiting for a discovery scan.
        client->SetDeviceTrusted(devicePath, true);
    }
    return connected;
}

void BaseStationController::Disconnect()
{
    if (connected && !devicePath.empty() && client && client->IsValid())
    {
        client->DisconnectDevice(devicePath);
    }
    connected = false;
}

bool BaseStationController::ConnectToDevice(const std::function<bool()>& shouldAbort)
{
    // Growing backoff: when the adapter is busy (connecting to another
    // station or scanning), BlueZ rejects connects instantly - rapid-fire
    // retries just burn through the attempts inside the same busy window.
    const int retryCount = 5;
    for (int i = 0; i < retryCount; i++)
    {
        if (shouldAbort && shouldAbort())
        {
            return false;
        }
        if (i > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500 * i));
        }

        if (client->ConnectDevice(devicePath))
        {
            // Give BlueZ a moment to settle the connection before GATT access.
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            return true;
        }
    }

    std::cerr << "Failed to connect to " << stationInfo.address
              << " after " << retryCount << " attempts\n";
    return false;
}

bool BaseStationController::WaitForServicesResolved()
{
    const int maxWaitTicks = 50;  // 50 x 100ms = 5s
    for (int i = 0; i < maxWaitTicks; i++)
    {
        bool resolved = false;
        if (client->GetBoolProperty(devicePath, "org.bluez.Device1", "ServicesResolved", resolved) &&
            resolved)
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

std::string BaseStationController::FindServicePath(const std::string& serviceUuid)
{
    if (!WaitForServicesResolved())
    {
        // Fall through - some stations expose GATT without flipping the flag.
    }

    const std::string target = ToLowerCopy(serviceUuid);

    for (const auto& [path, interfaces] : client->GetManagedObjects())
    {
        if (path.rfind(devicePath + "/", 0) != 0)
        {
            continue;
        }
        auto it = interfaces.find("org.bluez.GattService1");
        if (it == interfaces.end())
        {
            continue;
        }
        const std::string* uuid = it->second.GetString("UUID");
        if (uuid && ToLowerCopy(*uuid) == target)
        {
            return path;
        }
    }
    return "";
}

std::string BaseStationController::FindCharacteristicPath(const std::string& servicePath,
                                                          const std::string& charUuid)
{
    const std::string target = ToLowerCopy(charUuid);

    for (const auto& [path, interfaces] : client->GetManagedObjects())
    {
        if (path.rfind(servicePath + "/", 0) != 0)
        {
            continue;
        }
        auto it = interfaces.find("org.bluez.GattCharacteristic1");
        if (it == interfaces.end())
        {
            continue;
        }
        const std::string* uuid = it->second.GetString("UUID");
        if (uuid && ToLowerCopy(*uuid) == target)
        {
            return path;
        }
    }
    return "";
}

bool BaseStationController::WriteCharacteristicValue(const std::string& charPath,
                                                     const uint8_t* data, size_t dataLen)
{
    DBusMessage* msg = dbus_message_new_method_call(
        "org.bluez",
        charPath.c_str(),
        "org.bluez.GattCharacteristic1",
        "WriteValue");

    if (!msg)
    {
        return false;
    }

    DBusMessageIter iter, arrayIter;
    dbus_message_iter_init_append(msg, &iter);

    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "y", &arrayIter);
    for (size_t i = 0; i < dataLen; i++)
    {
        unsigned char byte = static_cast<unsigned char>(data[i]);
        dbus_message_iter_append_basic(&arrayIter, DBUS_TYPE_BYTE, &byte);
    }
    dbus_message_iter_close_container(&iter, &arrayIter);

    DBusMessageIter dictIter;
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dictIter);
    dbus_message_iter_close_container(&iter, &dictIter);

    DBusError err;
    dbus_error_init(&err);

    DBusMessage* reply = dbus_connection_send_with_reply_and_block(
        client->Raw(), msg, 5000, &err);

    dbus_message_unref(msg);

    if (dbus_error_is_set(&err))
    {
        std::cerr << "D-Bus error writing characteristic: " << err.name
                  << " - " << err.message << std::endl;
        dbus_error_free(&err);
        return false;
    }

    if (reply)
    {
        dbus_message_unref(reply);
        return true;
    }

    return false;
}

bool BaseStationController::WriteV2PowerCharacteristic(uint8_t value)
{
    if (!EnsureConnection())
    {
        return false;
    }

    std::string servicePath = FindServicePath(LIGHTHOUSE_V2_SERVICE_UUID);
    if (servicePath.empty())
    {
        std::cerr << "Failed to find GATT service\n";
        return false;
    }

    std::string charPath = FindCharacteristicPath(servicePath, V2_POWER_CHAR_UUID);
    if (charPath.empty())
    {
        std::cerr << "Failed to find power characteristic\n";
        return false;
    }

    return WriteCharacteristicValue(charPath, &value, 1);
}

bool BaseStationController::EnsureConnection()
{
    if (!client->IsValid())
    {
        std::cerr << "D-Bus connection not available\n";
        return false;
    }

    if (devicePath.empty())
    {
        return false;
    }

    // Do not trust the local flag: BLE links drop silently mid-session, and
    // BlueZ removes the GATT objects when they do. Check the real state and
    // reconnect so keep-alives and the sleep-on-exit path keep working.
    bool actuallyConnected = false;
    if (client->GetBoolProperty(devicePath, "org.bluez.Device1", "Connected", actuallyConnected) &&
        actuallyConnected)
    {
        connected = true;
        return true;
    }

    if (connected)
    {
        std::cerr << "Connection to " << stationInfo.address << " dropped - reconnecting\n";
    }
    connected = ConnectToDevice();
    return connected;
}

bool BaseStationController::SendCommand(BaseStationCommand command, int retryRounds,
                                        const std::function<bool()>& shouldAbort)
{
    uint8_t value = static_cast<uint8_t>(command);

    bool isV1 = stationInfo.name.rfind("HTC BS", 0) == 0 ||
                stationInfo.name.rfind("VIVE BS", 0) == 0;

    if (isV1)
    {
        std::cerr << "V1 base station control not yet implemented\n";
        return false;
    }

    const int retryCount = retryRounds > 0 ? retryRounds : 1;
    bool success = false;

    for (int i = 0; i < retryCount; i++)
    {
        if (shouldAbort && shouldAbort())
        {
            return false;
        }
        if (i > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        // Stations occasionally drop the first write after connecting; send a
        // short burst and count any accepted write as success.
        bool firstAttempt = WriteV2PowerCharacteristic(value);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        bool secondAttempt = WriteV2PowerCharacteristic(value);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        bool thirdAttempt = WriteV2PowerCharacteristic(value);

        if (firstAttempt || secondAttempt || thirdAttempt)
        {
            success = true;
            break;
        }
    }

    if (!success)
    {
        std::cerr << "Failed to write power characteristic after " << retryCount << " retries\n";
    }

    return success;
}

bool BaseStationController::Wake(int retryRounds, const std::function<bool()>& shouldAbort)
{
    return SendCommand(BaseStationCommand::Wake, retryRounds, shouldAbort);
}

bool BaseStationController::Sleep(int retryRounds)
{
    return SendCommand(BaseStationCommand::Sleep, retryRounds);
}

bool BaseStationController::Standby()
{
    return SendCommand(BaseStationCommand::Standby);
}

bool BaseStationController::SendWakePacket()
{
    if (!connected)
    {
        return false;
    }

    bool isV1 = stationInfo.name.rfind("HTC BS", 0) == 0 ||
                stationInfo.name.rfind("VIVE BS", 0) == 0;

    if (isV1)
    {
        return false;
    }

    uint8_t value = static_cast<uint8_t>(BaseStationCommand::Wake);
    return WriteV2PowerCharacteristic(value);
}
