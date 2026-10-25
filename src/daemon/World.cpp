#include "daemon/World.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include "core/WorldFile.hpp"

namespace bitzkey::daemon {

namespace {
constexpr auto MAX_TURN_SLICE = std::chrono::milliseconds(25);  // at full speed, the longest the lock is held at once
}

World::World(Simulation::Settings settings, bool playing, const std::filesystem::path& data_dir)
    : pool_(std::make_unique<Pool>(data_dir / "workers.json")), worker_(std::make_unique<Worker>(data_dir / "worker.json")),
      simulation_(std::make_unique<Simulation>(settings)), settings_(std::move(settings)),
      probe_([this](const std::function<void(Simulation&)>& look) {
          auto held = lock();
          look(*simulation_);
      }),
      playing_(playing) {
    paths_ = {data_dir / "catalog.json", data_dir / "exports", data_dir / "reports", data_dir / "worlds"};
    std::filesystem::create_directories(data_dir);
    adopt_pool();
    thread_ = std::thread([this] { run(); });
}

void World::adopt_pool() {
    pool_->adopt(*simulation_);
    simulation_->on_turn = [this](const Simulation& turned) { probe_.after_turn(turned); };  // watched tracks follow it
    touch();
}

World::~World() {
    stopping_ = true;
    if (thread_.joinable()) thread_.join();
}

void World::play(bool on) {
    playing_ = on && !simulation_->ended() && !worker_->on();
    touch();
}

void World::set_speed(int speed) {
    speed_ = std::max(0, std::min(static_cast<int>(TURN_INTERVALS.size()) - 1, speed));
    touch();
}

bool World::step() {
    playing_ = false;
    const int before = simulation_->turn();
    simulation_->advance();
    touch();
    return simulation_->turn() != before;
}

bool World::rewind() {
    playing_ = false;
    const bool moved = simulation_->rewind();
    touch();
    return moved;
}

void World::seek(int turn) {
    playing_ = false;
    simulation_->seek(turn);
    touch();
}

void World::new_world(Simulation::Settings settings, rules::Values rules) {
    rules::apply(rules);  // the advanced rules hold for this world and its restarts
    auto made = std::make_unique<Simulation>(settings);  // may throw, leaving the present world as it is
    simulation_ = std::move(made);
    settings_ = std::move(settings);
    rules_ = std::move(rules);
    playing_ = true;
    ++world_number_;
    adopt_pool();
}

void World::restart() { new_world(settings_, rules_); }

void World::load_world(std::unique_ptr<Simulation> loaded) {
    simulation_ = std::move(loaded);
    rules_ = rules::changed(rules::current());  // the rules it brought, which its restarts keep
    playing_ = false;
    ++world_number_;
    adopt_pool();
}

void World::keep(const std::filesystem::path& path) {
    std::string data;
    {
        std::lock_guard lock(mutex_);
        data = WorldFile::bytes(*simulation_);
    }
    std::filesystem::create_directories(path.parent_path());
    auto partial = path;
    partial += ".partial";
    {
        std::ofstream file(partial, std::ios::binary | std::ios::trunc);
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!file) throw std::runtime_error("couldn't write " + partial.string());
    }
    std::filesystem::rename(partial, path);
}

bool World::resume(const std::filesystem::path& path, bool playing) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    const std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::lock_guard lock(mutex_);  // its rules become the rules, so not while the world turns
    load_world(WorldFile::from_bytes(data));
    play(playing);
    return true;
}

void World::change_rules(rules::Values rules) {
    playing_ = false;
    rules::apply(rules);
    simulation_->rules_changed();
    rules_ = std::move(rules);
    ++version_;
}

void World::run() {
    using Clock = std::chrono::steady_clock;
    auto last_turn = Clock::now();
    while (!stopping_) {
        while (waiting_ > 0 && !stopping_) std::this_thread::sleep_for(std::chrono::microseconds(200));  // viewers first
        bool busy = false;
        try {
            std::lock_guard lock(mutex_);
            if (worker_->on()) playing_ = false;  // a worker thinks for another's world, not its own
            if (pool_->bound()) pool_->tend(*simulation_);
            if (playing_ && !simulation_->ended()) {
                const double interval = TURN_INTERVALS[static_cast<std::size_t>(speed_)];
                const auto now = Clock::now();
                if (interval <= 0) {  // as fast as possible, in slices, letting viewers in between
                    const auto start = now;
                    do {
                        simulation_->advance();
                    } while (!simulation_->ended() && Clock::now() - start < MAX_TURN_SLICE);
                    ++version_;
                    busy = true;
                } else if (std::chrono::duration<double>(now - last_turn).count() >= interval) {
                    simulation_->advance();
                    last_turn = now;
                    ++version_;
                }
                if (simulation_->ended()) {  // the run is over: it stops there, and viewers show its graph
                    playing_ = false;
                    ++version_;
                }
            }
        } catch (const std::exception& error) {  // the world pauses, and the daemon keeps serving it, rather than stop
            std::fprintf(stderr, "bitzkey_daemon: the world paused at turn %d: %s\n", simulation_->turn(), error.what());
            std::lock_guard lock(mutex_);
            playing_ = false;
            ++version_;
        }
        if (!busy) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        else std::this_thread::yield();
    }
}

}  // namespace bitzkey::daemon
