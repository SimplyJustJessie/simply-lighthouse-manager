#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

#include "BaseStationController.h"
#include "BaseStationDetector.h"
#include "Config.h"

// Interruptible-wait flag shared between the control loop and workers.
class CancellationToken
{
public:
    void Cancel();
    bool IsCancelled() const { return cancelled.load(); }

    // Sleeps up to the given duration; returns early (false) when cancelled.
    bool WaitFor(std::chrono::milliseconds duration);

private:
    std::atomic<bool> cancelled{false};
    std::mutex m;
    std::condition_variable cv;
};

// Owns the wake -> keep-alive -> sleep lifecycle for the stations selected by
// the config. Not thread-safe by itself: call it from one thread.
class AutoManager
{
public:
    AutoManager(BaseStationDetector& detector, Config config);

    // Wakes every station the config marks managed. Known stations are woken
    // immediately in parallel (no discovery latency); a discovery scan only
    // runs afterwards to catch stations BlueZ has not seen yet. Stations stay
    // connected for keep-alives. Interruptible via token.
    void WakeManaged(CancellationToken& token);

    // Re-sends a wake packet to all connected stations. Call periodically
    // while SteamVR runs; stations otherwise drift back to sleep.
    void KeepAlive();

    // Puts every managed station to sleep, one thread per station, bounded by
    // the deadline. Failures within budget get one retry pass. Disconnects
    // and drops all controllers.
    void SleepManagedFast(std::chrono::milliseconds budget);

    // Replace the config (e.g. after the file changed on disk). Stations that
    // become unmanaged are put to sleep and dropped; newly managed ones are
    // picked up by the next WakeManaged call.
    void UpdateConfig(const Config& newConfig, CancellationToken& token);

    size_t ManagedCount() const { return controllers.size(); }
    bool IsManaging(const std::string& address) const;

private:
    BaseStationDetector& detector;
    Config config;
    std::vector<std::unique_ptr<BaseStationController>> controllers;

    // Connect+wake the given stations concurrently (one thread each) and
    // adopt the successful controllers. Skips unmanaged/already-managed ones.
    void WakeStationsParallel(const std::vector<BaseStationInfo>& stations,
                              CancellationToken& token);
};
