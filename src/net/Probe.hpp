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
    std::function<void()> on_rules;  // told, in the look that changed them, that the rules have changed

    // Its routes, under `prefix` (such as "/probe" or "/api/probe"):
    //   GET    prefix/state                  the turn, season, map and how many of each species live
    //   GET    prefix/animals?species=&x=&y=&r=&limit=   living Bitzkeys, in brief: of a species, near a cell
    //   GET    prefix/animal/ID              one Bitzkey in full
    //   GET    prefix/family/ID              its parents and theirs as far as the world still holds them, and its
    //                                        living young and theirs
    //   GET    prefix/herds?species=&members=1&reach=   the herds of a species, as they stand: herd-mates within
    //                                        `reach` steps of one another (HERD_REACH unless given), and theirs
    //   POST   prefix/watch  {"ids": [...], "keep": turns}   watch these Bitzkeys, turn by turn
    //   POST   prefix/rules  {"rules": {KEY: value, ...}}    change rules from the next turn, the rest as they are
    //   POST   prefix/settings  {"population_cap": N or null, "mind_mutation": percent, "body_mutation": percent}
    //   These two answer only those on this machine: anything come through the web server is refused.
    //   GET    prefix/watch                  the tracks of those watched
    //   DELETE prefix/watch?ids=             stop watching these, or all of them
    // And the rest of the world:
    //   GET    prefix/rules                  every advanced rule: its value, group, label, hint and limits
    //   GET    prefix/settings               the world's settings, its turns kept, the GPUs, and its totals
    //   GET    prefix/climate                each row: its temperature now, its grass's growth, and its grass
    //   GET    prefix/grass?map=1            the grass, row by row, and cell by cell if asked
    //   GET    prefix/cell?x=&y=             one square: its grass, its Bitzkeys living and dead, its seeds
    //   GET    prefix/bodies?limit=          the bodies still lying in the world, fresh kills or carrion
    //   GET    prefix/seeds?limit=           the seeds waiting in the ground
    //   GET    prefix/deaths?since=&species=&cause=&limit=   the deaths the world remembers, and every death's cause
    //                                        counted since the start
    //   GET    prefix/history?points=        each species' numbers over the run, as the graph remembers it
    //   GET    prefix/insights               how often each species took each action when it could, lately
    //   GET    prefix/brain/ID?weights=1     a brain: its shape, its values for what it last sensed, its weights if asked
    //   GET    prefix/diary                  the first time each thing was done: fire, cooking, a shelter, a recipe found or taught
    //   GET    prefix/skills                 each species' crafts known and tried, how each came to be known, gear in hand, and the teaching done: infants
    //                                        taught by a parent, students by an elder, and elders old enough to teach
    //   GET    prefix/minds                  each species' brains at a glance: how far their values run and how
    //                                        many of their neurons have jammed
    //   GET    prefix/reports/brain          the brain report, as text
    //   GET    prefix/reports/deaths         the death report, as text
    //   GET    prefix/timings                where each turn's time has gone
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
    Json rules() const;
    Json change_rules(Simulation& world, const Json& asked);
    Json change_settings(Simulation& world, const Json& asked) const;
    Json settings(Simulation& world) const;
    Json climate(Simulation& world) const;
    Json grass(Simulation& world, bool map) const;
    Json cell(Simulation& world, int x, int y) const;
    Json bodies(Simulation& world, std::size_t limit) const;
    Json seeds(Simulation& world, std::size_t limit) const;
    Json deaths(Simulation& world, int since, const std::string& species, const std::string& cause, std::size_t limit) const;
    Json history(Simulation& world, std::size_t points) const;
    Json insights(Simulation& world) const;
    // Whether calls carry meaning, from each animal's last choice: how often each call is made with a hunter in sight
    // and without; and how hearing one changes what listeners do, fleeing a hunter in sight or moving with none.
    Json calls(Simulation& world) const;
    Json brain(Simulation& world, int identifier, bool weights) const;
    // A value this far from nothing has run away; a neuron this near either end of tanh has jammed there.
    static constexpr double RUNAWAY_VALUE = 50;
    static constexpr double JAMMED = 0.99;
    Json skills(Simulation& world) const;
    Json diary(Simulation& world) const;
    Json lands(Simulation& world) const;  // colour groups, their lands and wars, and the marks of the victor
    Json gifts(Simulation& world) const;  // the gifts lately given, looked over for trade
    Json language(Simulation& world) const;  // what each call goes with and brings about, and whether neighbours, kin and groups share calls
    Json towns(Simulation& world) const;  // how far from the sea shelters, sites and roads stand, against land, animals and plants
    Json opinions(Simulation& world) const;  // whether what animals think of a colour group goes with entering or contesting its land
    Json regard(Simulation& world) const;
    Json society(Simulation& world) const;
    Json minds(Simulation& world) const;
    std::string brain_report_text(Simulation& world) const;
    std::string death_report_text(Simulation& world) const;
    Json timings(Simulation& world) const;
    Json carrying(Simulation& world) const;  // /carrying: what is taken from where, dropped, eaten and spoiled

private:
    struct Herd {
        int id = 0;
        int since = 0;  // the turn it was first seen
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
