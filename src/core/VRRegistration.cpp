#include "VRRegistration.h"

#include <limits.h>
#include <unistd.h>
#include <iostream>

namespace vrreg
{

namespace
{

// The registration work itself, given a live VRApplications interface.
bool RegisterImpl(vr::IVRApplications* apps, bool setAutoLaunch, bool verbose)
{
    const std::string exeDir = ExecutableDir();
    if (exeDir.empty())
    {
        if (verbose)
        {
            std::cerr << "Failed to determine executable path\n";
        }
        return false;
    }
    const std::string manifestPath = exeDir + "/manifest.vrmanifest";

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
            if (verbose)
            {
                std::cerr << "Failed to add manifest: "
                          << apps->GetApplicationsErrorNameFromEnum(appError) << "\n"
                          << "Manifest path: " << manifestPath << "\n";
            }
            return false;
        }
    }

    if (setAutoLaunch)
    {
        vr::EVRApplicationError autoLaunchError = apps->SetApplicationAutoLaunch(APP_KEY, true);
        if (autoLaunchError != vr::VRApplicationError_None)
        {
            if (verbose)
            {
                std::cerr << "Warning: failed to enable auto-launch: "
                          << apps->GetApplicationsErrorNameFromEnum(autoLaunchError) << "\n";
            }
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

bool RegisterManifest(bool setAutoLaunch, bool verbose)
{
    ScopedVRSession session(vr::VRApplication_Utility);
    if (!session.Valid())
    {
        if (verbose)
        {
            std::cerr << "Failed to initialize OpenVR: "
                      << vr::VR_GetVRInitErrorAsEnglishDescription(session.Error()) << "\n"
                      << "Make sure SteamVR is running\n";
        }
        return false;
    }
    return RegisterManifestWithSession(session.System(), setAutoLaunch, verbose);
}

bool RegisterManifestWithSession(vr::IVRSystem* system, bool setAutoLaunch, bool verbose)
{
    if (!system)
    {
        return false;
    }
    vr::IVRApplications* apps = vr::VRApplications();
    if (!apps)
    {
        if (verbose)
        {
            std::cerr << "Failed to get VRApplications interface\n";
        }
        return false;
    }
    return RegisterImpl(apps, setAutoLaunch, verbose);
}

bool DisableAutoLaunch(bool verbose)
{
    ScopedVRSession session(vr::VRApplication_Utility);
    if (!session.Valid())
    {
        if (verbose)
        {
            std::cerr << "Failed to initialize OpenVR: "
                      << vr::VR_GetVRInitErrorAsEnglishDescription(session.Error()) << "\n"
                      << "Make sure SteamVR is running\n";
        }
        return false;
    }

    vr::IVRApplications* apps = vr::VRApplications();
    if (!apps)
    {
        return false;
    }

    vr::EVRApplicationError err = apps->SetApplicationAutoLaunch(APP_KEY, false);
    if (err != vr::VRApplicationError_None)
    {
        if (verbose)
        {
            std::cerr << "Failed to disable auto-launch: "
                      << apps->GetApplicationsErrorNameFromEnum(err) << "\n";
        }
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
