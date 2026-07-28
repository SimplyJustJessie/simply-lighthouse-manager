#pragma once

#include <chrono>

namespace vr
{
class IVRSystem;
}

// Watches the SteamVR session the *caller* owns. Holds no thread and never
// calls VR_Init/VR_Shutdown itself - exactly one component per process may
// own the OpenVR context (it is a process-wide global; concurrent
// init/shutdown from several threads was the cause of the old launch
// crashes).
class SteamVRWatcher
{
public:
    enum class Status
    {
        Running,
        QuitRequested,  // SteamVR sent VREvent_Quit - acknowledge and exit fast
        ProcessDead,    // vrserver vanished without a quit event
    };

    explicit SteamVRWatcher(vr::IVRSystem* system);

    // Call from the owning thread's loop. Drains pending VR events and
    // checks the vrserver process every ~2s.
    Status Poll();

    // Process-level check via /proc; needs no OpenVR context. Also used by
    // the GUI for its status indicator.
    static bool IsVrServerProcessRunning();

private:
    vr::IVRSystem* system;
    std::chrono::steady_clock::time_point lastProcessCheck;
    bool processWasRunning;
};
