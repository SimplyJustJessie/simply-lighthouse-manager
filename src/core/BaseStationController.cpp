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

// Value of one hexadecimal digit, or -1 when the character is not one.
int HexDigit(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

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
    // ConnectDevice waits out bluetoothd's full connect attempt (abortable),
    // so one patient attempt beats a retry storm: retries against an attempt
    // that is still running inside the daemon just collide with it
    // (org.bluez.Error.InProgress). One short second chance covers hiccups.
    const int retryCount = 2;
    for (int i = 0; i < retryCount; i++)
    {
        if (shouldAbort && shouldAbort())
        {
            return false;
        }
        if (i > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }

        if (client->ConnectDevice(devicePath, shouldAbort))
        {
            // Give BlueZ a moment to settle the connection before GATT access.
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            return true;
        }
    }

    std::cerr << "Failed to connect to " << stationInfo.address << "\n";
    return false;
}

bool BaseStationController::WaitForServicesResolved()
{
    auto start = std::chrono::steady_clock::now();
    const int maxWaitTicks = 50;  // 50 x 100ms = 5s
    for (int i = 0; i < maxWaitTicks; i++)
    {
        bool resolved = false;
        if (client->GetBoolProperty(devicePath, "org.bluez.Device1", "ServicesResolved", resolved) &&
            resolved)
        {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - start)
                          .count();
            if (ms > 1000)
            {
                std::cout << "Services resolved for " << stationInfo.address
                          << " after " << ms << "ms\n";
            }
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cerr << "Timed out waiting for services of " << stationInfo.address << "\n";
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

std::string BaseStationController::FindChannelCharacteristic()
{
    if (!EnsureConnection())
    {
        return "";
    }
    const std::string servicePath = FindServicePath(LIGHTHOUSE_V2_SERVICE_UUID);
    if (servicePath.empty())
    {
        std::cerr << "Failed to find GATT service\n";
        return "";
    }
    return FindCharacteristicPath(servicePath, V2_CHANNEL_CHAR_UUID);
}

// Sleep, pause, wake: the station applies a pending channel change as it
// comes back up (SteamVR restarts base stations for channel changes too).
bool BaseStationController::PowerCycle()
{
    if (!WriteV2PowerCharacteristic(static_cast<uint8_t>(BaseStationCommand::Sleep)))
    {
        return false;
    }
    std::this_thread::sleep_for(std::chrono::seconds(3));
    return WriteV2PowerCharacteristic(static_cast<uint8_t>(BaseStationCommand::Wake));
}

bool BaseStationController::WaitForAdvertisedChannel(int expected, int seconds)
{
    const size_t slash = devicePath.find_last_of('/');
    if (slash == std::string::npos)
    {
        return false;
    }
    const std::string adapterPath = devicePath.substr(0, slash);

    // A connected station stops advertising, and BlueZ only refreshes
    // advertisement properties while a scan is running.
    Disconnect();
    bluez::DiscoveryGuard discovery(*client, adapterPath);

    for (int i = 0; i < seconds; i++)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        for (const auto& [path, interfaces] : client->GetManagedObjects())
        {
            if (path != devicePath)
            {
                continue;
            }
            auto it = interfaces.find("org.bluez.Device1");
            if (it == interfaces.end())
            {
                continue;
            }
            auto mfr = it->second.manufacturerData.find(VALVE_COMPANY_ID);
            if (mfr != it->second.manufacturerData.end() &&
                ChannelFromValveManufacturerData(mfr->second) == expected)
            {
                return true;
            }
        }
    }
    return false;
}

ChannelSetResult BaseStationController::SetChannel(int channel)
{
    if (channel < LIGHTHOUSE_MIN_CHANNEL || channel > LIGHTHOUSE_MAX_CHANNEL)
    {
        std::cerr << "Channel " << channel << " out of range (" << LIGHTHOUSE_MIN_CHANNEL
                  << "-" << LIGHTHOUSE_MAX_CHANNEL << ")\n";
        return ChannelSetResult::Failed;
    }
    if (stationInfo.isBaseStation1)
    {
        std::cerr << "Channel control is only implemented for Base Station 2.0\n";
        return ChannelSetResult::Failed;
    }

    const std::string charPath = FindChannelCharacteristic();
    if (charPath.empty())
    {
        std::cerr << "Failed to find the channel characteristic\n";
        return ChannelSetResult::Failed;
    }

    const uint8_t value = static_cast<uint8_t>(channel);
    if (!WriteCharacteristicValue(charPath, &value, 1))
    {
        return ChannelSetResult::Failed;
    }

    PowerCycle();

    // Verified against the advertisement, never a GATT read-back: BlueZ
    // serves attribute reads from a cache that our own write just populated,
    // so a read-back confirms nothing.
    if (WaitForAdvertisedChannel(channel, 60))
    {
        stationInfo.channel = channel;
        return ChannelSetResult::Confirmed;
    }
    return ChannelSetResult::WrittenUnconfirmed;
}

bool BaseStationController::WritePowerCharacteristic(const char* serviceUuid,
                                                     const char* charUuid, const uint8_t* data,
                                                     size_t dataLen)
{
    if (!EnsureConnection())
    {
        return false;
    }

    std::string servicePath = FindServicePath(serviceUuid);
    if (servicePath.empty())
    {
        std::cerr << "Failed to find GATT service\n";
        return false;
    }

    std::string charPath = FindCharacteristicPath(servicePath, charUuid);
    if (charPath.empty())
    {
        std::cerr << "Failed to find power characteristic\n";
        return false;
    }

    return WriteCharacteristicValue(charPath, data, dataLen);
}

bool BaseStationController::WriteV2PowerCharacteristic(uint8_t value)
{
    return WritePowerCharacteristic(LIGHTHOUSE_V2_SERVICE_UUID, V2_POWER_CHAR_UUID, &value, 1);
}

bool BaseStationController::WriteV1PowerCommand(const uint8_t* command)
{
    return WritePowerCharacteristic(LIGHTHOUSE_V1_SERVICE_UUID, V1_POWER_CHAR_UUID, command,
                                    V1_COMMAND_LENGTH);
}

void BaseStationController::SetV1Id(const std::string& id)
{
    v1Id = id;
}

bool BaseStationController::ParseV1Id(const std::string& text, uint8_t out[4])
{
    if (text.size() != 8)
    {
        return false;
    }
    for (int i = 0; i < 4; i++)
    {
        const int high = HexDigit(text[i * 2]);
        const int low = HexDigit(text[i * 2 + 1]);
        if (high < 0 || low < 0)
        {
            return false;
        }
        out[i] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}

bool BaseStationController::IsV1() const
{
    // The classifier decides this from the advertised name or the 1.0 service
    // UUID; the name prefixes stay as a backstop for BaseStationInfo values
    // that were not built by it.
    return stationInfo.isBaseStation1 || stationInfo.name.rfind("HTC BS", 0) == 0 ||
           stationInfo.name.rfind("VIVE BS", 0) == 0;
}

// Layout as sent by OVR Lighthouse Manager on Windows: a 20 byte frame of
// 0x12, three command bytes, the station ID in reverse byte order, then zero
// padding.
bool BaseStationController::BuildV1Command(BaseStationCommand command,
                                           uint8_t out[V1_COMMAND_LENGTH], bool quiet) const
{
    uint8_t idBytes[4] = {0};
    if (!ParseV1Id(v1Id, idBytes))
    {
        if (!quiet)
        {
            if (v1Id.empty())
            {
                std::cerr << stationInfo.name
                          << " is a Base Station 1.0 - it needs its 8 digit ID before it can "
                             "be controlled (GUI: the ID column; CLI: --set-v1-id <station> "
                             "<id>)\n";
            }
            else
            {
                std::cerr << "Ignoring invalid Base Station 1.0 ID for " << stationInfo.name
                          << ": '" << v1Id << "' (expected 8 hexadecimal digits)\n";
            }
        }
        return false;
    }

    for (size_t i = 0; i < V1_COMMAND_LENGTH; i++)
    {
        out[i] = 0;
    }
    out[0] = 0x12;
    if (command == BaseStationCommand::Wake)
    {
        out[1] = 0x00;
        out[2] = 0x00;
        out[3] = 0x00;
    }
    else
    {
        // 1.0 stations have no standby mode; sleeping is the closest thing,
        // and is what an auto-managed station needs at session end.
        if (command == BaseStationCommand::Standby && !quiet)
        {
            std::cerr << stationInfo.name
                      << ": Base Station 1.0 has no standby mode - sending sleep instead\n";
        }
        out[1] = 0x02;
        out[2] = 0x00;
        out[3] = 0x01;
    }
    out[4] = idBytes[3];
    out[5] = idBytes[2];
    out[6] = idBytes[1];
    out[7] = idBytes[0];
    return true;
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
    const uint8_t value = static_cast<uint8_t>(command);
    const bool isV1 = IsV1();

    // Built once, before the radio is touched: an unusable 1.0 ID is a
    // configuration problem, and retrying it would only delay the failure.
    uint8_t v1Command[V1_COMMAND_LENGTH] = {0};
    if (isV1 && !BuildV1Command(command, v1Command))
    {
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
        auto write = [&] {
            return isV1 ? WriteV1PowerCommand(v1Command) : WriteV2PowerCharacteristic(value);
        };
        bool firstAttempt = write();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        bool secondAttempt = write();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        bool thirdAttempt = write();

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

    // One write, no retries: this runs every few seconds for the whole
    // session, so it stays cheap and quiet (a station that has stopped
    // answering is handled by the caller, not here).
    if (IsV1())
    {
        uint8_t v1Command[V1_COMMAND_LENGTH] = {0};
        if (!BuildV1Command(BaseStationCommand::Wake, v1Command, true))
        {
            return false;
        }
        return WriteV1PowerCommand(v1Command);
    }

    uint8_t value = static_cast<uint8_t>(BaseStationCommand::Wake);
    return WriteV2PowerCharacteristic(value);
}
