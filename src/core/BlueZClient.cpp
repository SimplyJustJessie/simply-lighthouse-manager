#include "BlueZClient.h"

#include <dbus/dbus.h>
#include <cstring>
#include <iostream>

namespace bluez
{

namespace
{

constexpr const char* BLUEZ_BUS = "org.bluez";
constexpr const char* ADAPTER_IFACE = "org.bluez.Adapter1";
constexpr const char* DEVICE_IFACE = "org.bluez.Device1";

Properties ParsePropertyDict(DBusMessageIter* dictIter)
{
    Properties props;

    while (dbus_message_iter_get_arg_type(dictIter) == DBUS_TYPE_DICT_ENTRY)
    {
        DBusMessageIter entry;
        dbus_message_iter_recurse(dictIter, &entry);

        const char* key = nullptr;
        if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING)
        {
            dbus_message_iter_get_basic(&entry, &key);
            dbus_message_iter_next(&entry);

            if (key && dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT)
            {
                DBusMessageIter variant;
                dbus_message_iter_recurse(&entry, &variant);
                int type = dbus_message_iter_get_arg_type(&variant);

                if (type == DBUS_TYPE_STRING || type == DBUS_TYPE_OBJECT_PATH)
                {
                    const char* value = nullptr;
                    dbus_message_iter_get_basic(&variant, &value);
                    if (value)
                    {
                        props.strings[key] = value;
                    }
                }
                else if (type == DBUS_TYPE_BOOLEAN)
                {
                    dbus_bool_t value = FALSE;
                    dbus_message_iter_get_basic(&variant, &value);
                    props.booleans[key] = (value != FALSE);
                }
                else if (type == DBUS_TYPE_ARRAY &&
                         dbus_message_iter_get_element_type(&variant) == DBUS_TYPE_STRING)
                {
                    DBusMessageIter array;
                    dbus_message_iter_recurse(&variant, &array);
                    auto& out = props.stringArrays[key];
                    while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRING)
                    {
                        const char* value = nullptr;
                        dbus_message_iter_get_basic(&array, &value);
                        if (value)
                        {
                            out.push_back(value);
                        }
                        dbus_message_iter_next(&array);
                    }
                }
            }
        }

        dbus_message_iter_next(dictIter);
    }

    return props;
}

}  // namespace

const std::string* Properties::GetString(const std::string& key) const
{
    auto it = strings.find(key);
    return it != strings.end() ? &it->second : nullptr;
}

bool Properties::GetBool(const std::string& key, bool fallback) const
{
    auto it = booleans.find(key);
    return it != booleans.end() ? it->second : fallback;
}

Client::Client() : conn(nullptr)
{
    DBusError err;
    dbus_error_init(&err);
    conn = dbus_bus_get_private(DBUS_BUS_SYSTEM, &err);
    if (dbus_error_is_set(&err))
    {
        std::cerr << "D-Bus connection error: " << err.message << std::endl;
        dbus_error_free(&err);
        conn = nullptr;
    }
    if (conn)
    {
        dbus_connection_set_exit_on_disconnect(conn, FALSE);
    }
}

Client::~Client()
{
    if (conn)
    {
        dbus_connection_close(conn);
        dbus_connection_unref(conn);
    }
}

DBusMessage* Client::Call(DBusMessage* msg, int timeoutMs, std::string* errorName)
{
    if (errorName)
    {
        errorName->clear();
    }
    if (!msg)
    {
        return nullptr;
    }
    if (!conn)
    {
        dbus_message_unref(msg);
        return nullptr;
    }

    DBusError err;
    dbus_error_init(&err);
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, timeoutMs, &err);
    dbus_message_unref(msg);

    if (dbus_error_is_set(&err))
    {
        if (errorName && err.name)
        {
            *errorName = err.name;
        }
        dbus_error_free(&err);
    }
    return reply;
}

bool Client::CallSimple(const char* path, const char* iface, const char* method,
                        int timeoutMs, const char* okError)
{
    DBusMessage* msg = dbus_message_new_method_call(BLUEZ_BUS, path, iface, method);
    std::string errorName;
    DBusMessage* reply = Call(msg, timeoutMs, &errorName);
    if (reply)
    {
        dbus_message_unref(reply);
        return true;
    }
    return okError && errorName == okError;
}

ObjectMap Client::GetManagedObjects()
{
    ObjectMap objects;

    DBusMessage* msg = dbus_message_new_method_call(
        BLUEZ_BUS, "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    DBusMessage* reply = Call(msg, 5000);
    if (!reply)
    {
        return objects;
    }

    DBusMessageIter iter;
    if (dbus_message_iter_init(reply, &iter) &&
        dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY)
    {
        DBusMessageIter objArray;
        dbus_message_iter_recurse(&iter, &objArray);

        while (dbus_message_iter_get_arg_type(&objArray) == DBUS_TYPE_DICT_ENTRY)
        {
            DBusMessageIter objEntry;
            dbus_message_iter_recurse(&objArray, &objEntry);

            const char* path = nullptr;
            if (dbus_message_iter_get_arg_type(&objEntry) == DBUS_TYPE_OBJECT_PATH)
            {
                dbus_message_iter_get_basic(&objEntry, &path);
                dbus_message_iter_next(&objEntry);

                if (path && dbus_message_iter_get_arg_type(&objEntry) == DBUS_TYPE_ARRAY)
                {
                    DBusMessageIter ifaceArray;
                    dbus_message_iter_recurse(&objEntry, &ifaceArray);

                    while (dbus_message_iter_get_arg_type(&ifaceArray) == DBUS_TYPE_DICT_ENTRY)
                    {
                        DBusMessageIter ifaceEntry;
                        dbus_message_iter_recurse(&ifaceArray, &ifaceEntry);

                        const char* ifaceName = nullptr;
                        if (dbus_message_iter_get_arg_type(&ifaceEntry) == DBUS_TYPE_STRING)
                        {
                            dbus_message_iter_get_basic(&ifaceEntry, &ifaceName);
                            dbus_message_iter_next(&ifaceEntry);

                            if (ifaceName &&
                                dbus_message_iter_get_arg_type(&ifaceEntry) == DBUS_TYPE_ARRAY)
                            {
                                DBusMessageIter propDict;
                                dbus_message_iter_recurse(&ifaceEntry, &propDict);
                                objects[path][ifaceName] = ParsePropertyDict(&propDict);
                            }
                        }

                        dbus_message_iter_next(&ifaceArray);
                    }
                }
            }

            dbus_message_iter_next(&objArray);
        }
    }

    dbus_message_unref(reply);
    return objects;
}

std::vector<std::string> Client::FindAdapters()
{
    std::vector<std::string> adapters;
    for (const auto& [path, interfaces] : GetManagedObjects())
    {
        if (interfaces.count(ADAPTER_IFACE))
        {
            adapters.push_back(path);
        }
    }
    return adapters;
}

std::string Client::DefaultAdapter()
{
    auto objects = GetManagedObjects();
    std::string unpowered;

    for (const auto& [path, interfaces] : objects)
    {
        auto it = interfaces.find(ADAPTER_IFACE);
        if (it == interfaces.end())
        {
            continue;
        }
        if (it->second.GetBool("Powered"))
        {
            return path;
        }
        if (unpowered.empty())
        {
            unpowered = path;
        }
    }

    if (!unpowered.empty())
    {
        std::cerr << "Bluetooth adapter " << unpowered
                  << " is not powered on - enable Bluetooth and retry\n";
    }
    else
    {
        std::cerr << "No Bluetooth adapter found\n";
    }
    return "";
}

std::string Client::FindDeviceByAddress(const std::string& address)
{
    for (const auto& [path, interfaces] : GetManagedObjects())
    {
        auto it = interfaces.find(DEVICE_IFACE);
        if (it == interfaces.end())
        {
            continue;
        }
        const std::string* addr = it->second.GetString("Address");
        if (addr && *addr == address)
        {
            return path;
        }
    }
    return "";
}

bool Client::StartDiscovery(const std::string& adapterPath)
{
    // Filter to LE transport; base stations are BLE-only. Failure here is
    // non-fatal (older BlueZ), discovery still works unfiltered.
    DBusMessage* filterMsg = dbus_message_new_method_call(
        BLUEZ_BUS, adapterPath.c_str(), ADAPTER_IFACE, "SetDiscoveryFilter");
    if (filterMsg)
    {
        DBusMessageIter iter, dict, entry, variant;
        dbus_message_iter_init_append(filterMsg, &iter);
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &dict);
        dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
        const char* key = "Transport";
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
        const char* transport = "le";
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &transport);
        dbus_message_iter_close_container(&entry, &variant);
        dbus_message_iter_close_container(&dict, &entry);
        dbus_message_iter_close_container(&iter, &dict);

        DBusMessage* reply = Call(filterMsg, 2000);
        if (reply)
        {
            dbus_message_unref(reply);
        }
    }

    return CallSimple(adapterPath.c_str(), ADAPTER_IFACE, "StartDiscovery", 5000,
                      "org.bluez.Error.InProgress");
}

void Client::StopDiscovery(const std::string& adapterPath)
{
    CallSimple(adapterPath.c_str(), ADAPTER_IFACE, "StopDiscovery", 5000, nullptr);
}

bool Client::ConnectDevice(const std::string& devicePath)
{
    // A connect to an advertising station normally completes in 1-3s; a
    // shorter timeout cycles hung attempts faster instead of stalling 10s.
    DBusMessage* msg = dbus_message_new_method_call(
        BLUEZ_BUS, devicePath.c_str(), DEVICE_IFACE, "Connect");
    std::string errorName;
    DBusMessage* reply = Call(msg, 6000, &errorName);
    if (reply)
    {
        dbus_message_unref(reply);
        return true;
    }
    if (errorName == "org.bluez.Error.AlreadyConnected")
    {
        return true;
    }
    std::cerr << "Connect failed for " << devicePath << ": "
              << (errorName.empty() ? "timeout/no reply" : errorName) << "\n";
    return false;
}

bool Client::DisconnectDevice(const std::string& devicePath)
{
    return CallSimple(devicePath.c_str(), DEVICE_IFACE, "Disconnect", 10000,
                      "org.bluez.Error.NotConnected");
}

bool Client::SetDeviceTrusted(const std::string& devicePath, bool trusted)
{
    DBusMessage* msg = dbus_message_new_method_call(
        BLUEZ_BUS, devicePath.c_str(), "org.freedesktop.DBus.Properties", "Set");
    if (!msg)
    {
        return false;
    }

    DBusMessageIter iter, variant;
    dbus_message_iter_init_append(msg, &iter);
    const char* iface = DEVICE_IFACE;
    const char* prop = "Trusted";
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &iface);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &prop);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "b", &variant);
    dbus_bool_t value = trusted ? TRUE : FALSE;
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &value);
    dbus_message_iter_close_container(&iter, &variant);

    DBusMessage* reply = Call(msg, 2000);
    if (reply)
    {
        dbus_message_unref(reply);
        return true;
    }
    return false;
}

bool Client::GetBoolProperty(const std::string& path, const char* iface, const char* prop, bool& out)
{
    DBusMessage* msg = dbus_message_new_method_call(
        BLUEZ_BUS, path.c_str(), "org.freedesktop.DBus.Properties", "Get");
    if (msg)
    {
        dbus_message_append_args(msg,
                                 DBUS_TYPE_STRING, &iface,
                                 DBUS_TYPE_STRING, &prop,
                                 DBUS_TYPE_INVALID);
    }

    DBusMessage* reply = Call(msg, 2000);
    if (!reply)
    {
        return false;
    }

    bool ok = false;
    DBusMessageIter iter;
    if (dbus_message_iter_init(reply, &iter) &&
        dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT)
    {
        DBusMessageIter variant;
        dbus_message_iter_recurse(&iter, &variant);
        if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_BOOLEAN)
        {
            dbus_bool_t value = FALSE;
            dbus_message_iter_get_basic(&variant, &value);
            out = (value != FALSE);
            ok = true;
        }
    }
    dbus_message_unref(reply);
    return ok;
}

DiscoveryGuard::DiscoveryGuard(Client& client, const std::string& adapterPath)
    : client(client), adapter(adapterPath), active(false)
{
    if (!adapter.empty())
    {
        active = client.StartDiscovery(adapter);
    }
}

DiscoveryGuard::~DiscoveryGuard()
{
    if (active)
    {
        client.StopDiscovery(adapter);
    }
}

}  // namespace bluez
