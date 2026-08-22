#include "RuntimeWatcher.h"

#include <dirent.h>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace runtime
{

namespace
{

struct KnownRuntime
{
    const char* comm;
    const char* label;
};

// /proc/<pid>/comm is capped at 15 characters, so these must match what the
// kernel actually reports, not the full binary name.
constexpr KnownRuntime KNOWN_RUNTIMES[] = {
    {"vrserver", "SteamVR"},
    {"wivrn-server", "WiVRn"},
    {"monado-service", "Monado"},
};

}  // namespace

bool IsProcessRunning(const char* comm)
{
    DIR* proc = opendir("/proc");
    if (!proc)
    {
        return false;
    }

    bool found = false;
    struct dirent* entry;
    while (!found && (entry = readdir(proc)) != nullptr)
    {
        const char* name = entry->d_name;
        bool numeric = *name != '\0';
        for (const char* c = name; *c; c++)
        {
            if (!std::isdigit(static_cast<unsigned char>(*c)))
            {
                numeric = false;
                break;
            }
        }
        if (!numeric)
        {
            continue;
        }

        char commPath[280];
        std::snprintf(commPath, sizeof(commPath), "/proc/%s/comm", name);
        FILE* file = std::fopen(commPath, "r");
        if (!file)
        {
            continue;
        }
        char buffer[64] = {0};
        if (std::fgets(buffer, sizeof(buffer), file))
        {
            buffer[strcspn(buffer, "\n")] = '\0';
            if (std::strcmp(buffer, comm) == 0)
            {
                found = true;
            }
        }
        std::fclose(file);
    }

    closedir(proc);
    return found;
}

std::string RunningRuntime()
{
    for (const auto& candidate : KNOWN_RUNTIMES)
    {
        if (IsProcessRunning(candidate.comm))
        {
            return candidate.label;
        }
    }
    return "";
}

}  // namespace runtime
