#include "VRRegistration.h"

#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>

namespace vrreg
{

namespace
{

void ReportError(const std::string& message, bool verbose, std::string* error)
{
    if (error)
    {
        *error = message;
    }
    if (verbose)
    {
        std::cerr << message << "\n";
    }
}

std::string ManifestDir()
{
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    std::string dir;
    if (xdg && *xdg)
    {
        dir = xdg;
    }
    else
    {
        const char* home = std::getenv("HOME");
        dir = std::string(home ? home : ".") + "/.config";
    }
    return dir + "/lighthouse-manager";
}

// Writes the manifest we register, with an ABSOLUTE binary path: newer
// SteamVR builds (seen on 2.17.x) reject manifests whose binary_path_linux
// is relative with VRApplicationError_InvalidManifest, while older builds
// (2.12.x) accept them. Generating at registration time also keeps the path
// correct for any install location (AUR /usr/lib, ~/.local, dev tree) and
// avoids registering files under read-only system paths.
bool WriteManifestFile(const std::string& manifestPath, const std::string& exeDir,
                       bool verbose, std::string* error)
{
    mkdir(ManifestDir().c_str(), 0755);

    std::ofstream file(manifestPath, std::ios::trunc);
    if (!file.is_open())
    {
        ReportError("Cannot write " + manifestPath, verbose, error);
        return false;
    }

    file << "{\n"
         << "\t\"source\": \"builtin\",\n"
         << "\t\"applications\": [{\n"
         << "\t\t\"app_key\": \"" << APP_KEY << "\",\n"
         << "\t\t\"launch_type\": \"binary\",\n"
         << "\t\t\"binary_path_linux\": \"" << exeDir << "/lighthouse-manager\",\n"
         << "\t\t\"arguments\": \"--auto\",\n"
         << "\t\t\"is_dashboard_overlay\": true,\n"
         << "\t\t\"strings\": {\n"
         << "\t\t\t\"en_us\": {\n"
         << "\t\t\t\t\"name\": \"Lighthouse Manager\",\n"
         << "\t\t\t\t\"description\": \"Base Station Power Management\"\n"
         << "\t\t\t}\n"
         << "\t\t}\n"
         << "\t}]\n"
         << "}\n";

    file.flush();
    if (!file.good())
    {
        ReportError("Failed writing " + manifestPath, verbose, error);
        return false;
    }
    return true;
}

// The registration work itself, given a live VRApplications interface.
bool RegisterImpl(vr::IVRApplications* apps, bool setAutoLaunch, bool verbose,
                  std::string* error)
{
    const std::string exeDir = ExecutableDir();
    if (exeDir.empty())
    {
        ReportError("Failed to determine executable path", verbose, error);
        return false;
    }
    const std::string manifestPath = ManifestPath();
    if (!WriteManifestFile(manifestPath, exeDir, verbose, error))
    {
        return false;
    }

    // Drop any existing registration (current or legacy key, same or stale
    // path) so manifest property changes actually propagate - SteamVR keeps
    // the properties from registration time otherwise.
    for (const char* key : {APP_KEY, LEGACY_APP_KEY})
    {
        if (!apps->IsApplicationInstalled(key))
        {
            continue;
        }
        char workingDir[1024] = {0};
        vr::EVRApplicationError propError = vr::VRApplicationError_None;
        apps->GetApplicationPropertyString(key,
                                           vr::VRApplicationProperty_WorkingDirectory_String,
                                           workingDir, sizeof(workingDir), &propError);
        if (propError == vr::VRApplicationError_None && workingDir[0] != '\0')
        {
            if (verbose && ManifestDir() != workingDir)
            {
                std::cout << "Removing old registration of " << key
                          << " (was: " << workingDir << ")\n";
            }
            std::string oldManifest = std::string(workingDir) + "/manifest.vrmanifest";
            apps->RemoveApplicationManifest(oldManifest.c_str());
        }
    }
    apps->RemoveApplicationManifest(manifestPath.c_str());

    if (verbose)
    {
        std::cout << "Registering manifest: " << manifestPath << "\n";
    }
    vr::EVRApplicationError appError = apps->AddApplicationManifest(manifestPath.c_str());
    if (appError != vr::VRApplicationError_None)
    {
        ReportError(std::string("Failed to add manifest (") +
                        apps->GetApplicationsErrorNameFromEnum(appError) +
                        "): " + manifestPath,
                    verbose, error);
        return false;
    }

    if (setAutoLaunch)
    {
        vr::EVRApplicationError autoLaunchError = apps->SetApplicationAutoLaunch(APP_KEY, true);
        if (autoLaunchError != vr::VRApplicationError_None)
        {
            ReportError(std::string("Registered, but enabling auto-start failed: ") +
                            apps->GetApplicationsErrorNameFromEnum(autoLaunchError),
                        verbose, error);
            return false;
        }
    }

    // Do not trust the return codes alone - read the state back. A headless
    // vrserver auto-spawned by VR_Init(Utility) has been seen accepting the
    // manifest and then not persisting anything.
    if (!apps->IsApplicationInstalled(APP_KEY))
    {
        ReportError("Registration did not persist - start SteamVR normally (from Steam) "
                    "and try again",
                    verbose, error);
        return false;
    }
    if (setAutoLaunch && !apps->GetApplicationAutoLaunch(APP_KEY))
    {
        ReportError("Auto-launch flag did not persist - start SteamVR normally (from Steam) "
                    "and try again",
                    verbose, error);
        return false;
    }
    if (setAutoLaunch && verbose)
    {
        std::cout << "Auto-launch enabled (verified)\n";
    }

    return true;
}

}  // namespace

std::string ManifestPath()
{
    return ManifestDir() + "/manifest.vrmanifest";
}

std::string ExecutableDir()
{
    char exePath[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (count <= 0)
    {
        return "";
    }
    exePath[count] = '\0';

    std::string dir(exePath);
    size_t lastSlash = dir.find_last_of('/');
    return lastSlash != std::string::npos ? dir.substr(0, lastSlash) : "";
}

bool RegisterManifest(bool setAutoLaunch, bool verbose, std::string* error)
{
    ScopedVRSession session(vr::VRApplication_Utility);
    if (!session.Valid())
    {
        ReportError(std::string("Failed to initialize OpenVR: ") +
                        vr::VR_GetVRInitErrorAsEnglishDescription(session.Error()) +
                        " (is SteamVR running?)",
                    verbose, error);
        return false;
    }
    return RegisterManifestWithSession(session.System(), setAutoLaunch, verbose, error);
}

bool RegisterManifestWithSession(vr::IVRSystem* system, bool setAutoLaunch, bool verbose,
                                 std::string* error)
{
    if (!system)
    {
        ReportError("No OpenVR session", verbose, error);
        return false;
    }
    vr::IVRApplications* apps = vr::VRApplications();
    if (!apps)
    {
        ReportError("Failed to get VRApplications interface", verbose, error);
        return false;
    }
    return RegisterImpl(apps, setAutoLaunch, verbose, error);
}

bool DisableAutoLaunch(bool verbose, std::string* error)
{
    ScopedVRSession session(vr::VRApplication_Utility);
    if (!session.Valid())
    {
        ReportError(std::string("Failed to initialize OpenVR: ") +
                        vr::VR_GetVRInitErrorAsEnglishDescription(session.Error()) +
                        " (is SteamVR running?)",
                    verbose, error);
        return false;
    }

    vr::IVRApplications* apps = vr::VRApplications();
    if (!apps)
    {
        ReportError("Failed to get VRApplications interface", verbose, error);
        return false;
    }

    vr::EVRApplicationError err = apps->SetApplicationAutoLaunch(APP_KEY, false);
    if (err != vr::VRApplicationError_None)
    {
        ReportError(std::string("Failed to disable auto-launch: ") +
                        apps->GetApplicationsErrorNameFromEnum(err),
                    verbose, error);
        return false;
    }
    if (verbose)
    {
        std::cout << "Auto-launch disabled\n";
    }
    return true;
}

bool VerifyPersistedOnDisk(bool expectAutoLaunch, std::string* detail)
{
    const char* home = std::getenv("HOME");
    if (!home)
    {
        return true;  // cannot check - do not block on it
    }

    const std::string candidates[] = {
        std::string(home) + "/.local/share/Steam/config",
        std::string(home) + "/.steam/steam/config",
        std::string(home) + "/.steam/root/config",
    };

    std::string configDir;
    for (const auto& dir : candidates)
    {
        std::ifstream probe(dir + "/appconfig.json");
        if (probe.is_open())
        {
            configDir = dir;
            break;
        }
    }
    if (configDir.empty())
    {
        return true;  // no Steam config found to verify against
    }

    const std::string manifestPath = ManifestPath();

    std::ifstream appconfig(configDir + "/appconfig.json");
    std::string contents((std::istreambuf_iterator<char>(appconfig)),
                         std::istreambuf_iterator<char>());
    if (contents.find(manifestPath) == std::string::npos)
    {
        if (detail)
        {
            *detail = "Steam did not persist the manifest registration (" + manifestPath +
                      " missing from " + configDir + "/appconfig.json)";
        }
        return false;
    }

    if (expectAutoLaunch)
    {
        std::ifstream appcfg(configDir + "/vrappconfig/" + APP_KEY + ".vrappconfig");
        std::string cfg((std::istreambuf_iterator<char>(appcfg)),
                        std::istreambuf_iterator<char>());
        if (cfg.find("\"autolaunch\"") == std::string::npos ||
            cfg.find("true") == std::string::npos)
        {
            if (detail)
            {
                *detail = "Steam did not persist the auto-launch flag (" + configDir +
                          "/vrappconfig/" + APP_KEY + ".vrappconfig)";
            }
            return false;
        }
    }

    return true;
}

Status CheckRegistration()
{
    Status status;

    ScopedVRSession session(vr::VRApplication_Utility);
    if (!session.Valid())
    {
        return status;
    }
    status.steamvrAvailable = true;

    vr::IVRApplications* apps = vr::VRApplications();
    if (!apps)
    {
        return status;
    }

    status.registered = apps->IsApplicationInstalled(APP_KEY);
    if (status.registered)
    {
        status.autoLaunch = apps->GetApplicationAutoLaunch(APP_KEY);

        char workingDir[1024] = {0};
        vr::EVRApplicationError err = vr::VRApplicationError_None;
        apps->GetApplicationPropertyString(APP_KEY,
                                           vr::VRApplicationProperty_WorkingDirectory_String,
                                           workingDir, sizeof(workingDir), &err);
        if (err == vr::VRApplicationError_None)
        {
            status.workingDirectory = workingDir;
        }
    }

    return status;
}

}  // namespace vrreg
