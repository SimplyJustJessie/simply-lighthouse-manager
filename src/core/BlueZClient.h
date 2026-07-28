#pragma once

#include <map>
#include <string>
#include <vector>

struct DBusConnection;
struct DBusMessage;

namespace bluez
{

// Subset of D-Bus property values BlueZ exposes that we care about.
struct Properties
{
    std::map<std::string, std::string> strings;                    // STRING / OBJECT_PATH
    std::map<std::string, bool> booleans;                          // BOOLEAN
    std::map<std::string, std::vector<std::string>> stringArrays;  // ARRAY of STRING (e.g. UUIDs)

    const std::string* GetString(const std::string& key) const;
    bool GetBool(const std::string& key, bool fallback = false) const;
};

// object path -> interface name -> properties
using ObjectMap = std::map<std::string, std::map<std::string, Properties>>;

class Client
{
public:
    Client();
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    bool IsValid() const { return conn != nullptr; }

    // org.freedesktop.DBus.ObjectManager.GetManagedObjects on org.bluez
    ObjectMap GetManagedObjects();

    std::vector<std::string> FindAdapters();
    // First powered adapter; empty string if none (logs why).
    std::string DefaultAdapter();

    // Empty string if BlueZ does not know the device.
    std::string FindDeviceByAddress(const std::string& address);

    // LE-filtered discovery. An "operation already in progress" error is
    // treated as success so two processes (GUI + auto service) can overlap.
    bool StartDiscovery(const std::string& adapterPath);
    void StopDiscovery(const std::string& adapterPath);

    bool ConnectDevice(const std::string& devicePath);     // AlreadyConnected == success
    bool DisconnectDevice(const std::string& devicePath);  // NotConnected == success

    bool GetBoolProperty(const std::string& path, const char* iface, const char* prop, bool& out);

    DBusConnection* Raw() { return conn; }

private:
    DBusConnection* conn;

    // Sends msg (consumed) and returns the reply, or nullptr. On error the
    // D-Bus error name is written to errorName when provided.
    DBusMessage* Call(DBusMessage* msg, int timeoutMs, std::string* errorName = nullptr);
    bool CallSimple(const char* path, const char* iface, const char* method,
                    int timeoutMs, const char* okError = nullptr);
};

// Starts discovery on construction, stops it on destruction.
class DiscoveryGuard
{
public:
    DiscoveryGuard(Client& client, const std::string& adapterPath);
    ~DiscoveryGuard();
    bool Active() const { return active; }

private:
    Client& client;
    std::string adapter;
    bool active;
};

}  // namespace bluez
