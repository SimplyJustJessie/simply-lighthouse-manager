#include "VRRegistration.h"

#include <limits.h>
#include <unistd.h>
#include <iostream>

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
    const std::string manifestPath = exeDir + "/manifest.vrmanifest";
    if (access(manifestPath.c_str(), R_OK) != 0)
    {
        ReportError("Manifest not found: " + manifestPath, verbose, error);
        return false;
    }

    bool needsRegistration = true;
    if (apps->IsApplicationInstalled(APP_KEY))
    {
        // Self-heal registrations pointing at a stale install location.
        char workingDir[1024] = {0};
        vr::EVRApplicationError propError = vr::VRApplicationError_None;
        apps->GetApplicationPropertyString(APP_KEY,
                                           vr::VRApplicationProperty_WorkingDirectory_String,
                                           workingDir, sizeof(workingDir), &propError);
        if (propError == vr::VRApplicationError_None && exeDir == workingDir)
        {
            needsRegistration = false;
            if (verbose)
            {
                std::cout << "Manifest already registered\n";
            }
        }
        else
        {
            if (verbose)
            {
                std::cout << "Replacing stale registration (was: " << workingDir << ")\n";
            }
            std::string oldManifest = std::string(workingDir) + "/manifest.vrmanifest";
            apps->RemoveApplicationManifest(oldManifest.c_str());
        }
    }

    if (needsRegistration)
    {
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
    }

    if (setAutoLaunch)
    {
        vr::EVRApplicationError autoLaunchError = apps->SetApplicationAutoLaunch(APP_KEY, true);
        if (autoLaunchError != vr::VRApplicationError_None)
        {
            // Registration itself succeeded; report but do not fail.
            ReportError(std::string("Warning: failed to enable auto-launch: ") +
                            apps->GetApplicationsErrorNameFromEnum(autoLaunchError),
                        verbose, error);
        }
        else if (verbose)
        {
            std::cout << "Auto-launch enabled\n";
        }
    }

    return true;
}

}  // namespace

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
