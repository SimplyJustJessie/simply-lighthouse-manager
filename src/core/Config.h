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
//   v1_id = 1A2B3C4D             ; Base Station 1.0 only (see StationEntry)
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
        // Base Station 1.0 only: the 8 hex digit unique ID, which its wake and
        // sleep commands have to carry. 1.0 stations do not advertise it, so it
        // cannot be discovered - the user types it in (GUI ID column, or
        // --set-v1-id). Empty for 2.0 stations and for 1.0 stations the user
        // has not identified yet; an empty ID simply means no 1.0 command is
        // ever sent.
        std::string v1Id;
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

    // The configured Base Station 1.0 ID, or "" when the station is unknown or
    // has none. Never throws, never inserts.
    std::string V1Id(const std::string& address) const;

    // Adds/updates an entry, recording the station name if known.
    void SetManaged(const std::string& address, const std::string& name, bool managed);

    // Adds/updates an entry's Base Station 1.0 ID without changing whether the
    // station is managed: identifying a station is not the same as opting it
    // into (or out of) auto-management.
    void SetV1Id(const std::string& address, const std::string& name, const std::string& id);

    static std::optional<time_t> FileMtime(const std::string& path = DefaultPath());
};
