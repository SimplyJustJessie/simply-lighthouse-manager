#include <fcntl.h>
#include <openvr.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../core/AutoManager.h"
#include "../core/BaseStationController.h"
#include "../core/BaseStationDetector.h"
#include "../core/Config.h"
#include "../core/SteamVRWatcher.h"
#include "../core/VRRegistration.h"

static volatile std::sig_atomic_t g_signal = 0;

extern "C" void signalHandler(int)
{
    g_signal = 1;
}

void PrintUsage(const char* programName)
{
    std::cout << "Lighthouse Manager - Linux Edition\n";
    std::cout << "Usage: " << programName << " [COMMAND] [OPTIONS]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  --list, --scan           Scan and list detected base stations\n";
    std::cout << "  --enable, --wake <id>    Wake up a base station (by ID or address)\n";
    std::cout << "  --disable, --sleep <id>  Put a base station to sleep (by ID or address)\n";
    std::cout << "  --standby <id>           Put a base station to standby (by ID or address)\n";
    std::cout << "  --auto                   Manage base stations for the current SteamVR session\n";
    std::cout << "                           (used by the SteamVR auto-launch entry)\n";
    std::cout << "  --list-managed           Show the auto-manage configuration\n";
    std::cout << "  --manage <id>            Include a base station in auto-management\n";
    std::cout << "                           (auto-management is opt-in; nothing is managed\n";
    std::cout << "                           until you mark stations or run --manage-all)\n";
    std::cout << "  --unmanage <id>          Exclude a base station from auto-management\n";
    std::cout << "  --manage-all             Manage every discovered station automatically\n";
    std::cout << "  --register-manifest      Register with SteamVR and enable auto-launch\n";
    std::cout << "  --disable-autolaunch     Disable SteamVR auto-launch\n";
    std::cout << "  --check-registration     Check if application is registered with SteamVR\n";
    std::cout << "  --help, -h               Display this help message\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << programName << " --list\n";
    std::cout << "  " << programName << " --wake 699A51BC\n";
    std::cout << "  " << programName << " --sleep D4:1D:FE:B1:FE:E8\n";
    std::cout << "  " << programName << " --unmanage LHB-699A51BC\n";
    std::cout << "\nNote: Base station ID can be:\n";
    std::cout << "  - 8-character ID (e.g., 699A51BC)\n";
    std::cout << "  - MAC address (e.g., D4:1D:FE:B1:FE:E8)\n";
    std::cout << "  - Partial name match\n";
    std::cout << "\nConfig file: " << Config::DefaultPath() << "\n";
}

void ListBaseStations()
{
    BaseStationDetector detector;
    if (!detector.Initialize())
    {
        std::cerr << "Failed to initialize Bluetooth adapter\n";
        std::cerr << "Make sure Bluetooth is enabled and working\n";
        return;
    }

    Config config;
    config.Load();

    auto stations = detector.ScanForBaseStations(15);

    if (stations.empty())
    {
        std::cout << "No base stations found\n";
        return;
    }

    std::cout << "Found " << stations.size() << " base station(s):\n";
    for (size_t i = 0; i < stations.size(); ++i)
    {
        const auto& station = stations[i];
        std::cout << "[" << i << "] " << station.name << "\n";
        std::cout << "     ID: " << station.id << "\n";
        std::cout << "     Address: " << station.address << "\n";
        std::cout << "     Type: " << (station.isBaseStation1 ? "1.0" : "2.0") << "\n";
        std::cout << "     Auto-managed: " << (config.IsManaged(station) ? "yes" : "no") << "\n";
    }
}

void ControlBaseStation(const std::string& stationId, BaseStationCommand command)
{
    BaseStationDetector detector;
    if (!detector.Initialize())
    {
        std::cerr << "Failed to initialize Bluetooth\n";
        return;
    }

    auto stations = detector.ScanForBaseStations(10);

    if (stations.empty())
    {
        std::cerr << "No base stations found. Cannot control.\n";
        return;
    }

    BaseStationInfo* targetStation = nullptr;
    for (auto& station : stations)
    {
        if (station.id == stationId || station.address == stationId ||
            station.serial == stationId || station.name.find(stationId) != std::string::npos)
        {
            targetStation = &station;
            break;
        }
    }

    if (!targetStation)
    {
        std::cerr << "Base station not found: " << stationId << "\n";
        std::cerr << "\nAvailable base stations:\n";
        for (size_t i = 0; i < stations.size(); ++i)
        {
            std::cerr << "  [" << i << "] " << stations[i].name
                      << " (ID: " << stations[i].id << ")\n";
        }
        return;
    }

    BaseStationController controller;
    if (!controller.Connect(*targetStation))
    {
        std::cerr << "Failed to connect to base station\n";
        return;
    }

    bool success = false;
    const char* commandName = "";

    switch (command)
    {
        case BaseStationCommand::Wake:
            success = controller.Wake();
            commandName = "Wake";
            break;
        case BaseStationCommand::Sleep:
            success = controller.Sleep();
            commandName = "Sleep";
            break;
        case BaseStationCommand::Standby:
            success = controller.Standby();
            commandName = "Standby";
            break;
    }

    if (success)
    {
        std::cout << "✓ " << commandName << " command sent\n";
    }
    else
    {
        std::cerr << "✗ Failed to send " << commandName << " command\n";
    }

    controller.Disconnect();
}

void ListManagedConfig()
{
    Config config;
    bool loaded = config.Load();

    std::cout << "Config file: " << Config::DefaultPath()
              << (loaded ? "" : " (not created yet - defaults shown)") << "\n";
    std::cout << "Mode: "
              << (config.manageMode == Config::ManageMode::All
                      ? "all (every discovered station is auto-managed unless excluded)"
                      : "selected (only stations marked managed are auto-managed)")
              << "\n";

    if (config.stations.empty())
    {
        std::cout << "No per-station entries.\n";
        return;
    }

    std::cout << "Stations:\n";
    for (const auto& [address, entry] : config.stations)
    {
        std::cout << "  " << address;
        if (!entry.name.empty())
        {
            std::cout << " (" << entry.name << ")";
        }
        std::cout << " - " << (entry.managed ? "managed" : "excluded") << "\n";
    }
}

int SetManagedFlag(const std::string& idOrAddress, bool managed)
{
    Config config;
    config.Load();

    std::string address;
    std::string name;

    // Resolve against existing config entries first (no scan needed).
    if (config.stations.count(idOrAddress))
    {
        address = idOrAddress;
        name = config.stations[idOrAddress].name;
    }
    else
    {
        for (const auto& [addr, entry] : config.stations)
        {
            if (!entry.name.empty() && entry.name.find(idOrAddress) != std::string::npos)
            {
                address = addr;
                name = entry.name;
                break;
            }
        }
    }

    // Fall back to a scan.
    if (address.empty())
    {
        std::cout << "Station not in config - scanning...\n";
        BaseStationDetector detector;
        if (!detector.Initialize())
        {
            std::cerr << "Failed to initialize Bluetooth\n";
            return 1;
        }
        auto stations = detector.ScanForBaseStations(10);
        for (const auto& station : stations)
        {
            if (station.id == idOrAddress || station.address == idOrAddress ||
                station.serial == idOrAddress ||
                station.name.find(idOrAddress) != std::string::npos)
            {
                address = station.address;
                name = station.name;
                break;
            }
        }
        if (address.empty())
        {
            std::cerr << "Base station not found: " << idOrAddress << "\n";
            if (!stations.empty())
            {
                std::cerr << "Available:\n";
                for (const auto& station : stations)
                {
                    std::cerr << "  " << station.name << " (" << station.address << ")\n";
                }
            }
            return 1;
        }
    }

    config.SetManaged(address, name, managed);
    if (!config.Save())
    {
        std::cerr << "Failed to write " << Config::DefaultPath() << "\n";
        return 1;
    }

    std::cout << (name.empty() ? address : name) << " is now "
              << (managed ? "auto-managed" : "excluded from auto-management") << "\n";
    std::cout << "A running auto service picks this up within ~15 seconds.\n";
    return 0;
}

int ManageAll()
{
    Config config;
    config.Load();
    config.manageMode = Config::ManageMode::All;
    for (auto& [address, entry] : config.stations)
    {
        entry.managed = true;
    }
    if (!config.Save())
    {
        std::cerr << "Failed to write " << Config::DefaultPath() << "\n";
        return 1;
    }
    std::cout << "All discovered base stations will be auto-managed.\n";
    return 0;
}

// Streambuf that prefixes every line with a wall-clock timestamp and flushes
// on newline - shared by cout and cerr when logging to a file.
class TimestampLogBuf : public std::streambuf
{
public:
    explicit TimestampLogBuf(FILE* file) : file(file) {}

protected:
    int overflow(int c) override
    {
        if (c == EOF)
        {
            return 0;
        }
        std::lock_guard<std::mutex> lock(m);  // cout/cerr are used from two threads
        if (atLineStart)
        {
            time_t now = time(nullptr);
            struct tm tmBuf;
            localtime_r(&now, &tmBuf);
            char stamp[32];
            strftime(stamp, sizeof(stamp), "[%H:%M:%S] ", &tmBuf);
            fputs(stamp, file);
            atLineStart = false;
        }
        fputc(c, file);
        if (c == '\n')
        {
            atLineStart = true;
            fflush(file);
        }
        return c;
    }

private:
    FILE* file;
    std::mutex m;
    bool atLineStart = true;
};

// When SteamVR launches the service there is no terminal - divert output to
// a log file so failures in the field are diagnosable.
void RedirectOutputToLogFile()
{
    if (isatty(STDOUT_FILENO))
    {
        return;
    }

    const char* stateHome = std::getenv("XDG_STATE_HOME");
    std::string dir;
    if (stateHome && *stateHome)
    {
        dir = std::string(stateHome);
    }
    else
    {
        const char* home = std::getenv("HOME");
        if (!home)
        {
            return;
        }
        dir = std::string(home) + "/.local/state";
        mkdir(dir.c_str(), 0755);
    }
    dir += "/lighthouse-manager";
    mkdir(dir.c_str(), 0755);

    std::string logPath = dir + "/auto.log";
    FILE* logFile = std::fopen(logPath.c_str(), "w");
    if (!logFile)
    {
        return;
    }
    // Leaked deliberately: must outlive every user of cout/cerr.
    static TimestampLogBuf* logBuf = nullptr;
    logBuf = new TimestampLogBuf(logFile);
    std::cout.rdbuf(logBuf);
    std::cerr.rdbuf(logBuf);
}

// Two concurrent services fight: their disconnects drop each other's BLE
// links, and one instance's keep-alive re-wakes stations the other just put
// to sleep. Take an exclusive per-user lock; the fd stays open (and the lock
// held) for the process lifetime.
bool AcquireSingleInstanceLock()
{
    const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
    std::string dir;
    if (runtimeDir && *runtimeDir)
    {
        dir = runtimeDir;
    }
    else
    {
        const char* home = std::getenv("HOME");
        dir = std::string(home ? home : "/tmp");
    }
    std::string lockPath = dir + "/lighthouse-manager-auto.lock";

    int fd = open(lockPath.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0644);
    if (fd < 0)
    {
        return true;  // cannot lock - do not block the service over it
    }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0)
    {
        close(fd);
        return false;
    }
    return true;  // fd intentionally kept open until process exit
}

// Headless SteamVR-session service. Single-threaded OpenVR ownership: the
// context is initialized once here and never touched from another thread.
int AutoManage()
{
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    if (!AcquireSingleInstanceLock())
    {
        // Deliberately before the log redirect so the losing instance does
        // not truncate the running instance's log file.
        std::cerr << "[auto] Another auto service instance is already running - exiting\n";
        return 0;
    }

    RedirectOutputToLogFile();

    // When launched by SteamVR the server is already up; when started
    // manually, wait for it.
    // Overlay type, not Background: SteamVR only grants the quit-handshake
    // grace period (AcknowledgeQuit_Exiting + cleanup time) to overlay apps;
    // background apps can be killed before they can sleep the stations.
    // Overlay init works headless - no overlay is ever drawn. It DOES
    // auto-start SteamVR though, so only attempt it once vrserver is
    // already up.
    std::unique_ptr<vrreg::ScopedVRSession> session;
    bool announcedWait = false;
    while (!g_signal)
    {
        if (SteamVRWatcher::IsVrServerProcessRunning())
        {
            session = std::make_unique<vrreg::ScopedVRSession>(vr::VRApplication_Overlay);
            if (session->Valid())
            {
                break;
            }
            session.reset();
        }
        if (!announcedWait)
        {
            std::cout << "[auto] Waiting for SteamVR to start...\n";
            announcedWait = true;
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    if (g_signal || !session)
    {
        return 0;
    }
    std::cout << "[auto] Connected to SteamVR\n";

    // Keep the registration fresh (heals stale install paths); deliberately
    // does NOT touch the auto-launch flag - that stays the user's choice.
    vrreg::RegisterManifestWithSession(session->System(), false, false);

    Config config;
    config.Load();

    BaseStationDetector detector;
    if (!detector.Initialize())
    {
        std::cerr << "[auto] Failed to initialize Bluetooth - exiting\n";
        return 1;
    }

    AutoManager manager(detector, config);
    CancellationToken token;
    SteamVRWatcher watcher(session->System());

    // The wake phase (BLE connects, possibly discovery scans) can take tens
    // of seconds; it runs on its own thread so this loop keeps polling for
    // SteamVR's quit event the whole time. Nothing else touches `manager`
    // until the wake thread is joined.
    std::atomic<bool> wakeDone{false};
    std::thread wakeThread(
        [&manager, &token, &wakeDone]()
        {
            manager.WakeManaged(token);
            wakeDone = true;
        });
    bool wakeJoined = false;
    auto JoinWake = [&]()
    {
        if (wakeThread.joinable())
        {
            wakeThread.join();
        }
    };

    enum class ExitReason
    {
        Signal,
        QuitRequested,
        ProcessDead
    };
    ExitReason reason = ExitReason::Signal;

    auto lastKeepAlive = std::chrono::steady_clock::now();
    auto lastConfigCheck = lastKeepAlive;
    auto configMtime = Config::FileMtime();

    bool running = true;
    while (running)
    {
        if (g_signal)
        {
            reason = ExitReason::Signal;
            break;
        }

        switch (watcher.Poll())
        {
            case SteamVRWatcher::Status::QuitRequested:
                reason = ExitReason::QuitRequested;
                running = false;
                continue;
            case SteamVRWatcher::Status::ProcessDead:
                reason = ExitReason::ProcessDead;
                running = false;
                continue;
            case SteamVRWatcher::Status::Running:
                break;
        }

        if (!wakeJoined)
        {
            if (!wakeDone)
            {
                // Keep-alives and config reloads wait until the wake phase
                // hands the manager over.
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                continue;
            }
            JoinWake();
            wakeJoined = true;
            std::cout << "[auto] Managing " << manager.ManagedCount() << " base station(s)\n";
            if (manager.ManagedCount() == 0)
            {
                std::cout << "[auto] Auto-management is opt-in: mark stations with\n"
                             "[auto]   lighthouse-manager --manage <id>\n"
                             "[auto] or the GUI's Auto checkboxes.\n";
            }
        }

        auto now = std::chrono::steady_clock::now();

        if (now - lastKeepAlive >= std::chrono::seconds(5))
        {
            manager.KeepAlive();
            lastKeepAlive = now;
        }

        if (now - lastConfigCheck >= std::chrono::seconds(15))
        {
            lastConfigCheck = now;
            auto mtime = Config::FileMtime();
            if (mtime != configMtime)
            {
                configMtime = mtime;
                std::cout << "[auto] Config changed - reloading\n";
                Config fresh;
                fresh.Load();
                manager.UpdateConfig(fresh, token);
                config = fresh;

                bool needWake = false;
                for (const auto& [address, entry] : config.stations)
                {
                    if (entry.managed && !manager.IsManaging(address))
                    {
                        needWake = true;
                        break;
                    }
                }
                if (needWake)
                {
                    manager.WakeManaged(token);
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    switch (reason)
    {
        case ExitReason::QuitRequested:
            std::cout << "[auto] SteamVR is shutting down\n";
            // Tell SteamVR we heard it so it does not kill us mid-cleanup.
            if (session->System())
            {
                session->System()->AcknowledgeQuit_Exiting();
            }
            break;
        case ExitReason::ProcessDead:
            std::cout << "[auto] SteamVR process died\n";
            break;
        case ExitReason::Signal:
            std::cout << "[auto] Received termination signal\n";
            break;
    }

    // Release the OpenVR IPC before the (bounded) BLE cleanup. The wake
    // thread must be stopped and joined before the manager sleeps stations.
    session->Shutdown();
    token.Cancel();
    JoinWake();
    manager.SleepManagedFast(std::chrono::milliseconds(4000));
    std::cout << "[auto] Done\n";
    return 0;
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        PrintUsage(argv[0]);
        return 1;
    }

    std::string command = argv[1];

    if (command == "--help" || command == "-h")
    {
        PrintUsage(argv[0]);
        return 0;
    }
    else if (command == "--list" || command == "--scan")
    {
        ListBaseStations();
    }
    else if (command == "--enable" || command == "--wake")
    {
        if (argc < 3)
        {
            std::cerr << "Error: Base station ID required\n";
            PrintUsage(argv[0]);
            return 1;
        }
        ControlBaseStation(argv[2], BaseStationCommand::Wake);
    }
    else if (command == "--disable" || command == "--sleep")
    {
        if (argc < 3)
        {
            std::cerr << "Error: Base station ID required\n";
            PrintUsage(argv[0]);
            return 1;
        }
        ControlBaseStation(argv[2], BaseStationCommand::Sleep);
    }
    else if (command == "--standby")
    {
        if (argc < 3)
        {
            std::cerr << "Error: Base station ID required\n";
            PrintUsage(argv[0]);
            return 1;
        }
        ControlBaseStation(argv[2], BaseStationCommand::Standby);
    }
    else if (command == "--auto")
    {
        return AutoManage();
    }
    else if (command == "--list-managed")
    {
        ListManagedConfig();
    }
    else if (command == "--manage")
    {
        if (argc < 3)
        {
            std::cerr << "Error: Base station ID required\n";
            return 1;
        }
        return SetManagedFlag(argv[2], true);
    }
    else if (command == "--unmanage")
    {
        if (argc < 3)
        {
            std::cerr << "Error: Base station ID required\n";
            return 1;
        }
        return SetManagedFlag(argv[2], false);
    }
    else if (command == "--manage-all")
    {
        return ManageAll();
    }
    else if (command == "--register-manifest")
    {
        if (!vrreg::RegisterManifest(true, true))
        {
            return 1;
        }
        std::string detail;
        if (!vrreg::VerifyPersistedOnDisk(true, &detail))
        {
            std::cerr << detail << "\n"
                      << "Start SteamVR normally (from Steam), then run "
                         "--register-manifest again.\n";
            return 1;
        }
        std::cout << "Registration verified in Steam's config store\n";
        return 0;
    }
    else if (command == "--disable-autolaunch")
    {
        return vrreg::DisableAutoLaunch(true) ? 0 : 1;
    }
    else if (command == "--check-registration")
    {
        vrreg::Status status = vrreg::CheckRegistration();
        if (!status.steamvrAvailable)
        {
            std::cerr << "Failed to initialize OpenVR - make sure SteamVR is running\n";
            return 1;
        }

        std::cout << "Registration Status:\n";
        std::cout << "  App Key: " << vrreg::APP_KEY << "\n";
        std::cout << "  Registered: " << (status.registered ? "Yes" : "No") << "\n";
        if (status.registered)
        {
            std::cout << "  Auto-launch: " << (status.autoLaunch ? "Enabled" : "Disabled") << "\n";
            if (!status.workingDirectory.empty())
            {
                std::cout << "  Working Directory: " << status.workingDirectory << "\n";
            }
        }
        else
        {
            std::cout << "\nTo register, run:\n";
            std::cout << "  lighthouse-manager --register-manifest\n";
        }
        return status.registered ? 0 : 1;
    }
    else
    {
        std::cerr << "Unknown command: " << command << "\n\n";
        PrintUsage(argv[0]);
        return 1;
    }

    return 0;
}
