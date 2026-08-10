#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "BaseStationDetector.h"

// GATT service UUIDs advertised/exposed by base stations.
inline constexpr const char* LIGHTHOUSE_V2_SERVICE_UUID = "00001523-1212-efde-1523-785feabcd124";
inline constexpr const char* LIGHTHOUSE_V1_SERVICE_UUID = "0000cb00-0000-1000-8000-00805f9b34fb";

// Valve's Bluetooth SIG company identifier, used in advertisements.
inline constexpr uint16_t VALVE_COMPANY_ID = 0x055D;

// Extracts the RF channel from Valve's manufacturer advertisement data
// (byte 2). Returns -1 when the data is absent or out of range. This is the
// authoritative source: it needs no connection and, unlike a GATT read,
// cannot be served stale from BlueZ's attribute cache.
int ChannelFromValveManufacturerData(const std::vector<uint8_t>& data);

// Decides whether a Bluetooth device is a base station and fills in
// name/id/type. Returns std::nullopt for non-lighthouse devices.
// All string handling is bounds-checked; never throws.
std::optional<BaseStationInfo> ClassifyStation(const std::string& address,
                                               const std::string& name,
                                               const std::vector<std::string>& serviceUuids = {});
