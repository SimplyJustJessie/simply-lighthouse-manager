#pragma once

#include <openvr.h>
#include <string>

namespace vrreg
{

// SteamVR expects vendor-dotted app keys ("vendor.name"); SetApplicationAutoLaunch
// rejects undotted keys with VRApplicationError_InvalidApplication.
inline constexpr const char* APP_KEY = "simplyyjessie.lighthouse-manager";
// Key used by pre-fork releases (auto-launch never worked under it).
inline constexpr const char* LEGACY_APP_KEY = "lighthouse-manager-linux";

// RAII wrapper for a short-lived OpenVR session. Never nest two of these and
// never create one while any other OpenVR context is live in the process -
// the context is a process-wide global.
class ScopedVRSession
{
public:
    explicit ScopedVRSession(vr::EVRApplicationType type)
    {
        vr::EVRInitError initError = vr::VRInitError_None;
        system_ = vr::VR_Init(&initError, type);
        error_ = initError;
        if (initError != vr::VRInitError_None)
        {
            system_ = nullptr;
        }
    }

    ~ScopedVRSession()
    {
        if (system_)
        {
            vr::VR_Shutdown();
        }
    }

    ScopedVRSession(const ScopedVRSession&) = delete;
    ScopedVRSession& operator=(const ScopedVRSession&) = delete;

    bool Valid() const { return system_ != nullptr; }
    vr::IVRSystem* System() const { return system_; }
    vr::EVRInitError Error() const { return error_; }

    // Explicit early shutdown (e.g. release the IPC before slow cleanup).
    void Shutdown()
    {
        if (system_)
        {
            vr::VR_Shutdown();
            system_ = nullptr;
        }
    }

private:
    vr::IVRSystem* system_ = nullptr;
    vr::EVRInitError error_ = vr::VRInitError_None;
};

struct Status
{
    bool steamvrAvailable = false;
    bool registered = false;
    bool autoLaunch = false;
    std::string workingDirectory;
};

// Directory containing the running executable (resolved via /proc/self/exe).
std::string ExecutableDir();

// Path of the manifest we register: generated (with an absolute binary
// path) under ~/.config/lighthouse-manager/ at registration time. Newer
// SteamVR builds reject relative binary paths as InvalidManifest, and a
// user-writable location works for every install prefix.
std::string ManifestPath();

// Registers <exe dir>/manifest.vrmanifest with SteamVR. If a registration
// already exists but points somewhere else (e.g. a pre-1.1 install under
// ~/.local/share/SteamVR/drivers/), it is replaced so upgrades self-heal.
// Only touches the auto-launch flag when setAutoLaunch is true - a user's
// choice to disable auto-launch in SteamVR is otherwise respected.
// Requires SteamVR to be running. Creates its own scoped VR session; do not
// call while another OpenVR context is live.
// On failure the human-readable reason is written to *error when provided.
bool RegisterManifest(bool setAutoLaunch, bool verbose, std::string* error = nullptr);

// Same, but reuses a caller-owned session (for use inside the auto service).
bool RegisterManifestWithSession(vr::IVRSystem* system, bool setAutoLaunch, bool verbose,
                                 std::string* error = nullptr);

// Turns off auto-launch. Requires SteamVR to be running.
bool DisableAutoLaunch(bool verbose, std::string* error = nullptr);

Status CheckRegistration();

// File-level verification against Steam's own config store (appconfig.json
// manifest list + the app's .vrappconfig). Call AFTER the VR session that
// registered is closed - a transient headless vrserver has been seen
// accepting a registration in memory without persisting it. Returns true
// when persisted (or when no Steam config dir can be found to check);
// writes a human-readable explanation to *detail on failure.
bool VerifyPersistedOnDisk(bool expectAutoLaunch, std::string* detail);

}  // namespace vrreg
