#pragma once

#include <ctime>
#include <map>
#include <optional>
#include <string>

#include "BaseStationDetector.h"

// Persistent settings at ${XDG_CONFIG_HOME:-~/.config}/lighthouse-manager/config.ini.
//
//   [general]
//   manage_mode = all            ; all | selected
//   power_off_mode = sleep       ; sleep | standby (standby wakes much faster)
//
//   [station D4:1D:FE:B1:FE:E8]
//   name = LHB-699A51BC          ; informational
//   managed = true
//
// Saves are atomic (temp file + rename), so a concurrent reader (the auto
// service) sees either the old or the new file, never a partial one.
class Config
{
public:
    enum class ManageMode
    {
        All,       // manage every discovered station
        Selected,  // manage only stations marked managed = true (default)
    };

    enum class PowerOffMode
    {
        Sleep,    // full power-off; slow to wake (station advertises rarely)
        Standby,  // motors off, radio alert; wakes near-instantly, uses more idle power
    };

    struct StationEntry
    {
        std::string name;
        bool managed = true;
    };

    // Auto-management is opt-in: until the user marks stations (or switches
    // to All mode), the auto service touches nothing.
    ManageMode manageMode = ManageMode::Selected;
    PowerOffMode powerOffMode = PowerOffMode::Sleep;
    std::map<std::string, StationEntry> stations;  // keyed by MAC address

    static std::string DefaultPath();

    // Missing or unreadable file yields defaults and returns false.
    bool Load(const std::string& path = DefaultPath());
    bool Save(const std::string& path = DefaultPath()) const;

    bool IsManaged(const BaseStationInfo& station) const;

    // Adds/updates an entry, recording the station name if known.
    void SetManaged(const std::string& address, const std::string& name, bool managed);

    static std::optional<time_t> FileMtime(const std::string& path = DefaultPath());
};
