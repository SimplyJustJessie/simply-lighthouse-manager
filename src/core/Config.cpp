#include "Config.h"

#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace
{

std::string Trim(const std::string& s)
{
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return "";
    }
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::string ConfigDir()
{
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg)
    {
        return std::string(xdg) + "/lighthouse-manager";
    }
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.config/lighthouse-manager";
}

bool ParseBool(const std::string& value, bool fallback)
{
    if (value == "true" || value == "1" || value == "yes" || value == "on")
    {
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off")
    {
        return false;
    }
    return fallback;
}

}  // namespace

std::string Config::DefaultPath()
{
    return ConfigDir() + "/config.ini";
}

bool Config::Load(const std::string& path)
{
    manageMode = ManageMode::Selected;
    powerOffMode = PowerOffMode::Sleep;
    stations.clear();

    std::ifstream file(path);
    if (!file.is_open())
    {
        return false;
    }

    std::string line;
    std::string section;        // "general" or "station"
    std::string sectionArg;     // station address

    while (std::getline(file, line))
    {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#')
        {
            continue;
        }

        if (line.front() == '[' && line.back() == ']')
        {
            std::string header = Trim(line.substr(1, line.size() - 2));
            size_t space = header.find_first_of(" \t");
            if (space == std::string::npos)
            {
                section = header;
                sectionArg.clear();
            }
            else
            {
                section = Trim(header.substr(0, space));
                sectionArg = Trim(header.substr(space + 1));
            }
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos)
        {
            continue;
        }
        std::string key = Trim(line.substr(0, eq));
        std::string value = Trim(line.substr(eq + 1));
        // Strip trailing comment
        size_t comment = value.find_first_of(";#");
        if (comment != std::string::npos)
        {
            value = Trim(value.substr(0, comment));
        }

        if (section == "general")
        {
            if (key == "manage_mode")
            {
                manageMode = (value == "all") ? ManageMode::All : ManageMode::Selected;
            }
            else if (key == "power_off_mode")
            {
                powerOffMode = (value == "standby") ? PowerOffMode::Standby
                                                    : PowerOffMode::Sleep;
            }
        }
        else if (section == "station" && !sectionArg.empty())
        {
            StationEntry& entry = stations[sectionArg];
            if (key == "name")
            {
                entry.name = value;
            }
            else if (key == "managed")
            {
                entry.managed = ParseBool(value, true);
            }
            else if (key == "v1_id")
            {
                entry.v1Id = value;
            }
        }
    }

    return true;
}

bool Config::Save(const std::string& path) const
{
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos)
    {
        // Best-effort recursive mkdir of the parent directory.
        std::string dir = path.substr(0, slash);
        std::string partial;
        std::istringstream segments(dir);
        std::string segment;
        while (std::getline(segments, segment, '/'))
        {
            partial += segment + "/";
            if (!segment.empty())
            {
                mkdir(partial.c_str(), 0755);
            }
        }
    }

    std::string tmpPath = path + ".tmp";
    {
        std::ofstream file(tmpPath, std::ios::trunc);
        if (!file.is_open())
        {
            return false;
        }

        file << "[general]\n";
        file << "manage_mode = " << (manageMode == ManageMode::Selected ? "selected" : "all")
             << "\n";
        file << "power_off_mode = "
             << (powerOffMode == PowerOffMode::Standby ? "standby" : "sleep") << "\n";

        for (const auto& [address, entry] : stations)
        {
            file << "\n[station " << address << "]\n";
            if (!entry.name.empty())
            {
                file << "name = " << entry.name << "\n";
            }
            file << "managed = " << (entry.managed ? "true" : "false") << "\n";
            if (!entry.v1Id.empty())
            {
                file << "v1_id = " << entry.v1Id << "\n";
            }
        }

        file.flush();
        if (!file.good())
        {
            return false;
        }
    }

    if (std::rename(tmpPath.c_str(), path.c_str()) != 0)
    {
        std::remove(tmpPath.c_str());
        return false;
    }
    return true;
}

bool Config::IsManaged(const BaseStationInfo& station) const
{
    auto it = stations.find(station.address);
    if (manageMode == ManageMode::All)
    {
        return it == stations.end() || it->second.managed;
    }
    return it != stations.end() && it->second.managed;
}

std::string Config::V1Id(const std::string& address) const
{
    auto it = stations.find(address);
    return it == stations.end() ? std::string() : it->second.v1Id;
}

void Config::SetManaged(const std::string& address, const std::string& name, bool managed)
{
    StationEntry& entry = stations[address];
    if (!name.empty())
    {
        entry.name = name;
    }
    entry.managed = managed;
}

void Config::SetV1Id(const std::string& address, const std::string& name,
                     const std::string& id)
{
    auto it = stations.find(address);
    if (it == stations.end())
    {
        StationEntry entry;
        entry.name = name;
        // Identifying a station must not change whether it is managed, and a
        // station with no entry is managed exactly when the mode says all.
        entry.managed = manageMode == ManageMode::All;
        entry.v1Id = id;
        stations.emplace(address, std::move(entry));
        return;
    }
    if (!name.empty())
    {
        it->second.name = name;
    }
    it->second.v1Id = id;
}

std::optional<time_t> Config::FileMtime(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
    {
        return std::nullopt;
    }
    return st.st_mtime;
}
