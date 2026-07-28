#include "SteamVRWatcher.h"

#include <dirent.h>
#include <openvr.h>
#include <cctype>
#include <cstdio>
#include <cstring>

SteamVRWatcher::SteamVRWatcher(vr::IVRSystem* system)
    : system(system),
      lastProcessCheck(std::chrono::steady_clock::now()),
      processWasRunning(IsVrServerProcessRunning())
{
}

bool SteamVRWatcher::IsVrServerProcessRunning()
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
        // Only numeric directories are processes.
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
        FILE* comm = std::fopen(commPath, "r");
        if (!comm)
        {
            continue;
        }
        char buffer[64] = {0};
        if (std::fgets(buffer, sizeof(buffer), comm))
        {
            buffer[strcspn(buffer, "\n")] = '\0';
            if (std::strcmp(buffer, "vrserver") == 0)
            {
                found = true;
            }
        }
        std::fclose(comm);
    }

    closedir(proc);
    return found;
}

SteamVRWatcher::Status SteamVRWatcher::Poll()
{
    if (system)
    {
        vr::VREvent_t event;
        while (system->PollNextEvent(&event, sizeof(event)))
        {
            if (event.eventType == vr::VREvent_Quit ||
                event.eventType == vr::VREvent_DriverRequestedQuit)
            {
                return Status::QuitRequested;
            }
        }
    }

    auto now = std::chrono::steady_clock::now();
    if (now - lastProcessCheck >= std::chrono::seconds(2))
    {
        lastProcessCheck = now;
        bool running = IsVrServerProcessRunning();
        if (processWasRunning && !running)
        {
            return Status::ProcessDead;
        }
        processWasRunning = running;
    }

    return Status::Running;
}
