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

void AutoManager::WakeManaged(CancellationToken& token)
{
    const int maxRetries = 5;
    const int scanTimeoutSeconds = 20;
    std::set<std::string> everSeen;

    for (int retry = 0; retry < maxRetries && !token.IsCancelled(); retry++)
    {
        if (retry > 0 && !token.WaitFor(std::chrono::milliseconds(2000)))
        {
            break;
        }

        auto stations = detector.ScanForBaseStations(scanTimeoutSeconds);

        for (const auto& station : stations)
        {
            if (token.IsCancelled())
            {
                break;
            }
            everSeen.insert(station.address);

            if (IsManaging(station.address))
            {
                continue;
            }
            if (!config.IsManaged(station))
            {
                std::cout << "[auto] Skipping " << station.name
                          << " (excluded by config)\n";
                continue;
            }

            auto controller = std::make_unique<BaseStationController>();
            if (controller->Connect(station))
            {
                if (!token.WaitFor(std::chrono::milliseconds(200)))
                {
                    break;
                }
                if (controller->Wake())
                {
                    std::cout << "[auto] Woke " << station.name << "\n";
                    controllers.push_back(std::move(controller));
                }
                else
                {
                    std::cerr << "[auto] Failed to wake " << station.name << "\n";
                    controller->Disconnect();
                }
            }
            else
            {
                std::cerr << "[auto] Failed to connect to " << station.name << "\n";
            }
        }

        if (!everSeen.empty() && controllers.size() >= everSeen.size())
        {
            break;  // everything discovered so far is awake
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
                    if (controllers[i]->Sleep())
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
