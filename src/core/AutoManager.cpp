#include "AutoManager.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <thread>

void CancellationToken::Cancel()
{
    {
        std::lock_guard<std::mutex> lock(m);
        cancelled = true;
    }
    cv.notify_all();
}

bool CancellationToken::WaitFor(std::chrono::milliseconds duration)
{
    std::unique_lock<std::mutex> lock(m);
    return !cv.wait_for(lock, duration, [this] { return cancelled.load(); });
}

AutoManager::AutoManager(BaseStationDetector& detector, Config config)
    : detector(detector), config(std::move(config))
{
}

bool AutoManager::IsManaging(const std::string& address) const
{
    return std::any_of(controllers.begin(), controllers.end(),
                       [&](const std::unique_ptr<BaseStationController>& c)
                       { return c->GetStationInfo().address == address; });
}

void AutoManager::WakeStationsParallel(const std::vector<BaseStationInfo>& stations,
                                       CancellationToken& token)
{
    std::vector<const BaseStationInfo*> targets;
    for (const auto& station : stations)
    {
        if (IsManaging(station.address))
        {
            continue;
        }
        if (!config.IsManaged(station))
        {
            if (warnedExcluded.insert(station.address).second)
            {
                std::cout << "[auto] Skipping " << station.name << " (excluded by config)\n";
            }
            continue;
        }
        auto attempt = lastWakeAttempt.find(station.address);
        if (attempt != lastWakeAttempt.end() &&
            std::chrono::steady_clock::now() - attempt->second < std::chrono::seconds(5))
        {
            continue;  // just failed - let the adapter settle first
        }
        targets.push_back(&station);
    }
    if (targets.empty() || token.IsCancelled())
    {
        return;
    }

    std::mutex adoptMutex;
    std::vector<std::thread> workers;
    for (const BaseStationInfo* target : targets)
    {
        lastWakeAttempt[target->address] = std::chrono::steady_clock::now();
        workers.emplace_back(
            [this, target, &adoptMutex, &token]()
            {
                auto abort = [&token] { return token.IsCancelled(); };
                auto controller = std::make_unique<BaseStationController>();
                bool ok = controller->Connect(*target, abort) && controller->Wake(10, abort);
                if (ok)
                {
                    std::cout << "[auto] Woke " << target->name << "\n";
                    std::lock_guard<std::mutex> lock(adoptMutex);
                    controllers.push_back(std::move(controller));
                }
                else
                {
                    std::cerr << "[auto] Failed to wake " << target->name << "\n";
                    controller->Disconnect();
                }
            });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }
}

void AutoManager::WakeManaged(CancellationToken& token)
{
    // Fast path: stations BlueZ already knows wake immediately, in parallel.
    WakeStationsParallel(detector.ListKnownStations(), token);
    if (token.IsCancelled())
    {
        return;
    }

    // Discovery is only needed for stations BlueZ has never seen: always a
    // possibility in "all" mode, and in "selected" mode only when a
    // configured station is still missing.
    bool missingConfigured = false;
    for (const auto& [address, entry] : config.stations)
    {
        if (entry.managed && !IsManaging(address))
        {
            missingConfigured = true;
            break;
        }
    }
    if (config.manageMode != Config::ManageMode::All && !missingConfigured)
    {
        return;
    }

    const int maxRetries = 3;
    const int scanTimeoutSeconds = 15;
    std::set<std::string> managedSeen;

    for (int retry = 0; retry < maxRetries && !token.IsCancelled(); retry++)
    {
        if (retry > 0 && !token.WaitFor(std::chrono::milliseconds(2000)))
        {
            break;
        }

        // Wake stations the moment discovery sees them (the callback runs on
        // this thread) instead of waiting for the scan round to finish.
        auto stations = detector.ScanForBaseStations(
            scanTimeoutSeconds, [&token] { return token.IsCancelled(); },
            [this, &token](const std::vector<BaseStationInfo>& found)
            { WakeStationsParallel(found, token); });
        for (const auto& station : stations)
        {
            if (config.IsManaged(station))
            {
                managedSeen.insert(station.address);
            }
        }

        // Only stations we would actually manage count toward "done" -
        // excluded ones must not keep the retry loop alive.
        if (!managedSeen.empty() && controllers.size() >= managedSeen.size())
        {
            break;
        }
        if (config.manageMode != Config::ManageMode::All)
        {
            bool stillMissing = false;
            for (const auto& [address, entry] : config.stations)
            {
                if (entry.managed && !IsManaging(address))
                {
                    stillMissing = true;
                    break;
                }
            }
            if (!stillMissing)
            {
                break;
            }
        }
    }
}

void AutoManager::KeepAlive()
{
    for (auto& controller : controllers)
    {
        if (controller && controller->IsConnected())
        {
            controller->SendWakePacket();
        }
    }
}

void AutoManager::SleepManagedFast(std::chrono::milliseconds budget)
{
    if (controllers.empty())
    {
        return;
    }

    auto deadline = std::chrono::steady_clock::now() + budget;

    for (int pass = 0; pass < 2 && !controllers.empty(); pass++)
    {
        std::vector<std::thread> workers;
        std::vector<char> succeeded(controllers.size(), 0);  // vector<bool> is not thread-safe

        for (size_t i = 0; i < controllers.size(); i++)
        {
            workers.emplace_back(
                [&, i]()
                {
                    // Few retry rounds: this path runs inside SteamVR's
                    // shutdown grace period.
                    BaseStationCommand cmd =
                        config.powerOffMode == Config::PowerOffMode::Standby
                            ? BaseStationCommand::Standby
                            : BaseStationCommand::Sleep;
                    if (controllers[i]->SendCommand(cmd, 2))
                    {
                        controllers[i]->Disconnect();
                        succeeded[i] = 1;
                    }
                });
        }
        for (auto& worker : workers)
        {
            worker.join();
        }

        std::vector<std::unique_ptr<BaseStationController>> failed;
        for (size_t i = 0; i < controllers.size(); i++)
        {
            if (!succeeded[i])
            {
                std::cerr << "[auto] Failed to sleep "
                          << controllers[i]->GetStationInfo().name << "\n";
                failed.push_back(std::move(controllers[i]));
            }
        }
        controllers = std::move(failed);

        if (controllers.empty() || std::chrono::steady_clock::now() >= deadline)
        {
            break;
        }
    }

    if (!controllers.empty())
    {
        std::cerr << "[auto] WARNING: " << controllers.size()
                  << " station(s) could not be put to sleep before the deadline\n";
        for (auto& controller : controllers)
        {
            controller->Disconnect();
        }
    }
    controllers.clear();
}

void AutoManager::UpdateConfig(const Config& newConfig, CancellationToken& token)
{
    config = newConfig;

    // Sleep and drop stations that are no longer managed.
    std::vector<std::unique_ptr<BaseStationController>> kept;
    for (auto& controller : controllers)
    {
        if (config.IsManaged(controller->GetStationInfo()))
        {
            kept.push_back(std::move(controller));
        }
        else
        {
            std::cout << "[auto] Config change: putting "
                      << controller->GetStationInfo().name << " to sleep\n";
            controller->Sleep();
            controller->Disconnect();
        }
        if (token.IsCancelled())
        {
            break;
        }
    }
    controllers = std::move(kept);
}
