#pragma once

#include <string>

// Detection of *any* VR runtime by process, with no runtime SDK involved.
// The Bluetooth side of this app never depended on SteamVR - only the
// session lifecycle did - so watching for a process is enough to manage base
// stations for SteamVR, WiVRn, or a bare Monado session alike.
namespace runtime
{

// True when a process with this exact /proc/<pid>/comm name exists.
bool IsProcessRunning(const char* comm);

// Human-readable name of the first running VR runtime ("SteamVR", "WiVRn",
// "Monado"), or an empty string when none is running.
std::string RunningRuntime();

}  // namespace runtime
