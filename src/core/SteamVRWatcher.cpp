#include "SteamVRWatcher.h"

#include "RuntimeWatcher.h"

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
    return runtime::IsProcessRunning("vrserver");
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
