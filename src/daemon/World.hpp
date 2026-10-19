#pragma once
// The one world the daemon runs, shared by everyone viewing it. It turns on its own thread at the chosen
// speed; the web interface reads it and steers it through the same lock, held only briefly, so the
// simulation never waits on a browser.

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "core/Rules.hpp"
#include "core/Simulation.hpp"
#include "net/Pool.hpp"
#include "net/Worker.hpp"

namespace bitzkey::daemon {

// Workers, as the desktop app has them too.
using net::DEFAULT_CHECKPOINT_TURNS;
using net::DEFAULT_WORKER_PORT;
using net::Pool;
using net::Resources;
using net::Worker;

// Seconds between turns at each speed, as in the window app: 1x, 2x, 4x, 8x, and as fast as possible.
constexpr std::array<double, 5> TURN_INTERVALS = {0.24, 0.12, 0.06, 0.03, 0.0};
constexpr std::array<const char*, 5> SPEED_LABELS = {"1x", "2x", "4x", "8x", "MAX"};

class World {
public:
    struct Paths {
        std::filesystem::path catalog, exports, reports, worlds;
    };

    World(Simulation::Settings settings, bool playing, const std::filesystem::path& data_dir);
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // Hold this while reading or changing anything below. A viewer waiting for it is let in before the world
    // turns again, however fast it runs: a plain mutex, released and at once retaken by the turning thread,
    // could keep a viewer waiting indefinitely.
    std::unique_lock<std::mutex> lock() {
        ++waiting_;
        std::unique_lock<std::mutex> held(mutex_);
        --waiting_;
        return held;
    }

    Simulation& simulation() { return *simulation_; }
    const Simulation::Settings& settings() const { return settings_; }
    const rules::Values& rules() const { return rules_; }
    const Paths& paths() const { return paths_; }
    bool playing() const { return playing_; }
    int speed() const { return speed_; }
    // Grows whenever the world changes, so a viewer can tell whether it has anything new to fetch.
    std::uint64_t version() const { return version_; }
    // Grows with each new world, so a viewer can forget what it knew of the last.
    std::uint64_t world_number() const { return world_number_; }

    void play(bool on);
    void set_speed(int speed);
    bool step();    // a turn on, paused
    bool rewind();  // a turn back, paused
    void seek(int turn);
    // A new world from these settings and advanced rules, replacing the present one.
    void new_world(Simulation::Settings settings, rules::Values rules);
    void restart();  // the same settings again
    // Carry on this world, loaded from a file (its rules already applied), in place of the present one, paused;
    // restarting it starts afresh by the settings.
    void load_world(std::unique_ptr<Simulation> loaded);
    // Pause, and play by these advanced rules (changed from the defaults) from the next turn; its restarts keep them.
    void change_rules(rules::Values rules);
    void touch() { ++version_; }  // something changed that viewers should fetch again

    // The workers this main service shares its brains with, and worker mode, in which it thinks for another.
    Pool& pool() { return *pool_; }
    Worker& worker() { return *worker_; }
    // Think through the pool while it has workers, and alone when it has none: after the list changes, or the world.
    void adopt_pool();

private:
    void run();

    std::unique_ptr<Pool> pool_;      // first, so it outlasts the world that thinks through it
    std::unique_ptr<Worker> worker_;
    std::mutex mutex_;
    std::atomic<int> waiting_{0};  // viewers waiting for the lock
    std::unique_ptr<Simulation> simulation_;
    Simulation::Settings settings_;
    rules::Values rules_;
    Paths paths_;
    bool playing_ = true;
    int speed_ = 0;
    std::uint64_t version_ = 1;
    std::uint64_t world_number_ = 1;
    std::atomic<bool> stopping_ = false;
    std::thread thread_;  // last, so it starts once the rest is ready
};

}  // namespace bitzkey::daemon
