#include "BaseStationController.h"
#include <iostream>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <dbus/dbus.h>
#include <unistd.h>

struct BaseStationController::DBusConnWrapper
{
    ::DBusConnection* conn;
    
    DBusConnWrapper() : conn(nullptr)
    {
        DBusError err;
        dbus_error_init(&err);
        conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
        if (dbus_error_is_set(&err))
        {
            std::cerr << "D-Bus connection error: " << err.message << std::endl;
            dbus_error_free(&err);
            conn = nullptr;
        }
    }
    
    ~DBusConnWrapper()
    {
        if (conn)
        {
            dbus_connection_unref(conn);
        }
    }
    
    bool IsValid() const { return conn != nullptr; }
    
    operator ::DBusConnection*() { return conn; }
};

BaseStationController::BaseStationController() : connected(false)
{
    dbusConn = std::make_unique<DBusConnWrapper>();
}

BaseStationController::~BaseStationController()
{
    Disconnect();
}

bool BaseStationController::Connect(const BaseStationInfo& station)
{
    stationInfo = station;
    
    if (station.address.empty())
    {
        std::cerr << "Error: Base station address is empty\n";
        return false;
    }
    
    if (!dbusConn->IsValid())
    {
        std::cerr << "Error: D-Bus connection not available\n";
        return false;
    }
    
    
    connected = ConnectToDevice();
    return connected;
}

void BaseStationController::Disconnect()
{
    connected = false;
}

bool BaseStationController::ConnectToDevice()
{
    std::string devicePath = GetDevicePath();
    if (devicePath.empty())
    {
        return false;
    }
    
    const int retryCount = 10;
    for (int i = 0; i < retryCount; i++)
    {
        DBusMessage* msg = dbus_message_new_method_call(
            "org.bluez",
            devicePath.c_str(),
            "org.bluez.Device1",
            "Connect");
        
        if (!msg)
        {
            if (i < retryCount - 1)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            continue;
        }
        
        DBusError err;
        dbus_error_init(&err);
        
        DBusMessage* reply = dbus_connection_send_with_reply_and_block(
            *dbusConn,
            msg,
            3000,
            &err);
        
        dbus_message_unref(msg);
        
        if (reply)
        {
            dbus_message_unref(reply);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            return true;
        }
        
        if (dbus_error_is_set(&err))
        {
            if (strstr(err.name, "org.bluez.Error.AlreadyConnected") != nullptr)
            {
                dbus_error_free(&err);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                return true;
            }
            dbus_error_free(&err);
        }
        
        if (i < retryCount - 1)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return true;
}

std::string BaseStationController::GetDevicePath()
{
    std::string macDbus = stationInfo.address;
    std::replace(macDbus.begin(), macDbus.end(), ':', '_');
    return "/org/bluez/hci0/dev_" + macDbus;
}

bool BaseStationController::WaitForServicesResolved()
{
    std::string devicePath = GetDevicePath();
    const int maxWait = 20;
    
    for (int i = 0; i < maxWait; i++)
    {
        DBusMessage* msg = dbus_message_new_method_call(
            "org.bluez",
            devicePath.c_str(),
            "org.freedesktop.DBus.Properties",
            "Get");
        
        if (!msg)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        const char* iface = "org.bluez.Device1";
        const char* prop = "ServicesResolved";
        dbus_message_append_args(msg,
            DBUS_TYPE_STRING, &iface,
            DBUS_TYPE_STRING, &prop,
            DBUS_TYPE_INVALID);
        
        DBusError err;
        dbus_error_init(&err);
        
        DBusMessage* reply = dbus_connection_send_with_reply_and_block(
            *dbusConn,
            msg,
            1000,
            &err);
        
        dbus_message_unref(msg);
        
        if (reply)
        {
            DBusMessageIter iter, varIter;
            bool resolved = false;
            if (dbus_message_iter_init(reply, &iter))
            {
                if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT)
                {
                    dbus_message_iter_recurse(&iter, &varIter);
                    if (dbus_message_iter_get_arg_type(&varIter) == DBUS_TYPE_BOOLEAN)
                    {
                        dbus_bool_t resolvedVal;
                        dbus_message_iter_get_basic(&varIter, &resolvedVal);
                        resolved = (resolvedVal != FALSE);
                    }
                }
            }
            dbus_message_unref(reply);
            if (resolved)
            {
                return true;
            }
        }
        
        if (dbus_error_is_set(&err))
        {
            dbus_error_free(&err);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return false;
}

std::string BaseStationController::FindServicePath(const std::string& serviceUuid)
{
    std::string devicePath = GetDevicePath();
    
    if (!WaitForServicesResolved())
    {
    }
    
    DBusMessage* msg = dbus_message_new_method_call(
        "org.bluez",
        devicePath.c_str(),
        "org.freedesktop.DBus.Introspectable",
        "Introspect");
    
    if (!msg)
    {
        return "";
    }
    
    DBusError err;
    dbus_error_init(&err);
    
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(
        *dbusConn,
        msg,
        5000,
        &err);
    
    dbus_message_unref(msg);
    
    if (!reply)
    {
        if (dbus_error_is_set(&err))
        {
            dbus_error_free(&err);
        }
        return "";
    }
    
    DBusMessageIter iter;
    const char* xmlData;
    if (dbus_message_iter_init(reply, &iter) && 
        dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING)
    {
        dbus_message_iter_get_basic(&iter, &xmlData);
        
        std::string xml(xmlData);
        std::string targetUuid(serviceUuid);
        std::transform(targetUuid.begin(), targetUuid.end(), targetUuid.begin(), ::tolower);
        
        size_t pos = 0;
        while ((pos = xml.find("<node name=\"service", pos)) != std::string::npos)
        {
            size_t nameStart = xml.find("name=\"", pos) + 6;
            size_t nameEnd = xml.find("\"", nameStart);
            if (nameEnd == std::string::npos) break;
            
            std::string serviceName = xml.substr(nameStart, nameEnd - nameStart);
            std::string servicePath = devicePath + "/" + serviceName;
            
            DBusMessage* svcMsg = dbus_message_new_method_call(
                "org.bluez",
                servicePath.c_str(),
                "org.freedesktop.DBus.Properties",
                "Get");
            
            if (svcMsg)
            {
                const char* svcIface = "org.bluez.GattService1";
                const char* svcProp = "UUID";
                dbus_message_append_args(svcMsg,
                    DBUS_TYPE_STRING, &svcIface,
                    DBUS_TYPE_STRING, &svcProp,
                    DBUS_TYPE_INVALID);
                
                DBusError svcErr;
                dbus_error_init(&svcErr);
                
                DBusMessage* svcReply = dbus_connection_send_with_reply_and_block(
                    *dbusConn,
                    svcMsg,
                    2000,
                    &svcErr);
                
                dbus_message_unref(svcMsg);
                
                if (svcReply)
                {
                    DBusMessageIter svcIter, svcVarIter;
                    if (dbus_message_iter_init(svcReply, &svcIter))
                    {
                        if (dbus_message_iter_get_arg_type(&svcIter) == DBUS_TYPE_VARIANT)
                        {
                            dbus_message_iter_recurse(&svcIter, &svcVarIter);
                            if (dbus_message_iter_get_arg_type(&svcVarIter) == DBUS_TYPE_STRING)
                            {
                                const char* uuid;
                                dbus_message_iter_get_basic(&svcVarIter, &uuid);
                                
                                std::string uuidStr(uuid);
                                std::transform(uuidStr.begin(), uuidStr.end(), uuidStr.begin(), ::tolower);
                                
                                if (uuidStr == targetUuid)
                                {
                                    dbus_message_unref(svcReply);
                                    dbus_message_unref(reply);
                                    return servicePath;
                                }
                            }
                        }
                    }
                    dbus_message_unref(svcReply);
                }
                
                if (dbus_error_is_set(&svcErr))
                {
                    dbus_error_free(&svcErr);
                }
            }
            
            pos = nameEnd;
        }
    }
    
    dbus_message_unref(reply);
    return "";
}

std::string BaseStationController::FindCharacteristicPath(const std::string& servicePath, const std::string& charUuid)
{
    DBusMessage* msg = dbus_message_new_method_call(
        "org.bluez",
        servicePath.c_str(),
        "org.freedesktop.DBus.Introspectable",
        "Introspect");
    
    if (!msg)
    {
        return "";
    }
    
    DBusError err;
    dbus_error_init(&err);
    
    DBusMessage* reply = dbus_connection_send_with_reply_and_block(
        *dbusConn,
        msg,
        5000,
        &err);
    
    dbus_message_unref(msg);
    
    if (!reply)
    {
        if (dbus_error_is_set(&err))
        {
            dbus_error_free(&err);
        }
        return "";
    }
    
    DBusMessageIter iter;
    const char* xmlData;
    if (dbus_message_iter_init(reply, &iter) && 
        dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING)
    {
        dbus_message_iter_get_basic(&iter, &xmlData);
        
        std::string xml(xmlData);
        std::string targetUuid(charUuid);
        std::transform(targetUuid.begin(), targetUuid.end(), targetUuid.begin(), ::tolower);
        
        size_t pos = 0;
        while ((pos = xml.find("<node name=\"char", pos)) != std::string::npos)
        {
            size_t nameStart = xml.find("name=\"", pos) + 6;
            size_t nameEnd = xml.find("\"", nameStart);
            if (nameEnd == std::string::npos) break;
            
            std::string charName = xml.substr(nameStart, nameEnd - nameStart);
            std::string charPath = servicePath + "/" + charName;
            
            DBusMessage* charMsg = dbus_message_new_method_call(
                "org.bluez",
                charPath.c_str(),
                "org.freedesktop.DBus.Properties",
                "Get");
            
            if (charMsg)
            {
                const char* charIface = "org.bluez.GattCharacteristic1";
                const char* charProp = "UUID";
                dbus_message_append_args(charMsg,
                    DBUS_TYPE_STRING, &charIface,
                    DBUS_TYPE_STRING, &charProp,
                    DBUS_TYPE_INVALID);
                
                DBusError charErr;
                dbus_error_init(&charErr);
                
                DBusMessage* charReply = dbus_connection_send_with_reply_and_block(
                    *dbusConn,
                    charMsg,
                    2000,
                    &charErr);
                
                dbus_message_unref(charMsg);
                
                if (charReply)
                {
                    DBusMessageIter charIter, charVarIter;
                    if (dbus_message_iter_init(charReply, &charIter))
                    {
                        if (dbus_message_iter_get_arg_type(&charIter) == DBUS_TYPE_VARIANT)
                        {
                            dbus_message_iter_recurse(&charIter, &charVarIter);
                            if (dbus_message_iter_get_arg_type(&charVarIter) == DBUS_TYPE_STRING)
                            {
                                const char* uuid;
                                dbus_message_iter_get_basic(&charVarIter, &uuid);
                                
                                std::string uuidStr(uuid);
                                std::transform(uuidStr.begin(), uuidStr.end(), uuidStr.begin(), ::tolower);
                                
                                if (uuidStr == targetUuid)
                                {
                                    dbus_message_unref(charReply);
                                    dbus_message_unref(reply);
                                    return charPath;
                                }
                            }
                        }
                    }
                    dbus_message_unref(charReply);
                }
                
                if (dbus_error_is_set(&charErr))
                {
                    dbus_error_free(&charErr);
                }
            }
            
            pos = nameEnd;
        }
    }
    
    dbus_message_unref(reply);
    return "";
}

bool BaseStationController::WriteCharacteristicValue(const std::string& charPath, const uint8_t* data, size_t dataLen)
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
        *dbusConn,
        msg,
        5000,
        &err);
    
    dbus_message_unref(msg);
    
    if (dbus_error_is_set(&err))
    {
        std::cerr << "D-Bus error writing characteristic: " << err.name << " - " << err.message << std::endl;
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
    
    std::string servicePath = FindServicePath(V2_SERVICE_UUID);
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

bool BaseStationController::WriteV1PowerCharacteristic(const uint8_t* data, size_t dataLen)
{
    if (!EnsureConnection())
    {
        return false;
    }
    
    std::string servicePath = FindServicePath(V1_SERVICE_UUID);
    if (servicePath.empty())
    {
        std::cerr << "Failed to find V1 GATT service\n";
        return false;
    }
    
    std::string charPath = FindCharacteristicPath(servicePath, V1_POWER_CHAR_UUID);
    if (charPath.empty())
    {
        std::cerr << "Failed to find V1 power characteristic\n";
        return false;
    }
    
    return WriteCharacteristicValue(charPath, data, dataLen);
}

bool BaseStationController::EnsureConnection()
{
    if (!dbusConn->IsValid())
    {
        std::cerr << "D-Bus connection not available\n";
        return false;
    }
    
    if (!connected)
    {
        if (!ConnectToDevice())
        {
            return false;
        }
        connected = true;
    }
    
    return true;
}

bool BaseStationController::SendCommand(BaseStationCommand command)
{
    if (!connected)
    {
        std::cerr << "Not connected to base station\n";
        return false;
    }
    
    uint8_t value = static_cast<uint8_t>(command);
    
    bool isV1 = stationInfo.name.find("HTC BS") == 0 || 
                stationInfo.name.find("VIVE BS") == 0;
    
    if (isV1)
    {
        std::cerr << "V1 base station control not yet implemented\n";
        return false;
    }
    
    const int retryCount = 10;
    bool success = false;
    
    for (int i = 0; i < retryCount; i++)
    {
        if (i > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        
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

bool BaseStationController::Wake()
{
    return SendCommand(BaseStationCommand::Wake);
}

bool BaseStationController::Sleep()
{
    if (!connected)
    {
        std::cerr << "Not connected to base station\n";
        return false;
    }
    
    return SendCommand(BaseStationCommand::Sleep);
}

bool BaseStationController::Standby()
{
    if (!connected)
    {
        std::cerr << "Not connected to base station\n";
        return false;
    }
    
    return SendCommand(BaseStationCommand::Standby);
}

bool BaseStationController::SendWakePacket()
{
    if (!connected)
    {
        return false;
    }
    
    bool isV1 = stationInfo.name.find("HTC BS") == 0 || 
                stationInfo.name.find("VIVE BS") == 0;
    
    if (isV1)
    {
        return false;
    }
    
    uint8_t value = static_cast<uint8_t>(BaseStationCommand::Wake);
    return WriteV2PowerCharacteristic(value);
}
