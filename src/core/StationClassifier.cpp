#include "StationClassifier.h"

#include <algorithm>
#include <cctype>

namespace
{

std::string ToLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool StartsWith(const std::string& s, const char* prefix)
{
    return s.rfind(prefix, 0) == 0;
}

// Last two octets of the MAC ("EE:FF"), or the whole address if it is not a
// well-formed MAC. Used as a fallback identifier only.
std::string MacSuffix(const std::string& address)
{
    return address.size() >= 17 ? address.substr(12) : address;
}

// "LHB-XXXXXXXX" / "LHR-XXXXXXXX" -> the 8-char serial, if present.
std::string SerialFromPrefixedName(const std::string& name, const std::string& fallback)
{
    return name.size() >= 12 ? name.substr(4, 8) : fallback;
}

}  // namespace

std::optional<BaseStationInfo> ClassifyStation(const std::string& address,
                                               const std::string& name,
                                               const std::vector<std::string>& serviceUuids)
{
    if (address.empty())
    {
        return std::nullopt;
    }

    const std::string suffix = MacSuffix(address);

    BaseStationInfo station;
    station.address = address;
    station.name = name.empty() ? "Base Station " + suffix : name;

    bool matched = false;

    // LHB-XXXXXXXX is the advertising name of Base Station 2.0 (SteamVR 2.x /
    // Valve Index); 1.0 stations advertise as "HTC BS XXXXXX". The pre-rework
    // code had these swapped.
    if (StartsWith(name, "LHB-") || StartsWith(name, "LHR-"))
    {
        station.isBaseStation1 = false;
        station.id = SerialFromPrefixedName(name, suffix);
        matched = true;
    }
    else if (StartsWith(name, "HTC BS") || StartsWith(name, "VIVE BS"))
    {
        station.isBaseStation1 = true;
        station.id = name.size() >= 8 ? name.substr(name.size() - 8) : suffix;
        matched = true;
    }
    else if (name.find("Lighthouse") != std::string::npos ||
             name.find("Base Station") != std::string::npos)
    {
        station.isBaseStation1 = false;
        station.id = suffix;
        matched = true;
    }

    if (!matched)
    {
        for (const auto& uuid : serviceUuids)
        {
            const std::string lower = ToLower(uuid);
            if (lower == LIGHTHOUSE_V2_SERVICE_UUID)
            {
                station.isBaseStation1 = false;
                station.id = suffix;
                matched = true;
                break;
            }
            if (lower == LIGHTHOUSE_V1_SERVICE_UUID)
            {
                station.isBaseStation1 = true;
                station.id = suffix;
                matched = true;
                break;
            }
        }
    }

    if (!matched)
    {
        return std::nullopt;
    }

    station.serial = station.id;
    return station;
}
