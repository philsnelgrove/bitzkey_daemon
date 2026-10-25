#pragma once
// The probe: a read-only window onto a running world, over HTTP, for analysing it as it goes. It shows the world's
// state, any Bitzkey (its senses, the odds it chose by, its mind, its family), the herds as they stand (each kept
// under the same number from one look to the next, as long as most of it holds together, with its drift and any
// split or merge), and the turn-by-turn tracks of the Bitzkeys it is asked to watch. It never changes the world.
// The desktop app and the daemon serve the same probe: the app on a port of this computer only, when asked to, and
// the daemon among its web interface's routes.

#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/Simulation.hpp"

namespace httplib {
class Server;
}

namespace bitzkey::net {

class Probe {
public:
    using Json = nlohmann::json;
    // Runs a look at the world while nothing else touches it: under the daemon's lock, or between the app's turns.
    // Throws std::runtime_error when there is no world to look at.
    using Run = std::function<void(const std::function<void(Simulation&)>&)>;

    explicit Probe(Run run);

    // Its routes, under `prefix` (such as "/probe" or "/api/probe"):
    //   GET    prefix/state                  the turn, season, map and how many of each species live
    //   GET    prefix/animals?species=&x=&y=&r=&limit=   living Bitzkeys, in brief: of a species, near a cell
    //   GET    prefix/animal/ID              one Bitzkey in full
    //   GET    prefix/family/ID              its parents and theirs as far as the world still holds them, and its
    //                                        living young and theirs
    //   GET    prefix/herds?species=&members=1&reach=   the herds of a species, as they stand: herd-mates within
    //                                        `reach` steps of one another (HERD_REACH unless given), and theirs
    //   POST   prefix/watch  {"ids": [...], "keep": turns}   watch these Bitzkeys, turn by turn
    //   GET    prefix/watch                  the tracks of those watched
    //   DELETE prefix/watch?ids=             stop watching these, or all of them
    void install(httplib::Server& server, const std::string& prefix);

    // After each new turn, with the world held: the watched Bitzkeys' tracks grow by a turn.
    void after_turn(const Simulation& world);

    // The same as the routes, for tests and for asking without a server.
    Json state(Simulation& world) const;
    Json animals(Simulation& world, const std::string& species, int x, int y, int radius, std::size_t limit) const;
    Json animal(Simulation& world, int identifier) const;
    Json family(Simulation& world, int identifier) const;
    // Herd-mates this many steps apart or nearer are one herd: as close as the herd-mates an animal counts as close by.
    static constexpr int HERD_REACH = 2;
    Json herds(Simulation& world, const std::string& species, bool members, int reach = HERD_REACH);
    void watch(const std::vector<int>& identifiers, std::size_t keep);
    Json tracks() const;
    void unwatch(const std::vector<int>& identifiers);  // all, when empty

private:
    struct Herd {
        int id = 0;
        std::set<int> members;
        double x = 0, y = 0;
    };

    Run run_;
    mutable std::mutex watch_mutex_;
    std::map<int, std::deque<Json>> watched_;  // by identifier: a sample for each turn, the oldest first
    std::size_t keep_ = 720;                     // turns of each track kept
    std::mutex herd_mutex_;
    std::map<std::string, std::vector<Herd>> herds_;  // by species: the herds as last seen, to keep their numbers
    int next_herd_ = 1;
};

}  // namespace bitzkey::net
