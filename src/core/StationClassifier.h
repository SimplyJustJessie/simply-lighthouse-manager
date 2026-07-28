#pragma once

#include <optional>
#include <string>
#include <vector>

#include "BaseStationDetector.h"

// GATT service UUIDs advertised/exposed by base stations.
inline constexpr const char* LIGHTHOUSE_V2_SERVICE_UUID = "00001523-1212-efde-1523-785feabcd124";
inline constexpr const char* LIGHTHOUSE_V1_SERVICE_UUID = "0000cb00-0000-1000-8000-00805f9b34fb";

// Decides whether a Bluetooth device is a base station and fills in
// name/id/type. Returns std::nullopt for non-lighthouse devices.
// All string handling is bounds-checked; never throws.
std::optional<BaseStationInfo> ClassifyStation(const std::string& address,
                                               const std::string& name,
                                               const std::vector<std::string>& serviceUuids = {});
