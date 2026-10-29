#include "core/Simulation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <execution>
#include <numeric>
#include <cstdio>
#include <fstream>
#include <limits>
#include <set>
#include <type_traits>
#include <random>
#include <stdexcept>
#include <tuple>

#include "core/PyFormat.hpp"

namespace bitzkey {

std::array<std::vector<Shift>, 14> EXPERIENCES = {{
    {{Aggression, 0.04}, {Fear, -0.02}, {Intellect, 0.004}},  // kill
    {{Aggression, 0.0067}},                       // hit
    {{Aggression, -0.0133}, {Fear, 0.0133}},      // stung
    {{Fear, 0.0333}, {Aggression, -0.0067}, {Intellect, -0.002}},  // wounded
    {{Sociability, 0.02}},                        // wounded_alone
    {{Libido, 0.02}, {Sociability, 0.0133}, {Intellect, 0.002}},  // bred
    {{Libido, -0.0533}},                          // bred_into_hunger
    {{Appetite, 0.0033}, {Patience, -0.0033}, {Intellect, -0.0005}},  // starving
    {{Patience, 0.0067}},                         // foraged_meal
    {{Curiosity, 0.01}, {Intellect, 0.002}},      // discovery
    {{Curiosity, -0.02}, {Fear, 0.0133}},         // ambushed
    {{Fear, -0.0133}, {Aggression, 0.0067}, {Intellect, 0.002}},  // defended
    {{Fear, 0.0067}, {Intellect, -0.001}},        // panicked
    {},                                           // lesson: a choice that turned out well, learnt from: it no longer sharpens the mind
}};

namespace {

// Milliseconds since `since`, and `since` moved on to now: for Simulation::timings.
double lap(std::chrono::steady_clock::time_point& since) {
    const auto now = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(now - since).count();
    since = now;
    return ms;
}

}  // namespace

namespace {

// The experiences, in the order of EXPERIENCES.
enum Event { Kill, Hit, Stung, Wounded, WoundedAlone, Bred, BredIntoHunger, Starving, ForagedMeal, Discovery,
             Ambushed, Defended, Panicked, Lesson };

struct Offset {
    int dx, dy;
};

// Every offset within MAX_SIGHT, nearest first (by distance, then dx, then dy); a search mirrors them by its tie_side.
std::vector<Offset> make_sight_offsets() {
    std::vector<Offset> offsets;
    for (int dx = -MAX_SIGHT; dx <= MAX_SIGHT; ++dx) {
        for (int dy = -MAX_SIGHT; dy <= MAX_SIGHT; ++dy) {
            const int distance = std::abs(dx) + std::abs(dy);
            if (0 < distance && distance <= MAX_SIGHT) offsets.push_back({dx, dy});
        }
    }
    std::sort(offsets.begin(), offsets.end(), [](const Offset& a, const Offset& b) {
        return std::make_tuple(std::abs(a.dx) + std::abs(a.dy), a.dx, a.dy) <
               std::make_tuple(std::abs(b.dx) + std::abs(b.dy), b.dx, b.dy);
    });
    return offsets;
}
const std::vector<Offset> SIGHT_OFFSETS = make_sight_offsets();

// The same offsets cut off at each distance; SEEN adds the animal's own cell first.
std::array<std::vector<Offset>, MAX_SIGHT + 1> make_within(bool seen) {
    std::array<std::vector<Offset>, MAX_SIGHT + 1> within;
    for (int reach = 0; reach <= MAX_SIGHT; ++reach) {
        if (seen) within[reach].push_back({0, 0});
        for (const Offset& offset : SIGHT_OFFSETS) {
            if (std::abs(offset.dx) + std::abs(offset.dy) <= reach) within[reach].push_back(offset);
        }
    }
    return within;
}
const auto SIGHT_WITHIN = make_within(false);
const auto SEEN_WITHIN = make_within(true);

// How many of the first sight offsets a seed reaches.
std::int64_t seed_offsets(int reach) { return static_cast<std::int64_t>(SIGHT_WITHIN[static_cast<std::size_t>(reach)].size()); }

// The offsets within `radius` steps, the own cell among them, column by column.
// Where young may be born around their mother, nearest first: the sides, then the corners, then each
// ring further out, up to `reach`, its nearest cells first.
const std::vector<Offset>& birth_offsets(int reach) {
    static const std::array<std::vector<Offset>, 16> all = [] {  // built once, whatever thread asks first
        std::array<std::vector<Offset>, 16> built;
        for (int ring = 0; ring < 16; ++ring) {
            std::vector<Offset>& offsets = built[static_cast<std::size_t>(ring)];
            offsets = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}, {1, -1}, {1, 1}, {-1, 1}, {-1, -1}};
            std::vector<Offset> further;
            for (int dy = -ring; dy <= ring; ++dy) {
                for (int dx = -ring; dx <= ring; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) > 1) further.push_back({dx, dy});
                }
            }
            const auto key = [](const Offset& offset) {
                return std::tuple{std::max(std::abs(offset.dx), std::abs(offset.dy)), offset.dx * offset.dx + offset.dy * offset.dy,
                                  offset.dy, offset.dx};
            };
            std::sort(further.begin(), further.end(), [&](const Offset& a, const Offset& b) { return key(a) < key(b); });
            offsets.insert(offsets.end(), further.begin(), further.end());
        }
        return built;
    }();
    return all.at(static_cast<std::size_t>(reach));
}

const std::vector<Offset>& nearby_offsets(int radius) {
    // As far as anything reaches: the furthest mating call, at the sharpest sight.
    constexpr int REACH = static_cast<int>(MAX_MATING_CALL_RANGE * (2 + SIGHT_PER_TRAIT * TRAIT_MAX)) + 1;
    static const std::array<std::vector<Offset>, REACH> all = [] {  // built once, whatever thread asks first
        std::array<std::vector<Offset>, REACH> built;
        for (int reach = 0; reach < REACH; ++reach) {
            for (int dx = -reach; dx <= reach; ++dx) {
                for (int dy = -(reach - std::abs(dx)); dy <= reach - std::abs(dx); ++dy) built[static_cast<std::size_t>(reach)].push_back({dx, dy});
            }
        }
        return built;
    }();
    return all.at(static_cast<std::size_t>(radius));
}

// Setting off in a direction, from north to west: the step in x and y, and what it then says it did.
// The top and bottom of the map are barriers; east and west wrap round.
// Setting off in a direction, from GoNorth to WalkWest: the step in x and y, whether it is a walk of a
// single cell (else it goes as far as its speed allows), and what it then says it did. The top and
// bottom of the map are barriers; east and west wrap round.
struct Heading {
    int dx, dy;
    bool walk;
    std::string_view went;
};
constexpr std::array<Heading, 8> HEADINGS = {{{0, -1, false, "went north"}, {0, 1, false, "went south"},
                                              {1, 0, false, "went east"}, {-1, 0, false, "went west"},
                                              {0, -1, true, "walked north"}, {0, 1, true, "walked south"},
                                              {1, 0, true, "walked east"}, {-1, 0, true, "walked west"}}};
bool travelled(std::string_view action) {
    return std::any_of(HEADINGS.begin(), HEADINGS.end(), [action](const Heading& heading) { return heading.went == action; });
}

int distance(int x1, int y1, int x2, int y2) { return std::abs(x1 - x2) + std::abs(y1 - y2); }

double closeness(const Bitzkey& agent, std::optional<Cell> cell, int sight) {
    if (!cell) return 0.0;
    return std::min(1.0, 1 - static_cast<double>(distance(cell->x, cell->y, agent.x, agent.y) - 1) / sight);
}

std::optional<Cell> cell_of(const Bitzkey* agent) {
    if (!agent) return std::nullopt;
    return Cell{agent->x, agent->y};
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Climate

int season(int turn) { return turn % YEAR_LENGTH * 4 / YEAR_LENGTH; }

int year(int turn) { return turn / YEAR_LENGTH + 1; }
int month(int turn) { return py_mod(turn, YEAR_LENGTH) * 12 / YEAR_LENGTH + 1; }

int zone_temperature(int y, int height) {
    const double place = (y + 0.5) / height;
    if (place < POLAR_SHARE || place > 1 - POLAR_SHARE) return VeryCold;
    if (place < ARCTIC_SHARE || place > 1 - ARCTIC_SHARE) return Cold;
    return std::abs(place - 0.5) < EQUATOR_SHARE / 2 ? Hot : Medium;
}

bool in_winter(int y, int height, int quarter) {
    const double place = (y + 0.5) / height;
    if (place < POLAR_SHARE || place > 1 - POLAR_SHARE) return false;  // the polar caps are very cold all year
    const bool equator = std::abs(place - 0.5) < EQUATOR_SHARE / 2;
    if (equator && std::abs(place - 0.5) < WINTER_EQUATOR_SHARE / 2) return false;  // the equator's heart stays hot
    return (quarter == 1 && (place < 0.5 || equator)) || (quarter == 3 && (place > 0.5 || equator));
}

int season_temperature(int y, int height, int quarter) {
    return zone_temperature(y, height) + (in_winter(y, height, quarter) ? 1 : 0);
}

std::pair<int, int> zone_rows(int y, int height) {
    const int zone = zone_temperature(y, height);
    int first = y, last = y;
    while (first > 0 && zone_temperature(first - 1, height) == zone) --first;
    while (last < height - 1 && zone_temperature(last + 1, height) == zone) ++last;
    return {first, last};
}

std::pair<int, int> wave_band(int y, int height, int quarter) {
    // Every zone's wave sets off at once, crossing its own zone.
    const bool north = quarter == 1 || quarter == 2;
    const auto [first, last] = zone_rows(y, height);
    const bool inward = quarter == 1 || quarter == 3;
    const int offset = north == inward ? y - first : last - y;
    return {offset / WAVE_BAND, (last - first) / WAVE_BAND + 1};
}

namespace {

double season_into(int turn) {  // the share of its season gone
    return static_cast<double>(turn % YEAR_LENGTH * 4 - season(turn) * YEAR_LENGTH) / YEAR_LENGTH;
}

double turning() { return std::min(0.5, SEASON_TURNING); }  // the wave in and the ebb never overlap

}  // namespace

double season_progress(int turn) {
    if (season(turn) % 2 == 0 || turning() <= 0) return 1.0;  // the mild seasons are settled throughout
    return std::min(1.0, season_into(turn) / turning());
}

double temperature_at(int y, int height, int turn) {
    const int quarter = season(turn);
    const int mild = zone_temperature(y, height);
    if (!in_winter(y, height, quarter)) return mild;
    const int winter = mild + 1;
    const double into = season_into(turn);
    if (turning() <= 0 || (into >= turning() && into < 1 - turning())) return winter;
    // Washing in, from the edge toward the equator, or ebbing back the way it came.
    const bool ebbing = into >= 1 - turning();
    const double progress = ebbing ? (into - (1 - turning())) / turning() : into / turning();
    const auto [band, bands] = wave_band(y, height, ebbing ? (quarter + 1) % 4 : quarter);
    const double reach = progress * (bands + WAVE_FRONT);  // how far the front has come, in bands
    const double turned = std::min(1.0, std::max(0.0, (reach - band) / WAVE_FRONT));
    return ebbing ? winter - turned : mild + turned;
}

// The average animal spends its stamina over its waking half day's ACTIVE_SHARE and gets it back over the rest.
double stamina_pace() {
    return FULL_STAMINA / (TYPICAL_FATIGUE * std::max(1.0, DAY_LENGTH / 2.0 * ACTIVE_SHARE));
}

double rest_recovery() { return FULL_STAMINA / std::max(1.0, DAY_LENGTH / 2.0 * (1 - ACTIVE_SHARE)); }

double dieback_threshold(int x, int y) {
    return static_cast<double>((static_cast<long long>(x) * 7919 + static_cast<long long>(y) * 104729) % 1000) / 1000;
}

// ---------------------------------------------------------------------------------------------
// Bitzkey

Bitzkey::Bitzkey(int identifier_, Dna dna_, int x_, int y_)
    : identifier(identifier_), dna(dna_), x(x_), y(y_), inborn(decode_temperament(dna_)), body(decode_body(dna_)) {
    species = static_cast<Species>(body[SpeciesTrait]);
    climate = decode_climate(dna_);
    mind = to_mind(inborn);
    derive();
}

void Bitzkey::derive() {
    terrain = decode_terrain(dna);
    nocturnal = decode_nocturnal(dna);
    {  // plant or animal: it is its genes' body that counts, feeble or not
        const double shortest = LIFESPAN_YEARS[0], middle = LIFESPAN_YEARS[1], longest = LIFESPAN_YEARS[2];
        const double fastest = METABOLISM[0], slowest = METABOLISM[1];
        const double genetic = fastest + (slowest - fastest) * share(body[Size]);
        const double slowing = (fastest - genetic) / (fastest - slowest);  // 0 at the fastest metabolism, 1 the slowest
        const double years = slowing <= 0.5 ? shortest + (middle - shortest) * (2 * slowing)
                                            : middle + (longest - middle) * (2 * slowing - 1);
        lifespan = years * YEAR_LENGTH;
    }
    feeble = feeble_at_age();
    const double size = physique(Size);
    max_health = (60.0 + 90 * size) * (feeble ? FEEBLE_SHARE : 1.0);
    metabolism = METABOLISM[0] + (METABOLISM[1] - METABOLISM[0]) * size;
    stomach = STOMACH[0] + (STOMACH[1] - STOMACH[0]) * size;
    burn = metabolism * (0.45 + 0.18 * physique(Strength) + 0.24 * physique(Speed) +
                         0.12 * (physique(Weapons) + physique(Armour)));
    if (species != Species::Plant) burn *= 1 + SIZE_HUNGER * size * size;  // a large body is hungry work (Size hunger)
    if (species != Species::Plant) {  // and a cleverer animal's brain burns more (Brain cost)
        const double cost = BRAIN_COST * (species == Species::Predator ? PREDATOR_BRAIN_COST : 1.0);
        burn = burn * (1 + cost * inborn[Intellect] / TRAIT_MAX);
        if (terrain == Terrain::Avian) burn *= 1 + FLIGHT_HUNGER;  // flight is hungry work
    }
    might = (1 - SIZE_IN_MIGHT) * physique(Strength) + SIZE_IN_MIGHT * size;
    toughness = (1 - SIZE_IN_MIGHT) * physique(Armour) + SIZE_IN_MIGHT * size;
    const int base_steps = 1 + trait(Speed) * 3 / TRAIT_MAX;
    steps = species == Species::Grazer ? static_cast<int>(base_steps * GRAZER_SPEED + 0.5) : base_steps;
    grazing = GRAZING[static_cast<std::size_t>(species)];
    browsing = BROWSING[static_cast<std::size_t>(species)];
    sight = 2 + SIGHT_PER_TRAIT * trait(Sight);
    breeding_cost = (BREEDING_COST_MOST - 15 * physique(Fertility)) *
                    (species == Species::Omnivore || species == Species::Predator ? HUNTER_BREEDING_COST
                     : species == Species::Grazer                                  ? GRAZER_BREEDING_COST
                                                                                   : 1.0);  // hunters and grazers pay less
    digestion = 0.2 + 0.8 * physique(Carrion);
}

double Bitzkey::exposure_share(double here) const {
    const int home = climate.temperature;
    if (species == Species::Plant || here == home) return 1.0;
    if (here > home ? climate.cold : climate.heat) return HARDINESS;
    if (here > home ? climate.heat : climate.cold) return FRAILTY;
    return 1.0;
}

Profile Bitzkey::profile() const {
    Profile profile{};
    for (int trait = 0; trait < TEMPERAMENT_COUNT; ++trait) profile[trait] = mind[trait];
    for (int trait = 1; trait < BODY_TRAIT_COUNT; ++trait) profile[TEMPERAMENT_COUNT + trait - 1] = body[trait];
    return profile;
}

static std::uintmax_t brain_bytes(const Brain& brain) {
    std::uintmax_t bytes = sizeof(Brain);
    for (const Layer& layer : brain.layers) bytes += sizeof(Layer) + layer.weights.size() * sizeof(double);
    if (brain.traces) {
        for (const Layer& layer : brain.traces->layers) bytes += sizeof(Layer) + layer.weights.size() * sizeof(double);
    }
    return bytes;
}

std::uintmax_t Bitzkey::brain_bytes() const {
    std::uintmax_t bytes = brain ? bitzkey::brain_bytes(*brain) : 0;
    for (const Young& young : womb) bytes += young.brain ? bitzkey::brain_bytes(*young.brain) : 0;
    return bytes;
}

Brain& Bitzkey::own_brain() {
    if (brain.use_count() > 1) brain = std::make_shared<Brain>(*brain);
    return *brain;
}

int thinking_intellect(int intellect, Species species) {
    return std::min(TRAIT_MAX, static_cast<int>(intellect * (species == Species::Predator ? PREDATOR_INTELLIGENCE : 1.0)));
}

std::pair<int, int> brain_shape(Dna dna) {
    const int intellect = thinking_intellect(decode_temperament(dna)[Intellect], static_cast<Species>(decode_body(dna)[SpeciesTrait]));
    return {depth_for(intellect), width_for(intellect)};
}

Dna evolved(Dna parent, Dna child) {
    constexpr int shift = 32;  // the species trait is the ring's first; the species is it mod 4
    const int before = static_cast<int>((parent.bits >> shift) & 0x3), after = static_cast<int>((child.bits >> shift) & 0x3);
    constexpr int plant = 0, predator = 3;
    if (after == before) return child;
    int species;
    if (before == plant) {
        species = plant;
    } else if (after == predator) {
        species = predator;
    } else {
        species = ((before ^ after) & 0x1) ? before + 1 : before - 1;  // the lower bit a step up, the upper a step down
        if (!(plant < species && species <= predator)) species = before;
    }
    child.bits = (child.bits & ~(std::uint64_t{0x3} << shift)) | (static_cast<std::uint64_t>(species) << shift);
    return child;
}

Dna predator_dna(Dna dna) {
    constexpr int shift = 32;  // the species trait is the ring's first
    const std::uint64_t trait = (dna.bits >> shift) & 0x7;
    dna.bits = (dna.bits & ~(std::uint64_t{0x7} << shift)) | (((trait & 0x4) | 0x3) << shift);
    return dna;
}

int litter_size(std::span<const std::pair<int, int>> parents) {
    const int smallest = LITTER_RANGE[0], largest = LITTER_RANGE[1];
    std::vector<double> tendencies;
    for (const auto& [litter, size] : parents) {
        tendencies.push_back((1 - LITTER_SIZE_WEIGHT) * share(litter) + LITTER_SIZE_WEIGHT * (1 - share(size)));
    }
    const double tendency = py_sum(tendencies) / static_cast<double>(parents.size());
    int litter = smallest + static_cast<int>(tendency * (largest - smallest) + 0.5);
    // The smallest bodies have SMALL_LITTERS times as many, falling evenly to the usual for the largest.
    std::vector<double> sizes;
    for (const auto& [trait, size] : parents) sizes.push_back(share(size));
    const double small = 1 - py_sum(sizes) / static_cast<double>(parents.size());
    litter = static_cast<int>(litter * (1 + (SMALL_LITTERS - 1) * small) + 0.5);
    return litter;
}

// ---------------------------------------------------------------------------------------------
// The world

Simulation::Simulation(int width, int height, int population, std::optional<std::uint64_t> seed)
    : Simulation(Settings{width, height, population, seed}) {}

Simulation::Simulation(Settings settings)
    : width_(settings.width), height_(settings.height), mind_mutation_(settings.mind_mutation / 100),
      body_mutation_(settings.body_mutation / 100) {
    if (settings.year_length < 4) throw std::invalid_argument("A year must be at least 4 turns.");
    YEAR_LENGTH = settings.year_length;
    if (width_ < 10 || height_ < 10) throw std::invalid_argument("World dimensions must both be at least 10 cells.");
    if (settings.population < 0) throw std::invalid_argument("Population cannot be negative.");
    if (USE_GPU) {
        std::string why;
        gpu_ = gpu::BrainEngine::create(&why);
        if (gpu_) {
            gpu_status_ = "thinking on";
            for (const gpu::DeviceInfo& device : gpu_->devices()) gpu_status_ += " " + device.name + ";";
            gpu_status_.pop_back();
        } else {
            gpu_status_ = "thinking on the CPU: " + why;
        }
    }
    if (!(0 <= settings.grass_density && settings.grass_density <= 100)) {
        throw std::invalid_argument("Grass density must be between 0 and 100 percent.");
    }
    if (!(0 <= settings.mind_mutation && settings.mind_mutation <= 100 && 0 <= settings.body_mutation &&
          settings.body_mutation <= 100)) {
        throw std::invalid_argument("Mutation chances must be between 0 and 100 percent.");
    }
    std::uint64_t seed;
    if (settings.seed) {
        seed = *settings.seed;
    } else {
        std::random_device device;
        seed = (static_cast<std::uint64_t>(device()) << 32) | device();
    }
    rng().seed(seed);
    if (!(0 <= settings.ocean && settings.ocean <= 100)) throw std::invalid_argument("The sea must cover between 0 and 100 percent.");
    ocean_ = settings.ocean;
    if (ocean_ > 0) make_sea(ocean_, seed);
    population_cap_ = settings.population_cap;
    const std::size_t area = static_cast<std::size_t>(width_) * height_;
    cells_.resize(area);
    carrion_.resize(area);
    kills_.resize(area);
    grass_.assign(area, 0);
    update_climate();
    const int initial_grass = static_cast<int>(py_round(static_cast<double>(width_ * height_) * settings.grass_density / 100));
    initial_grass_ = initial_grass;
    grass_regrowth_ = initial_grass * GRASS_REGROWTH;
    sow(initial_grass);
    const std::vector<int> sizes = group_sizes(settings.population);
    std::vector<int> species;
    for (std::size_t group = 0; group < sizes.size(); ++group) species.push_back(static_cast<int>(rng().randrange(4)));
    const std::vector<std::vector<int>> homes = plant_homes(species);
    for (std::size_t group = 0; group < sizes.size(); ++group) {
        spawn_group(make_dna(rng(), species[group]), sizes[group], nullptr, std::nullopt, true, homes[group]);
    }
    if (settings.population > 0) spawn_biomes();
    for (const Imported& bitzkey : settings.imported) {
        spawn_group(bitzkey.dna, static_cast<int>(rng().randint(SPAWN_GROUP[0], SPAWN_GROUP[1])), bitzkey.brain,
                    bitzkey.mind);
    }
    take_census();
}

std::vector<Bitzkey*> Simulation::living() const {
    std::vector<Bitzkey*> found;
    found.reserve(agents_.size());
    for (const auto& agent : agents_) {
        if (agent->alive()) found.push_back(agent.get());
    }
    return found;
}

int Simulation::grass_at(int x, int y) const {
    return in_bounds(x, y) ? grass_[static_cast<std::size_t>(y) * width_ + x] : 0;
}

std::size_t Simulation::grass_tufts() const {
    std::size_t total = 0;
    for (const int tufts : grass_) total += static_cast<std::size_t>(tufts);
    return total;
}

std::vector<int> Simulation::group_sizes(int population) {
    const int smallest = SPAWN_GROUP[0], largest = SPAWN_GROUP[1];
    std::vector<int> sizes;
    int remaining = population;
    while (remaining > largest) {
        const int size = static_cast<int>(rng().randint(smallest, std::min(largest, remaining - smallest)));
        sizes.push_back(size);
        remaining -= size;
    }
    if (remaining) sizes.push_back(remaining);
    return sizes;
}

std::vector<std::vector<int>> Simulation::zones() const {
    std::vector<std::vector<int>> found;
    for (int y = 0; y < height_; ++y) {
        if (!found.empty() && zone_temperature(found.back().front(), height_) == zone_temperature(y, height_)) {
            found.back().push_back(y);
        } else {
            found.push_back({y});
        }
    }
    return found;
}

std::vector<std::vector<int>> Simulation::plant_homes(std::vector<int>& species) const {
    const std::vector<std::vector<int>> all = zones();
    std::vector<int> plants, animals;
    for (std::size_t group = 0; group < species.size(); ++group) {
        (species[group] == static_cast<int>(Species::Plant) ? plants : animals).push_back(static_cast<int>(group));
    }
    while (plants.size() < all.size() && animals.size() > 1) {
        const int group = animals.back();
        animals.pop_back();
        species[static_cast<std::size_t>(group)] = static_cast<int>(Species::Plant);
        plants.push_back(group);
    }
    std::sort(plants.begin(), plants.end());
    const int lowest = SPAWN_SPREAD, highest = height_ - SPAWN_SPREAD - 1;  // where a group's centre may be
    std::vector<std::vector<int>> homes(species.size());
    for (std::size_t zone = 0; zone < std::min(plants.size(), all.size()); ++zone) {
        const std::vector<int>& rows = all[zone];
        std::vector<int>& centres = homes[static_cast<std::size_t>(plants[zone])];
        for (const int y : rows) {
            if (lowest <= y && y <= highest) centres.push_back(y);
        }
        if (centres.empty()) centres.push_back(std::min(highest, std::max(lowest, rows[rows.size() / 2])));
    }
    return homes;
}

std::vector<int> Simulation::home_rows(int home) const {
    int nearest = std::numeric_limits<int>::max();
    for (int y = SPAWN_SPREAD; y < height_ - SPAWN_SPREAD; ++y) nearest = std::min(nearest, std::abs(zone_temperature(y, height_) - home));
    std::vector<int> rows;
    for (int y = SPAWN_SPREAD; y < height_ - SPAWN_SPREAD; ++y) {
        if (std::abs(zone_temperature(y, height_) - home) == nearest) rows.push_back(y);
    }
    return rows;
}

void Simulation::make_sea(double ocean, std::uint64_t seed) {
    // Value noise in octaves, each twice as fine and half as strong as the last, wrapping round east and west.
    PyRandom rng;
    rng.seed(seed ^ 0x5EA5EA5EA5EA5EA5ULL);  // its own stream, so the sea leaves the world's draws as they were
    const std::size_t area = static_cast<std::size_t>(width_) * height_;
    std::vector<double> level(area, 0.0);
    const int columns = std::max(2, static_cast<int>(1 / CONTINENT_BREADTH + 0.5));
    const int rows = std::max(1, static_cast<int>(static_cast<double>(columns) * height_ / width_ + 0.5));
    const auto smooth = [](double t) { return t * t * (3 - 2 * t); };
    double strength = 1.0;
    for (int octave = 0; octave < 4; ++octave, strength /= 2) {
        const int across = columns << octave, down = rows << octave;
        std::vector<double> lattice(static_cast<std::size_t>(across) * (down + 1));
        for (double& value : lattice) value = rng.random();
        const auto at = [&](int i, int j) { return lattice[static_cast<std::size_t>(j) * across + static_cast<std::size_t>(i % across)]; };
        for (int y = 0; y < height_; ++y) {
            const double fy = (y + 0.5) / height_ * down;
            const int j = std::min(down - 1, static_cast<int>(fy));
            const double ty = smooth(fy - j);
            for (int x = 0; x < width_; ++x) {
                const double fx = (x + 0.5) / width_ * across;
                const int i = static_cast<int>(fx);
                const double tx = smooth(fx - i);
                const double top = at(i, j) + (at(i + 1, j) - at(i, j)) * tx;
                const double bottom = at(i, j + 1) + (at(i + 1, j + 1) - at(i, j + 1)) * tx;
                level[static_cast<std::size_t>(y) * width_ + x] += strength * (top + (bottom - top) * ty);
            }
        }
    }
    // The lowest squares are sea, as many as the share asks.
    std::vector<std::size_t> order(area);
    for (std::size_t index = 0; index < area; ++index) order[index] = index;
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return std::pair{level[a], a} < std::pair{level[b], b}; });
    sea_.assign(area, 0);
    const auto under = static_cast<std::size_t>(py_round(static_cast<double>(area) * ocean / 100));
    for (std::size_t index = 0; index < std::min(under, area); ++index) sea_[order[index]] = 1;
}

std::vector<std::vector<Cell>> Simulation::biomes() const {
    std::vector<int> band(static_cast<std::size_t>(height_));
    {
        int index = 0;
        for (const std::vector<int>& rows : zones()) {
            for (const int y : rows) band[static_cast<std::size_t>(y)] = index;
            ++index;
        }
    }
    const auto key = [&](int x, int y) { return std::pair{band[static_cast<std::size_t>(y)], sea(x, y)}; };
    std::vector<std::uint8_t> seen(static_cast<std::size_t>(width_) * height_, 0);
    std::vector<std::vector<Cell>> found;
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            if (seen[static_cast<std::size_t>(y) * width_ + x]) continue;
            seen[static_cast<std::size_t>(y) * width_ + x] = 1;
            std::vector<Cell> biome{{x, y}};  // gathered breadth first, from its first square
            for (std::size_t next = 0; next < biome.size(); ++next) {
                const Cell here = biome[next];
                for (const auto [dx, dy] : {std::pair{0, -1}, std::pair{1, 0}, std::pair{0, 1}, std::pair{-1, 0}}) {
                    const int nx = py_mod(here.x + dx, width_), ny = here.y + dy;
                    if (ny < 0 || ny >= height_ || seen[static_cast<std::size_t>(ny) * width_ + nx] || key(nx, ny) != key(x, y)) continue;
                    seen[static_cast<std::size_t>(ny) * width_ + nx] = 1;
                    biome.push_back({nx, ny});
                }
            }
            if (static_cast<int>(biome.size()) >= BIOME_SIZE) found.push_back(std::move(biome));
        }
    }
    return found;
}

void Simulation::spawn_biomes() {
    for (const std::vector<Cell>& biome : biomes()) {
        const bool wet = sea(biome.front().x, biome.front().y);
        const int home = zone_temperature(biome.front().y, height_);
        for (int group = 0; group < BIOME_GROUPS; ++group) {
            Dna dna = make_dna(rng(), static_cast<int>(rng().randrange(4)));
            dna.bits = (dna.bits & ~(std::uint64_t{0x3} << TEMPERATURE_SHIFT)) | static_cast<std::uint64_t>(home) << TEMPERATURE_SHIFT;
            // The first the surface's own; the rest any terrain that may live there.
            static constexpr std::array<Terrain, 3> land = {Terrain::Terrestrial, Terrain::Avian, Terrain::Amphibious};
            static constexpr std::array<Terrain, 3> water = {Terrain::Aquatic, Terrain::Avian, Terrain::Amphibious};
            const auto& fitting = wet ? water : land;
            dna = with_terrain(dna, group == 0 ? fitting[0] : fitting[static_cast<std::size_t>(rng().randrange(3))]);
            spawn_group(dna, static_cast<int>(rng().randint(SPAWN_GROUP[0], SPAWN_GROUP[1])), nullptr, std::nullopt, true, {}, &biome);
        }
    }
}

std::optional<Cell> Simulation::group_centre(Terrain terrain, const std::vector<int>& rows) {
    for (int tries = 0; tries < 1000; ++tries) {
        const int x = static_cast<int>(rng().randrange(SPAWN_SPREAD, width_ - SPAWN_SPREAD));
        const int y = rows.empty() ? static_cast<int>(rng().randrange(SPAWN_SPREAD, height_ - SPAWN_SPREAD))
                                   : rows[static_cast<std::size_t>(rng().randrange(static_cast<std::int64_t>(rows.size())))];
        if (suits(terrain, Cell{x, y})) return Cell{x, y};
    }
    return std::nullopt;
}

void Simulation::spawn_group(Dna dna, int size, const std::shared_ptr<const Brain>& brain,
                             const std::optional<PartialMind>& mind, bool native, const std::vector<int>& given_rows,
                             const std::vector<Cell>* biome) {
    // In one of the rows given, if any; otherwise, with SPAWN_AT_HOME, in its home zone; on land or sea as its terrain
    // allows. A world without sea starts with nothing but terrestrial Bitzkeys.
    if (native && !has_sea()) dna = with_terrain(dna, Terrain::Terrestrial);
    const std::vector<int> rows = !given_rows.empty() || !SPAWN_AT_HOME ? given_rows : home_rows(decode_climate(dna).temperature);
    const Terrain terrain = decode_terrain(dna);
    const std::optional<Cell> centre = biome ? (*biome)[static_cast<std::size_t>(rng().randrange(static_cast<std::int64_t>(biome->size())))]
                                             : group_centre(terrain, rows);
    if (!centre) return;  // nowhere it could live
    const int x = centre->x, y = centre->y;
    if (native) {  // tolerance is rare at the start
        for (const std::uint64_t flag : {HEAT_TOLERANCE, COLD_TOLERANCE}) {
            dna.bits = rng().random() < STARTING_TOLERANCE ? dna.bits | flag : dna.bits & ~flag;
        }
    }
    if (native && decode_body(dna)[SpeciesTrait] == static_cast<int>(Species::Plant)) {
        dna.bits = (dna.bits & ~(std::uint64_t{0x3} << TEMPERATURE_SHIFT)) |
                   static_cast<std::uint64_t>(zone_temperature(y, height_)) << TEMPERATURE_SHIFT;
    }
    const int space = 1 + decode_body(dna)[Size];
    for (int member = 0; member < size; ++member) {
        const int dx = static_cast<int>(rng().randint(-SPAWN_SPREAD, SPAWN_SPREAD));
        const int dy = static_cast<int>(rng().randint(-SPAWN_SPREAD, SPAWN_SPREAD));
        Cell aim{py_mod(x + dx, width_), y + dy};
        if (biome && std::find(biome->begin(), biome->end(), aim) == biome->end()) aim = Cell{x, y};  // kept in its biome
        if (const auto spot = spot_near(aim, space, terrain)) {  // only where there is room
            add_agent(dna, spot->x, spot->y, brain ? std::make_shared<Brain>(*brain) : nullptr, mind);
        }
    }
}

Bitzkey& Simulation::add_agent(Dna dna, int x, int y, std::shared_ptr<Brain> brain, const std::optional<PartialMind>& mind) {
    auto agent = std::make_unique<Bitzkey>(next_identifier_, dna, x, y);
    agent->health = agent->max_health;
    if (mind) {
        for (int trait = 0; trait < TEMPERAMENT_COUNT; ++trait) {
            if (mind->traits[trait]) agent->mind[trait] = *mind->traits[trait];
        }
    }
    if (agent->species != Species::Plant) {  // plants need no brain
        const auto [depth, width] = brain_shape(agent->dna);
        if (!brain) brain = std::make_shared<Brain>(Brain::random(rng(), depth, width));
        else if (brain->depth() != depth || brain->width() != width) brain = std::make_shared<Brain>(brain->resized(depth, width));
        agent->brain = std::move(brain);
        attach(*agent);
    }
    agent->wet = sea(x, y);
    ++next_identifier_;
    if (agent->identifier >= lineage_first_) {
        const auto place = static_cast<std::size_t>(agent->identifier - lineage_first_);
        if (lineage_.size() <= place) lineage_.resize(place + 1);
        lineage_[place] = agent->dna;
    }
    if (in_bounds(x, y)) cell_at(x, y).push_back(agent.get());
    agents_.push_back(std::move(agent));
    return *agents_.back();
}

// ---------------------------------------------------------------------------------------------
// History


std::string_view kept_text(std::string_view text) {
    static std::set<std::string, std::less<>> kept;
    const auto found = kept.find(text);
    return found != kept.end() ? *found : *kept.emplace(text).first;
}

void remove_stale_histories() {
    std::error_code error;
    const auto cutoff = std::filesystem::file_time_type::clock::now() - std::chrono::hours(STALE_HISTORY_HOURS);
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::temp_directory_path(error), error)) {
        const std::string name = entry.path().filename().string();
        if (!name.starts_with(HISTORY_PREFIX) || !entry.is_directory(error)) continue;
        if (entry.last_write_time(error) < cutoff && !error) std::filesystem::remove_all(entry.path(), error);
    }
}

std::vector<Bitzkey*> Simulation::deploy(const std::vector<Imported>& bitzkeys, int x, int y) {
    std::vector<Bitzkey*> deployed;
    for (const Imported& bitzkey : bitzkeys) {
        const int space = 1 + decode_body(bitzkey.dna)[Size];
        for (int member = 0; member < DEPLOY_GROUP; ++member) {
            const int cell_x = x + static_cast<int>(rng().randint(-SPAWN_SPREAD, SPAWN_SPREAD));
            const int cell_y = y + static_cast<int>(rng().randint(-SPAWN_SPREAD, SPAWN_SPREAD));
            if (const auto spot = spot_near(Cell{cell_x, cell_y}, space, decode_terrain(bitzkey.dna))) {  // only where there is room
                deployed.push_back(&add_agent(bitzkey.dna, spot->x, spot->y, bitzkey.brain ? std::make_shared<Brain>(*bitzkey.brain) : nullptr,
                                              bitzkey.mind));
            }
        }
    }
    census_.forget_from(turn_);  // this turn again, as it now is
    take_census();
    return deployed;
}

void Simulation::rules_changed() {
    grass_regrowth_ = initial_grass_ * GRASS_REGROWTH;
    band_of_row_.clear();  // the bands are worked out again as the cells are next indexed
    update_climate();
    for (auto& agent : agents_) agent->derive();
}

void Simulation::advance() {
    if (end_reason() != EndReason::None) return;
    take_turn();
    if (on_turn) on_turn(*this);
}

EndReason Simulation::end_reason() const {
    const auto alive = living();
    if (std::none_of(alive.begin(), alive.end(), [](const Bitzkey* agent) { return agent->species != Species::Plant; })) {
        return EndReason::Extinct;
    }
    std::vector<std::uint8_t> taken(static_cast<std::size_t>(width_) * height_, 0);
    std::size_t occupied = 0;
    for (const Bitzkey* agent : alive) {
        if (!in_bounds(agent->x, agent->y)) {
            ++occupied;
            continue;
        }
        auto& cell = taken[static_cast<std::size_t>(agent->y) * width_ + agent->x];
        if (!cell) {
            cell = 1;
            ++occupied;
        }
    }
    return occupied >= static_cast<std::size_t>(width_) * height_ ? EndReason::Full : EndReason::None;
}

// ---------------------------------------------------------------------------------------------
// A turn

std::string_view Simulation::exposure(const Bitzkey& agent, double here) {
    return here > agent.climate.temperature ? "froze" : "overheated";
}

void Simulation::update_climate() {
    // How well grass grows in each row: fully where it is medium, less the further it has turned from
    // medium, and not at all a step away.
    climate_.resize(static_cast<std::size_t>(height_));
    growth_.resize(static_cast<std::size_t>(height_));
    grass_rows_.clear();
    grass_reach_.clear();
    double reach = 0.0;
    for (int y = 0; y < height_; ++y) {
        const double temperature = temperature_at(y, height_, turn_);
        const double growth = std::max(0.0, 1 - std::abs(temperature - static_cast<int>(Medium)));
        climate_[static_cast<std::size_t>(y)] = temperature;
        growth_[static_cast<std::size_t>(y)] = growth;
        if (growth > 0) {
            grass_rows_.push_back(y);
            reach += growth;
            grass_reach_.push_back(reach);
        }
    }
}

void Simulation::take_turn() {
    auto clock = std::chrono::steady_clock::now();
    ++turn_;
    update_climate();
    index_cells();
    for (const auto& agent : agents_) {  // a body some cost starved, its death not yet recorded: recorded now
        agent->trail.clear();             // last turn's way is forgotten
        if (!agent->died_turn && !agent->alive()) die(*agent, agent->hunger >= 100 ? "starved" : "killed");
    }
    std::vector<Bitzkey*> order = living();
    std::vector<Bitzkey*> thinkers;
    plants_ = static_cast<int>(std::count_if(order.begin(), order.end(),
                                             [](const Bitzkey* agent) { return agent->species == Species::Plant; }));
    population_ = static_cast<int>(order.size()) - plants_;
    rng().shuffle(order);
    // What each needs that no other changes this turn's upkeep, on every core at once: whether its season to breed has
    // come, and the living parents near enough to shelter it (positions stay put until the animals act, so only
    // whether they are still alive when its turn comes is left to see).
    struct Ahead {
        std::array<const Bitzkey*, 2> shelter{};
        bool in_heat = false;
    };
    std::vector<Ahead> ahead(order.size());
    std::vector<std::size_t> places(order.size());
    std::iota(places.begin(), places.end(), std::size_t{0});
    std::for_each(std::execution::par, places.begin(), places.end(), [&](std::size_t place) {
        const Bitzkey& agent = *order[place];
        if (agent.species == Species::Plant) return;
        ahead[place].in_heat = heat(agent);
        std::size_t kept = 0;
        for (const int parent : {agent.mother, agent.father}) {
            const Bitzkey* found = parent >= 0 ? find_agent(parent) : nullptr;
            if (found && found->alive() && distance(found->x, found->y, agent.x, agent.y) <= SHELTER_REACH) ahead[place].shelter[kept++] = found;
        }
    });
    for (std::size_t place = 0; place < order.size(); ++place) {
        Bitzkey* agent = order[place];
        if (!agent->alive()) continue;
        const double full = agent->max_health, share_before = std::max(0.0, agent->health) / full;
        agent->age += 1;
        agent->grow();
        agent->health = std::min(agent->health, agent->max_health);  // growing old, it grows feeble
        if (agent->max_health != full) {  // grown out of infancy, or into old age: the change in its health's share is no lesson
            agent->last_wellbeing += std::max(0.0, agent->health) / agent->max_health - share_before;
        }
        const bool plant = agent->species == Species::Plant;
        const double here = in_bounds(agent->x, agent->y) ? climate_[static_cast<std::size_t>(agent->y)] : Medium;
        const auto& shelter = ahead[place].shelter;
        const bool sheltered = agent->infant() && std::any_of(shelter.begin(), shelter.end(), [](const Bitzkey* parent) {
                                   return parent && parent->alive();
                               });
        const double steps = sheltered ? 0.0 : agent->climate_steps(here);  // a sheltered infant feels no weather
        const bool hungrier = steps > 0 && (!plant || here < agent->climate.temperature);  // out of its temperature; a plant, too hot
        const double share = agent->exposure_share(here);  // less for the hardy
        // The extra food it needs rises with the weather's turning, to the full amount a step away.
        const double burn = agent->burn * (hungrier ? 1 + (CLIMATE_HUNGER - 1) * share * std::min(1.0, steps) : 1.0);
        agent->hunger = std::min(100.0, agent->hunger + agent->digest(burn));  // meat lasts; hunger stops at full
        if (agent->cooldown) agent->cooldown -= 1;
        if (agent->health < agent->max_health && agent->hunger < 65) agent->heal(0.005);
        // A tenth of its health a step, part steps in part; a plant takes one step in its stride, and is
        // hurt more and more as the weather turns toward a second, to full harm two away.
        const double felt = plant ? std::max(0.0, std::min(steps, 2 * (steps - 1))) : steps;
        // An animal endures the weather awhile, uneasy but unharmed, the more resilient the longer, and twice as
        // long weather it is hardy to.
        const int home = agent->climate.temperature;
        const bool hardy = here != home && (here > home ? agent->climate.cold : agent->climate.heat);
        const double grace = plant ? 0.0 : WEATHER_GRACE * DAY_LENGTH * (0.5 + agent->feeling(Resilience)) * (hardy ? 2 : 1);
        agent->weathered = felt > 0 ? agent->weathered + 1 : std::max(0.0, agent->weathered - 1);
        // Choosing to sit the weather out hurts the more (Resting exposure); resting asleep or spent was no choice.
        const double resting = agent->has_last_senses && agent->last_choice == Rest && !agent->last_forced ? RESTING_EXPOSURE : 1.0;
        if (felt > 0 && agent->weathered > grace) agent->health -= CLIMATE_DAMAGE * felt * share * resting * agent->max_health;
        const bool starving = agent->hunger >= 100;
        if (starving) agent->health -= STARVING_DAMAGE * (1 + SIZE_STARVING * agent->physique(Size)) * agent->max_health;  // it wastes away, the larger the faster
        if (agent->health <= 0) {  // starving, and hungrier out of its temperature, it died of exposure
            die(*agent, starving && !hungrier ? "starved" : exposure(*agent, here));
            continue;
        }
        if (dies_of_age(*agent)) {
            die(*agent, "old age");
            continue;
        }
        if (plant) {
            agent->hunger = std::max(0.0, agent->hunger - 1.5);
            if (steps >= 0.5) {  // nearer another temperature than its own, a plant neither grows nor seeds
                agent->last_action = "withering";
                continue;
            }
            agent->health = std::min(agent->max_health, agent->health + agent->max_health * PLANT_REGROWTH);
            agent->last_action = "photosynthesized";
            seed_plant(*agent);
            continue;
        }
        agent->in_heat = ahead[place].in_heat;
        if (agent->due_turn && *agent->due_turn <= turn_) give_birth(*agent);
        agent->seen_success *= SOCIAL_MEMORY;
        agent->seen_failure *= std::pow(SOCIAL_MEMORY, 1 + RESILIENCE * (2 * agent->feeling(Resilience) - 1));  // got over sooner
        settle(*agent);
        nurse(*agent);
        if (agent->hunger >= 80) experience(*agent, Starving);
        thinkers.push_back(agent);
    }
    timings.upkeep += lap(clock);
    think_all(thinkers);
    clock = std::chrono::steady_clock::now();

    germinate();
    wither_grass();
    const double whole = std::floor(grass_regrowth_);
    const double fraction = grass_regrowth_ - whole;
    sow(static_cast<int>(whole) + ((fraction != 0.0 && rng().random() < fraction) ? 1 : 0));
    for (auto& agent : agents_) {
        // no longer fresh by next turn: it rots
        if (agent->died_turn && turn_ - *agent->died_turn >= FRESH_KILL_TURNS) agent->meat = std::max(0.0, agent->meat - agent->rot);
    }
    for (const auto& agent : agents_) {  // bodies rotted away, not eaten to the last portion, may feed a new plant
        if (agent->died_turn && turn_ - *agent->died_turn >= CORPSE_TURNS && agent->species != Species::Plant && agent->portions > 0 &&
            CORPSE_PLANT && rng().random() < CORPSE_PLANT) {
            seed_corpse(*agent);
        }
    }
    for (auto& agent : agents_) {
        if (agent->died_turn && turn_ - *agent->died_turn >= CORPSE_TURNS) departed_.push_back(std::move(agent));
    }
    std::erase_if(agents_, [](const std::unique_ptr<Bitzkey>& agent) { return !agent; });
    identifiers_.clear();  // out of step with them now
    insights_.decay();
    take_census();
    free_the_dead();
    timings.after += lap(clock);
}

void Simulation::take_census() {
    std::vector<int> identifiers;
    for (const Bitzkey* agent : living()) identifiers.push_back(agent->identifier);
    if (census_.record(turn_, std::move(identifiers))) forget_before(census_.snapshots().front().turn);
    forget_old_deaths();
}

void Simulation::forget_old_deaths() {
    const int start = turn_ - RECENT_DEATH_TURNS;
    while (!death_order_.empty() && death_order_.front().first < start) {  // oldest first, so only those forgotten are looked at
        const auto [turn, identifier] = death_order_.front();
        death_order_.pop_front();
        forget_death(turn, identifier);
    }
    if (turn_ % KILLERS_LET_GO == 0) {
        death_tally_.let_go([this](int identifier) {
            const Bitzkey* killer = find_agent(identifier);
            return killer && killer->alive();
        });
    }
}

void Simulation::forget_before(int start) {
    while (!death_order_.empty() && death_order_.front().first < start) {  // oldest first, so only those forgotten are looked at
        const auto [turn, identifier] = death_order_.front();
        death_order_.pop_front();
        forget_death(turn, identifier);
    }
    int first = next_identifier_;
    for (const int identifier : census_.snapshots().front().identifiers) first = std::min(first, identifier);
    // Any kept death before the graph's start was alive then, and in its first snapshot, unless it died that
    // very turn; any born since has a later identifier.
    for (const auto& [turn, identifier] : death_order_) {
        if (turn > start) break;
        first = std::min(first, identifier);
    }
    for (const auto& agent : agents_) first = std::min(first, agent->identifier);
    if (first <= lineage_first_) return;
    const std::size_t gone = std::min(lineage_.size(), static_cast<std::size_t>(first - lineage_first_));
    lineage_.erase(lineage_.begin(), lineage_.begin() + static_cast<std::ptrdiff_t>(gone));
    lineage_first_ = first;
}

void Simulation::forget_death(int turn, int identifier) {
    const auto found = death_records_.find(identifier);
    if (found != death_records_.end() && found->second.turn == turn) death_records_.erase(found);
}

void Simulation::order_deaths() {
    std::vector<std::pair<int, int>> order;
    order.reserve(death_records_.size());
    for (const auto& [identifier, death] : death_records_) order.emplace_back(death.turn, identifier);
    std::sort(order.begin(), order.end());
    death_order_.assign(order.begin(), order.end());
}

void Simulation::index_bands() {
    // The band each row lies in, and for each home temperature and row, the home band nearest: of the
    // bands whose usual temperature is nearest home, the one nearest the row, the upper on a tie.
    const std::vector<std::vector<int>> bands = zones();
    band_of_row_.clear();
    std::vector<int> temperatures;
    for (std::size_t band = 0; band < bands.size(); ++band) {
        for (std::size_t row = 0; row < bands[band].size(); ++row) band_of_row_.push_back(static_cast<int>(band));
        temperatures.push_back(zone_temperature(bands[band].front(), height_));
    }
    home_band_.assign(TEMPERATURES.size(), std::vector<int>(static_cast<std::size_t>(height_), 0));
    for (int home = 0; home < static_cast<int>(TEMPERATURES.size()); ++home) {
        int nearest = std::numeric_limits<int>::max();
        for (const int temperature : temperatures) nearest = std::min(nearest, std::abs(temperature - home));
        for (int y = 0; y < height_; ++y) {
            int best = -1, best_distance = 0;
            for (std::size_t band = 0; band < bands.size(); ++band) {
                if (std::abs(temperatures[band] - home) != nearest) continue;
                const int distance = std::max({0, bands[band].front() - y, y - bands[band].back()});
                if (best < 0 || distance < best_distance) best = static_cast<int>(band), best_distance = distance;
            }
            home_band_[static_cast<std::size_t>(home)][static_cast<std::size_t>(y)] = best;
        }
    }
}

void Simulation::index_cells() {
    identifiers_.clear();
    for (const auto& agent : agents_) identifiers_.push_back(agent->identifier);
    for (auto& cell : cells_) cell.clear();
    for (auto& cell : carrion_) cell.clear();
    carrion_count_ = 0;
    for (auto& cell : kills_) cell.clear();
    kill_count_ = 0;
    if (band_of_row_.size() != static_cast<std::size_t>(height_)) index_bands();
    const std::size_t band_count = band_of_row_.empty() ? 0 : static_cast<std::size_t>(band_of_row_.back()) + 1;
    band_plants_.assign(band_count, 0);
    band_grazers_.assign(band_count, 0);
    band_prey_.assign(band_count, 0);
    band_predators_.assign(band_count, 0);
    band_omnivores_.assign(band_count, 0);
    band_omnivore_prey_.assign(band_count, 0);
    for (const auto& agent : agents_) {
        if (!in_bounds(agent->x, agent->y)) continue;
        agent->wet = sea(agent->x, agent->y);
        if (agent->alive()) {
            cell_at(agent->x, agent->y).push_back(agent.get());
            if (agent->species == Species::Plant) {
                ++band_plants_[static_cast<std::size_t>(band_of_row_[static_cast<std::size_t>(agent->y)])];
            } else {
                const auto home = static_cast<std::size_t>(home_band_[static_cast<std::size_t>(agent->climate.temperature)][static_cast<std::size_t>(agent->y)]);
                if (agent->species == Species::Grazer) ++band_grazers_[home];
                else if (agent->species == Species::Predator) ++band_predators_[home];
                else ++band_omnivores_[home];
                const auto here = static_cast<std::size_t>(band_of_row_[static_cast<std::size_t>(agent->y)]);
                if (agent->species == Species::Grazer || agent->species == Species::Omnivore) ++band_prey_[here];  // what predators live on
                if (agent->species == Species::Grazer || agent->species == Species::Predator) ++band_omnivore_prey_[here];  // what omnivores hunt
            }
        } else if (fresh(*agent)) {
            if (agent->species != Species::Plant) {
                kills_[static_cast<std::size_t>(agent->y) * width_ + agent->x].push_back(agent.get());
                ++kill_count_;
            }
        } else if (agent->meat > 0) {
            carrion_[static_cast<std::size_t>(agent->y) * width_ + agent->x].push_back(agent.get());
            ++carrion_count_;
        }
    }
    living_.fill(0);
    for (const auto& agent : agents_) {
        if (agent->alive()) ++living_[static_cast<std::size_t>(agent->species)];
    }
    const int predators = living_[static_cast<std::size_t>(Species::Predator)];
    double glut = PREDATOR_GLUT > 0 ? std::max(0.0, static_cast<double>(predators - PREDATOR_GLUT) / PREDATOR_GLUT) : 0.0;
    const double famine_line = 2.0 * RARE_PREY / 100 * predators;  // and prey dying out: from twice the rare line, other kinds by it, its own by none
    const int prey = living_[static_cast<std::size_t>(Species::Grazer)] + living_[static_cast<std::size_t>(Species::Omnivore)];
    if (famine_line > 0) glut = std::max(glut, std::min(2.0, 2.0 * (famine_line - prey) / famine_line));
    // Each its own, on every core at once: a scarce species breeds more cheaply, a glut of predators turns them on
    // one another, and a predator's infant is guarded while a grown one of its kind is close by.
    std::for_each(std::execution::par, agents_.begin(), agents_.end(), [&](const std::unique_ptr<Bitzkey>& agent) {
        if (!agent->alive() || agent->species == Species::Plant) return;
        agent->population = living_[static_cast<std::size_t>(agent->species)];
        agent->cost_share = scarcity_share(living_[static_cast<std::size_t>(agent->species)], agent->species);
        if (agent->species != Species::Predator) return;
        agent->glut = glut;
        agent->guarded = false;
        if (!agent->infant()) return;
        for (const Bitzkey* other : around(*agent, NURSING_REACH)) {
            if (other->species == Species::Predator && !other->infant() && same_kind(*agent, *other)) {
                agent->guarded = true;
                break;
            }
        }
    });
    for (auto& body : departed_) dead_bodies_.push_back(std::move(body));
    departed_.clear();
}

void Simulation::free_the_dead() {
    if (freeing_.valid()) freeing_.wait();  // the last turn's, done first
    if (dead_brains_.empty() && dead_bodies_.empty()) return;
    freeing_ = std::async(std::launch::async, [brains = std::move(dead_brains_), bodies = std::move(dead_bodies_)]() mutable {
        bodies.clear();
        brains.clear();
    });
    dead_brains_.clear();
    dead_bodies_.clear();
}

double Simulation::home_prey(const Bitzkey& predator) const {
    if (band_prey_.empty()) return 0.0;  // none before the cells are first indexed
    const auto band = static_cast<std::size_t>(home_band_[static_cast<std::size_t>(predator.climate.temperature)][static_cast<std::size_t>(predator.y)]);
    return static_cast<double>(band_prey_[band]) / std::max(1, band_predators_[band]);
}

bool Simulation::may_breed(const Bitzkey& agent, const Bitzkey& partner) const {
    return agent.can_breed() && partner.can_breed() && room() > 0 &&
           (!CROWDING_LIMIT || crowding(agent, partner, true) < CROWDING_LIMIT) &&  // its own kind crowds it
           (!food_needed(agent) || food_supply(agent) >= food_needed(agent)) &&
           well_fed(agent) && !prey_rare(agent);
}

bool Simulation::prey_rare(const Bitzkey& agent) const {
    const auto count = [&](Species species) { return living_[static_cast<std::size_t>(species)]; };
    const double line = static_cast<double>(RARE_PREY) / 100 * agent.population;
    if (agent.species == Species::Predator) return count(Species::Grazer) + count(Species::Omnivore) < line;
    if (agent.species == Species::Omnivore) return count(Species::Grazer) + count(Species::Predator) < line;
    return false;
}

double Simulation::home_food(const Bitzkey& agent) const {
    if (agent.species == Species::Grazer) return home_plants(agent);
    return agent.species == Species::Predator ? home_prey(agent) : 0.0;
}

double Simulation::home_food_needed(const Bitzkey& agent) {
    return agent.species == Species::Grazer ? PLANTS_PER_GRAZER : agent.species == Species::Predator ? PREY_PER_PREDATOR : 0.0;
}

double Simulation::home_food_sense(const Bitzkey& agent) const { return home_fed(agent); }

double Simulation::home_fed(const Bitzkey& agent) const {
    if (agent.species == Species::Omnivore) {
        if (band_omnivores_.empty()) return 0.0;  // none before the cells are first indexed
        const auto band = static_cast<std::size_t>(home_band_[static_cast<std::size_t>(agent.climate.temperature)][static_cast<std::size_t>(agent.y)]);
        const double mouths = std::max(1, band_omnivores_[band]);
        return 0.5 * fed_share(band_plants_[band] / mouths, PLANTS_PER_GRAZER) +
               0.5 * fed_share(band_omnivore_prey_[band] / mouths, PREY_PER_PREDATOR);
    }
    if (agent.species == Species::Plant) return 0.0;
    return fed_share(home_food(agent), home_food_needed(agent), full_litter(agent));
}

double Simulation::home_plants(const Bitzkey& grazer) const {
    if (band_plants_.empty()) return 0.0;  // none before the cells are first indexed
    const auto band = static_cast<std::size_t>(home_band_[static_cast<std::size_t>(grazer.climate.temperature)][static_cast<std::size_t>(grazer.y)]);
    return static_cast<double>(band_plants_[band]) / std::max(1, band_grazers_[band]);
}

bool Simulation::heat(const Bitzkey& agent) const {
    if (agent.species == Species::Plant || !in_bounds(agent.x, agent.y)) return true;
    // Turns until the next winter where it stands: of its hemisphere, or the nearer of the two on the equator.
    const int day = turn_ % YEAR_LENGTH;
    std::optional<int> until;
    for (const int quarter : {1, 3}) {
        if (!in_winter(agent.y, height_, quarter)) continue;
        const int start = quarter * YEAR_LENGTH / 4;
        const int turns = ((start - day) % YEAR_LENGTH + YEAR_LENGTH) % YEAR_LENGTH;
        if (!until || turns < *until) until = turns;
    }
    if (!until) return true;  // no winter ever comes here
    const auto species = static_cast<std::size_t>(agent.species);
    const double end = HEAT_CLOSEST[species] + HEAT_FORESIGHT * agent.feeling(Patience);
    const double begin = std::min(end + HEAT_LENGTH[species], 0.75);  // never through its own winter, a quarter of the year
    return *until > end * YEAR_LENGTH && *until <= begin * YEAR_LENGTH;
}

int call_reach(const Bitzkey& caller) {
    return std::min(MAX_CALL_REACH, std::max(1, static_cast<int>(py_round(CALL_REACH * (0.5 + caller.physique(Size))))));
}

int gestation(int size) {
    return GESTATION[0] + static_cast<int>(static_cast<double>((GESTATION[1] - GESTATION[0]) * size) / TRAIT_MAX + 0.5);
}

void Simulation::seed_plant(Bitzkey& plant) {
    if (plant.hunger > SEED_HUNGER) return;
    // Plants compete for light: the more plants grow within LIGHT_RADIUS, the less likely a plant
    // is to seed, and with LIGHT_LIMIT it doesn't.
    const double least = SEED_CHANCE[0], most = SEED_CHANCE[1];
    const double roll = rng().random();
    const double chance = least + (most - least) * plant.physique(Fertility);
    if (roll >= chance) return;
    int shade = 0;
    for (const Bitzkey* other : around(plant, LIGHT_RADIUS)) shade += other->species == Species::Plant;
    if (roll >= chance * std::max(0.0, 1 - static_cast<double>(shade) / LIGHT_LIMIT)) return;
    const int reach = plant.terrain == Terrain::Avian ? std::min(MAX_SIGHT, static_cast<int>(SEED_REACH * AVIAN_SEED_REACH)) : SEED_REACH;
    const Offset offset = SIGHT_OFFSETS[static_cast<std::size_t>(rng().randrange(seed_offsets(reach)))];
    if (plant.y + offset.dy < 0 || plant.y + offset.dy >= height_) return;  // off the top or bottom of the map: the seed is lost
    const Cell cell{py_mod(plant.x + offset.dx, width_), plant.y + offset.dy};
    if (!room_for_seedling(cell)) return;
    const Dna dna = evolved(plant.dna, cross(plant.dna, plant.dna, rng(), std::nullopt, mind_mutation_, body_mutation_, has_sea()));
    if (!suits(decode_terrain(dna), cell)) return;  // fallen in the sea, or on the land, where it can't grow
    plant.hunger += SEED_COST;
    plant.children += 1;
    plant.last_action = "seeded";
    if (!plant.alive()) die(plant, "seeded to death");  // it spent the last of its food on the seed
    seeds_.push_back({cell.x, cell.y, dna, plant.generation + 1, turn_});  // it lies PLANT_GESTATION turns before it can sprout
}

void Simulation::nurse(Bitzkey& agent) {
    if (agent.species == Species::Plant || agent.infant() || !agent.alive()) return;
    Bitzkey* infant = nullptr;
    for (const Offset& offset : nearby_offsets(NURSING_REACH)) {  // as nearby() would list them, in the same order
        const auto* list = cell_if(agent.x + offset.dx, agent.y + offset.dy);
        if (!list) continue;
        for (Bitzkey* other : *list) {
            if (other != &agent && other->alive() && other->species == agent.species && other->infant() && other->hunger > 0 &&
                (!infant || other->hunger > infant->hunger)) {
                infant = other;
            }
        }
    }
    if (!infant) return;
    const double fed = std::min(infant->hunger, infant->burn);
    const double cost = fed * infant->stomach / agent.stomach;
    if (agent.hunger + cost >= NURSING_LIMIT) return;
    agent.hunger += cost;
    infant->hunger -= fed;
}

void Simulation::seed_corpse(const Bitzkey& corpse) {
    // A random plant genome, at home in the temperature where the body lay, and in its sea or on its land; it sprouts
    // as any seed does.
    Dna dna = make_dna(rng(), static_cast<int>(Species::Plant));
    dna.bits = (dna.bits & ~(std::uint64_t{0x3} << TEMPERATURE_SHIFT)) |
               static_cast<std::uint64_t>(zone_temperature(corpse.y, height_)) << TEMPERATURE_SHIFT;
    dna = with_terrain(dna, sea(corpse.x, corpse.y) ? Terrain::Aquatic : Terrain::Terrestrial);
    seeds_.push_back({corpse.x, corpse.y, dna, 0, turn_});
}

void Simulation::wither_grass() {
    // Grass dies back where it isn't medium: a tuft a cell every GRASS_DIEBACK turns, the cells
    // taking their turns in step. While a row turns away from medium, only some of its cells die back,
    // more as it turns further: each cell whose fixed threshold its shortfall passes.
    for (int y = 0; y < height_; ++y) {
        const double shortfall = 1 - growth_[static_cast<std::size_t>(y)];
        if (shortfall <= 0) continue;
        for (int x = 0; x < width_; ++x) {
            int& tufts = grass_[static_cast<std::size_t>(y) * width_ + x];
            if (tufts > 0 && shortfall > dieback_threshold(x, y) && (x + y * width_ + turn_) % GRASS_DIEBACK == 0) tufts -= 1;
        }
    }
}

bool Simulation::room_for_seedling(Cell cell) const {
    for (const Bitzkey* other : cells_[static_cast<std::size_t>(cell.y) * width_ + cell.x]) {
        if (other->alive()) return false;
    }
    int crowding = 0;  // the living plants within two squares, counted until there are too many
    for (const Offset& offset : nearby_offsets(2)) {
        const auto* list = cell_if(cell.x + offset.dx, cell.y + offset.dy);
        if (!list) continue;
        for (const Bitzkey* other : *list) {
            if (other->alive() && other->species == Species::Plant && ++crowding > PLANT_CROWDING) return false;
        }
    }
    return true;
}

void Simulation::sprout(Dna dna, Cell cell, int generation) {
    Bitzkey& seedling = add_agent(dna, cell.x, cell.y);
    seedling.hunger = SEEDLING_HUNGER;
    seedling.generation = generation;
    plants_ += 1;
}

void Simulation::germinate() {
    // Seeds sprout once they have lain PLANT_GESTATION turns, their cell is at their temperature and
    // there is room, unless they have waited SEED_VIABILITY of a year, when they are dead.
    const double viable = SEED_VIABILITY * YEAR_LENGTH;
    // What becomes of each seed, decided on every core at once as the cells now stand; then, in their order, the
    // seedlings sprout. A seedling takes room as it sprouts, so one found room beforehand is looked at again; one
    // found none has none still, as sprouting only ever crowds a cell.
    enum Fate : std::uint8_t { Waits, Dies, Sprouts };
    std::vector<Fate> fates(seeds_.size());
    std::vector<std::size_t> indices(seeds_.size());
    std::iota(indices.begin(), indices.end(), std::size_t{0});
    std::for_each(std::execution::par, indices.begin(), indices.end(), [&](std::size_t index) {
        const Seed& seed = seeds_[index];
        if (turn_ - seed.sown >= viable) fates[index] = Dies;
        else if (turn_ - seed.sown >= PLANT_GESTATION &&
                 std::abs(climate_[static_cast<std::size_t>(seed.y)] - decode_climate(seed.dna).temperature) < 0.5 &&
                 suits(decode_terrain(seed.dna), {seed.x, seed.y}) && room_for_seedling({seed.x, seed.y})) {
            fates[index] = Sprouts;
        }
    });
    std::size_t kept = 0;  // those still waiting, kept in their order at the front
    for (std::size_t index = 0; index < seeds_.size(); ++index) {
        const Seed seed = seeds_[index];
        if (fates[index] == Sprouts && room_for_seedling({seed.x, seed.y})) sprout(seed.dna, {seed.x, seed.y}, seed.generation);
        else if (fates[index] != Dies) seeds_[kept++] = seed;
    }
    seeds_.resize(kept);
}

void Simulation::sow(int tufts) {
    // Grass grows only where it is medium, and less in a row turning to or from medium, by how medium
    // it is; on and around a corpse, it grows CORPSE_FERTILITY times as fast.
    if (grass_rows_.empty()) return;
    std::vector<std::uint8_t> fertile;
    for (const auto& corpse : agents_) {
        if (!corpse->died_turn) continue;
        if (fertile.empty()) fertile.assign(static_cast<std::size_t>(width_) * height_, 0);
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                if (corpse->y + dy < 0 || corpse->y + dy >= height_) continue;  // no further than the top or bottom
                fertile[static_cast<std::size_t>(corpse->y + dy) * width_ + py_mod(corpse->x + dx, width_)] = 1;
            }
        }
    }
    for (int tuft = 0; tuft < tufts; ++tuft) {
        const int x = static_cast<int>(rng().randrange(width_));
        const auto reached = std::upper_bound(grass_reach_.begin(), grass_reach_.end(), rng().random() * grass_reach_.back());
        const std::size_t row = std::min(static_cast<std::size_t>(reached - grass_reach_.begin()), grass_rows_.size() - 1);
        const int y = grass_rows_[row];
        const std::size_t at = static_cast<std::size_t>(y) * width_ + x;
        grass_[at] += (!fertile.empty() && fertile[at]) ? CORPSE_FERTILITY : 1;
    }
}

// ---------------------------------------------------------------------------------------------
// Temperament, decisions and learning

void Simulation::experience(Bitzkey& agent, int event) {
    // Intellect sets how far, more for the moody and less for the steady. The resilient take fright less, and
    // the fragile more.
    const double plasticity = (0.2 + 0.8 * agent.intelligence()) * (1 + MOODINESS * (2 * agent.feeling(Moodiness) - 1));
    for (const Shift& shift : EXPERIENCES[static_cast<std::size_t>(event)]) {
        double change = shift.change;
        if (shift.trait == Fear && change > 0) change *= 1 + RESILIENCE * (1 - 2 * agent.feeling(Resilience));
        agent.mind[shift.trait] = std::max(0.0, std::min(static_cast<double>(TRAIT_MAX),
                                                         agent.mind[shift.trait] + change * TRAIT_MAX * plasticity));
    }
}

void Simulation::settle(Bitzkey& agent) {
    for (int trait = 0; trait < TEMPERAMENT_COUNT; ++trait) {
        agent.mind[trait] += (agent.inborn[trait] - agent.mind[trait]) * TEMPERAMENT_RELAPSE;
    }
}

static double learning_rate(const Bitzkey& agent) {
    // How fast its brain learns: LEARNING_FLOOR at the least, and more with its intellect, boosted further the cleverer
    // it is; a predator's PREDATOR_LEARNING times as fast, an omnivore's with half that bonus.
    const double sharpness = agent.intelligence();
    const double hunter = agent.species == Species::Predator  ? PREDATOR_LEARNING
                          : agent.species == Species::Omnivore ? 1 + (PREDATOR_LEARNING - 1) / 2
                                                               : 1.0;
    return LEARNING_RATE * (LEARNING_FLOOR + sharpness * (1 + LEARNING_BOOST * sharpness)) * hunter;
}

double choice_learning(int choice) {
    switch (choice) {
        case Breed: return BREED_LEARNING;
        case Court: return COURT_LEARNING;
        case Hunt: case Attack: case Feast: return KILL_FOOD_LEARNING;
        case Eat: case Forage: case Browse: case Uproot: return PLANT_FOOD_LEARNING;
        default: return 1.0;
    }
}

bool Simulation::sheltered(const Bitzkey& agent) const {
    if (!agent.infant()) return false;
    for (const int parent : {agent.mother, agent.father}) {
        const Bitzkey* found = parent >= 0 ? find_agent(parent) : nullptr;
        if (found && found->alive() && distance(found->x, found->y, agent.x, agent.y) <= SHELTER_REACH) return true;
    }
    return false;
}

int Simulation::night_start() const {
    return py_mod(-(turn_ % DAY_LENGTH) * width_ / DAY_LENGTH, width_);
}

bool Simulation::night(int x) const {
    return py_mod(x - night_start(), width_) < width_ / 2;
}

Brain& Simulation::learning_brain(Bitzkey& agent) {
    if (agent.brain.use_count() > 1) agent.brain = std::make_shared<Brain>(*agent.brain);
    return *agent.brain;
}

void Simulation::learn(Bitzkey& agent, double target, double outcome, bool replay, double call_target) {
    // From its own last choice, by TD(lambda), so the choices before it share in what came of it. With
    // `replay`, it then re-learns from its most recent experiences, as many as its intellect allows,
    // and remembers this one. On the GPU, the lessons are queued, to be learnt in one batch.
    const double rate = learning_rate(agent);
    const std::size_t count = replay ? static_cast<std::size_t>(REPLAY_PER_INTELLECT * thinking_intellect(agent.inborn[Intellect], agent.species)) : 0;
    const std::size_t first = agent.memories.size() > count ? agent.memories.size() - count : 0;
    const double weight = lesson_weight(agent.last_choice, outcome);
    if (gpu_) {
        if (rate > 0) {
            const double decay = discount(agent) * trace_decay(agent);
            queue_lesson(agent, agent.last_senses, agent.last_choice, target, rate * weight, true, decay);
            for (std::size_t index = first; count && index < agent.memories.size(); ++index) {
                const Bitzkey::Memory& memory = agent.memories[index];
                queue_lesson(agent, memory.senses, memory.choice, memory.target, rate * memory.weight, false, 0.0);
            }
            if (agent.last_call >= 0) queue_lesson(agent, agent.last_senses, call_output(agent.last_call), call_target, rate, false, 0.0);
            const int steps = std::min(static_cast<int>(agent.past.size()), MEMORY_STEPS);
            if (steps > 0 && MEMORY_LEARNING > 0) {  // back in time, the senses of the choices before following
                queue_lesson(agent, agent.last_senses, agent.last_choice, target, rate * MEMORY_LEARNING, false, 0.0, steps);
                for (int back = 0; back < steps; ++back) queue_lesson(agent, agent.past[static_cast<std::size_t>(back)], 0, 0.0, 0.0, false, 0.0, -1);
            }
        }
    } else {
        learning_brain(agent).learn_traced_by(agent.last_senses, agent.last_choice, target, rate * weight, discount(agent) * trace_decay(agent));
        for (std::size_t index = first; count && index < agent.memories.size(); ++index) {
            const Bitzkey::Memory& memory = agent.memories[index];
            agent.own_brain().learn(memory.senses, memory.choice, memory.target, rate * memory.weight);
        }
        // And from its call, as from an action untraced: what came of the turn, and the best call open now.
        if (agent.last_call >= 0) agent.own_brain().learn(agent.last_senses, call_output(agent.last_call), call_target, rate);
        // And back in time: what its memory held, which the choice read, is taught to the gates that wrote it.
        const int steps = std::min(static_cast<int>(agent.past.size()), MEMORY_STEPS);
        if (steps > 0 && MEMORY_LEARNING > 0) {
            std::vector<const Senses*> past;
            for (int back = 0; back < steps; ++back) past.push_back(&agent.past[static_cast<std::size_t>(back)]);
            agent.own_brain().learn_memory(agent.last_senses, past, agent.last_choice, target, rate * MEMORY_LEARNING);
        }
    }
    if (!count) return;
    agent.memories.push_back({agent.last_senses, agent.last_choice, target, weight});
    if (agent.memories.size() > count) agent.memories.erase(agent.memories.begin(), agent.memories.end() - static_cast<std::ptrdiff_t>(count));
}

void Simulation::queue_lesson(const Bitzkey& agent, const Senses& senses, int action, double target, double rate, bool traced,
                              double decay, int steps) {
    lesson_senses_.push_back(senses);
    lessons_.push_back({agent.gpu, &lesson_senses_.back(), action, target, rate, traced, decay, steps});
}

void Simulation::learn_queued() {
    if (gpu_ && !lessons_.empty()) {
        try {
            gpu_->learn(lessons_);
        } catch (const std::runtime_error& error) {
            leave_gpu(error);  // the lessons are lost with it
        }
    }
    lessons_.clear();
    lesson_senses_.clear();
}

void Simulation::attach(Bitzkey& agent) {
    if (!gpu_ || !agent.brain) return;
    try {
        agent.gpu = gpu_->attach(*agent.brain);
    } catch (const std::runtime_error& error) {
        agent.gpu = -1;
        leave_gpu(error);
    }
}

void Simulation::leave_gpu(const std::exception& error) {
    if (!gpu_) return;
    const auto engine = std::move(gpu_);  // gone at once, so nothing that follows calls back into it
    std::vector<gpu::BrainEngine::Handle> handles;
    std::vector<Brain*> into;
    for (const auto& agent : agents_) {
        if (agent->brain && agent->gpu >= 0) {
            handles.push_back(agent->gpu);
            into.push_back(&agent->own_brain());
        }
    }
    try {
        if (!handles.empty()) engine->download(handles, into);
    } catch (const std::runtime_error&) {  // their CPU copies stand as they were last brought back
    }
    for (const auto& agent : agents_) {
        agent->gpu = -1;
        agent->synced = -1;
    }
    lessons_.clear();
    lesson_senses_.clear();
    gpu_status_ = std::string("thinking on the CPU: the GPU failed (") + error.what() + ")";
}

void Simulation::use_engine(std::unique_ptr<gpu::BrainEngine> engine, std::string status) {
    learn_queued();  // what is waiting to be learnt, learnt where the brains are now
    sync_brains();
    const auto old = std::move(gpu_);
    for (const auto& agent : agents_) {
        agent->gpu = -1;
        agent->synced = -1;
    }
    gpu_ = std::move(engine);
    gpu_status_ = std::move(status);
    for (const auto& agent : agents_) {
        if (agent->alive()) attach(*agent);
    }
}

const Brain* Simulation::engine_brain(int handle) const {
    for (const auto& agent : agents_) {
        if (agent->gpu == handle && agent->brain) return agent->brain.get();
    }
    return nullptr;
}

void Simulation::sync(std::span<Bitzkey* const> agents) {
    if (!gpu_) return;
    std::vector<gpu::BrainEngine::Handle> handles;
    std::vector<Brain*> into;
    for (Bitzkey* agent : agents) {
        if (agent->brain && agent->gpu >= 0 && agent->synced != turn_) {  // each once a turn: it learns no more till its end
            agent->synced = turn_;
            handles.push_back(agent->gpu);
            into.push_back(&agent->own_brain());
        }
    }
    if (handles.empty()) return;
    try {
        gpu_->download(handles, into);
    } catch (const std::runtime_error& error) {
        leave_gpu(error);
    }
}

void Simulation::sync_brains() { sync_brains(0, 1, 1); }

void Simulation::sync_brains(int from, int count, int parts) {
    if (!gpu_) return;
    std::vector<gpu::BrainEngine::Handle> handles;
    std::vector<Brain*> into;
    for (const auto& agent : agents_) {
        if (agent->brain && agent->gpu >= 0 && ((agent->gpu - from) % parts + parts) % parts < count) {
            handles.push_back(agent->gpu);
            into.push_back(&agent->own_brain());
        }
    }
    if (handles.empty()) return;
    try {
        gpu_->download(handles, into);
    } catch (const std::runtime_error& error) {
        leave_gpu(error);
    }
}

bool Simulation::dies_of_age(Bitzkey& agent) {
    const double onset = AGEING * agent.lifespan;
    if (agent.age <= onset) return false;
    const double steepness = 2 * std::log(2.0) / std::pow(agent.lifespan - onset, 2.0);
    return rng().random() < steepness * (agent.age - onset);
}

void Simulation::think(Bitzkey& agent) { think_all({&agent}); }

void Simulation::think_all(const std::vector<Bitzkey*>& agents) {
    auto clock = std::chrono::steady_clock::now();
    // Every hunter first surveys its territory, so that all then sense the same world whatever their
    // order; then all sense it, on every core at once, each writing only to itself.
    std::for_each(std::execution::par, agents.begin(), agents.end(), [this](Bitzkey* agent) { survey(*agent); });
    std::vector<Sensed> plans(agents.size());
    std::vector<std::size_t> indices(agents.size());
    std::iota(indices.begin(), indices.end(), std::size_t{0});
    std::for_each(std::execution::par, indices.begin(), indices.end(),
                  [&](std::size_t index) { plans[index] = sense(*agents[index], true); });
    for (std::size_t index = 0; index < agents.size(); ++index) {  // all sense the world as it stands
        Bitzkey* agent = agents[index];
        const Sensed& sensed = plans[index];
        if (travelled(agent->last_action)) {
            if (sensed.situation.threat) {
                experience(*agent, Ambushed);
            } else if ((sensed.situation.grass || sensed.situation.grass_here) && !(agent->last_senses[SenseGrassSeen] != 0.0)) {
                experience(*agent, Discovery);
            }
        }
    }
    timings.sense += lap(clock);
    // A brain idle this turn isn't asked: one that can only rest (asleep with nothing to startle it, or spent), with no
    // lesson waiting on what it values now, its last turn having been as forced. Nothing it would say is used.
    std::vector<char> idle(agents.size());
    for (std::size_t index = 0; index < agents.size(); ++index) {
        const Bitzkey& agent = *agents[index];
        const std::vector<int>& options = plans[index].options;
        const bool startled = std::find(options.begin(), options.end(), Flee) != options.end() ||
                              std::find(options.begin(), options.end(), Defend) != options.end();
        const bool forced = agent.exhausted() || (SLEEP && resting_time(agent) && !agent.aloft_at_sea() && !startled);
        const bool lesson = agent.has_last_senses && (FORCED_LESSONS || !agent.last_forced || agent.last_panicked);
        idle[index] = forced && !lesson;
    }
    std::vector<std::size_t> asked;  // the rest
    for (const std::size_t index : indices) {
        if (!idle[index]) asked.push_back(index);
    }
    std::vector<Values> values(agents.size(), Values{});  // one batch of brains; the idle's left at nothing
    std::vector<double> comforts;
    comforts.reserve(agents.size());
    if (gpu_) {  // every brain at once, on the GPUs
        std::vector<gpu::BrainEngine::Handle> handles;
        std::vector<Senses> senses;
        handles.reserve(asked.size());
        senses.reserve(asked.size());
        for (const std::size_t index : asked) {
            handles.push_back(agents[index]->gpu);
            senses.push_back(plans[index].senses);
        }
        std::vector<Values> batch(asked.size());
        try {
            if (!asked.empty()) gpu_->values(handles, senses, batch);
            for (std::size_t place = 0; place < asked.size(); ++place) values[asked[place]] = batch[place];
        } catch (const std::runtime_error& error) {
            leave_gpu(error);  // and this turn's thinking is done on the CPU, below
        }
    }
    if (!gpu_) {  // on every core at once, each brain reading only itself
        std::for_each(std::execution::par, asked.begin(), asked.end(),
                      [&](std::size_t index) { values[index] = agents[index]->brain->values(plans[index].senses); });
    }
    for (std::size_t index = 0; index < agents.size(); ++index) comforts.push_back(comfort(*agents[index], plans[index].situation));
    timings.think += lap(clock);
    // What came of each one's last choice: whatever raised its health (fullness counting with it) or its mood was a
    // success, whatever lowered them a failure, by how much; both may happen. Each learns from it on its own brain
    // alone, so on the CPU they all learn at once, on every core, each its lessons in their order, as one by one.
    std::vector<double> rewards(agents.size()), targets(agents.size()), call_targets(agents.size());
    for (std::size_t index = 0; index < agents.size(); ++index) {
        Bitzkey& agent = *agents[index];
        if (!agent.has_last_senses) continue;
        rewards[index] = agent.reward + wellbeing(agent) - agent.last_wellbeing + comforts[index] - agent.last_comfort;
        // Having chosen to sit the weather out, it feels it the more (Resting unease); asleep or spent, it had no choice.
        if (agent.last_choice == Rest && !agent.last_forced) rewards[index] -= RESTING_UNEASE * plans[index].situation.exposure;
        const std::vector<int>& options = plans[index].options;
        double best = values[index][options[0]];
        for (const int option : options) best = std::max(best, values[index][option]);
        targets[index] = rewards[index] + discount(agent) * best;
        double best_call = values[index][call_output(Silent)];
        for (const int call : plans[index].calls) best_call = std::max(best_call, values[index][call_output(call)]);
        call_targets[index] = rewards[index] + discount(agent) * best_call;
    }
    const auto taught = [&](std::size_t index) {  // whether it learns from its last turn: not one it had no choice in, by the rule
        return agents[index]->has_last_senses && (FORCED_LESSONS || !agents[index]->last_forced || agents[index]->last_panicked);  // a panicked flight teaches
    };
    const auto teach = [&](std::size_t index) {
        if (taught(index)) learn(*agents[index], targets[index], rewards[index], true, call_targets[index]);
    };
    if (gpu_) std::for_each(indices.begin(), indices.end(), teach);  // queued for the GPUs, in order
    else std::for_each(std::execution::par, indices.begin(), indices.end(), teach);
    for (std::size_t index = 0; index < agents.size(); ++index) {
        if (taught(index) && rewards[index] > 0) experience(*agents[index], Lesson);  // a choice that turned out well
    }
    learn_queued();  // on the GPUs, every brain's lessons at once
    timings.learn += lap(clock);
    // Choosing, in three passes with the same outcome as one by one: first, in their order, what the world's one
    // stream of chance decides (primal fear, and the roll each choice is made by); then on every core at once, each
    // its own choice from its own values; then, in their order, what all share (the insights).
    std::vector<int> choices(agents.size());
    std::vector<double> rolls(agents.size()), explores(agents.size()), restless(agents.size()), call_rolls(agents.size());
    for (std::size_t index = 0; index < agents.size(); ++index) {
        Bitzkey& agent = *agents[index];
        Situation& situation = plans[index].situation;
        std::vector<int>& options = plans[index].options;
        if (primal_fear(agent, situation.hunter)) {
            // Overcome: it panics, fleeing the hunter, or freezes where it stands, whatever its brain wants.
            if (rng().random() < PRIMAL_FREEZE * (1 - agent.intelligence())) {
                situation.frozen = true;
                options = {Rest};
            } else {
                situation.panic = true;
                options = {Flee};
            }
        } else if (agent.panicked && situation.assailant) {
            // Fight or flight, decided by fear: it breaks off and flees its assailant.
            situation.hunter = situation.assailant;
            situation.panic = true;
            options = {Flee};
        }
        rolls[index] = rng().random();
        explores[index] = rng().random();
        restless[index] = rng().random();
        call_rolls[index] = rng().random();
    }
    std::for_each(std::execution::par, indices.begin(), indices.end(), [&](std::size_t index) {
        Bitzkey& agent = *agents[index];
        Situation& situation = plans[index].situation;
        std::vector<int>& options = plans[index].options;
        if (agent.exhausted()) {  // spent: it can do nothing but rest, whatever it fears
            situation.panic = situation.frozen = false;
            options = {Rest};
        } else if (SLEEP && resting_time(agent) && !agent.aloft_at_sea() && !situation.panic && !situation.frozen) {
            std::vector<int> asleep{Rest};  // its night: it rests, unless startled awake by a hunter or a blow
            for (const int option : options) {
                if (option == Flee || option == Defend) asleep.push_back(option);
            }
            options = std::move(asleep);
        }
        Values valued = values[index];
        double hungriest = 0.0;  // of its infants about it
        for (const Bitzkey* child : situation.young) hungriest = std::max(hungriest, std::min(100.0, child->hunger));
        add_instincts(agent, valued, options, agent.hunger, hungriest);
        const auto odds = choice_odds(valued, options);
        double roll = rolls[index];
        int choice = options.back();
        // In peril, with a hunter or threat near, struck, or starving, it heeds its brain alone: curiosity pales.
        const bool peril = situation.hunter || situation.threats > 0 || situation.assailant || agent.hunger >= 100;
        const double curiosity = agent.feeling(Curiosity);
        if (!peril && options.size() > 1 && explores[index] < CURIOUS_EXPLORATION * curiosity) {
            // Curious, it tries any of its options, whatever its brain makes of them.
            choice = options[std::min(options.size() - 1, static_cast<std::size_t>(roll * static_cast<double>(options.size())))];
        } else {
            for (const auto& [option, chance] : odds) {
                roll -= chance;
                if (roll < 0) {
                    choice = option;
                    break;
                }
            }
            const double restlessness = RESTLESS_CURIOSITY * (0.5 + curiosity);
            if (!peril && choice == Rest && options.size() > 1 && restless[index] < restlessness) {
                // Restless, it does any of its other options instead of resting.
                std::vector<int> others;
                for (const int option : options) {
                    if (option != Rest) others.push_back(option);
                }
                choice = others[std::min(others.size() - 1, static_cast<std::size_t>(restless[index] / restlessness * static_cast<double>(others.size())))];
            }
        }
        agent.odds.fill(0.0);
        agent.odds_offered = 0;
        for (const auto& [option, chance] : odds) {
            agent.odds[option] = chance;
            agent.odds_offered |= 1U << option;
        }
        // And what it calls as it does it: by its brain alone, one asleep, spent or frozen with fear keeping silent.
        agent.call = Silent;
        agent.last_call = -1;
        if (!idle[index] && !situation.frozen) {
            std::vector<int> outputs;
            for (const int call : plans[index].calls) outputs.push_back(call_output(call));
            double call_roll = call_rolls[index];
            for (const auto& [output, chance] : choice_odds(values[index], outputs)) {
                call_roll -= chance;
                agent.call = output - ACTION_COUNT;
                if (call_roll < 0) break;
            }
            agent.last_call = agent.call;
            agent.call_turn = turn_;
        }
        // What its memory now holds, written as it chose, unless its brain wasn't asked (asleep or spent, it keeps what
        // it held); and the senses it chose by before, while each choice since wrote its memory, to learn back in time.
        if (agent.has_last_senses && agent.last_wrote && MEMORY_STEPS > 0) {
            agent.past.insert(agent.past.begin(), agent.last_senses);
            if (agent.past.size() > static_cast<std::size_t>(MEMORY_STEPS)) agent.past.resize(static_cast<std::size_t>(MEMORY_STEPS));
        } else {
            agent.past.clear();
        }
        agent.last_wrote = !idle[index];
        if (!idle[index]) agent.held = memory_after(values[index], agent.held);
        agent.has_last_senses = true;
        agent.last_senses = plans[index].senses;
        agent.last_choice = choice;
        agent.last_forced = options.size() == 1;
        agent.last_panicked = situation.panic;
        agent.last_wellbeing = wellbeing(agent);
        agent.last_comfort = comforts[index];
        agent.reward = 0.0;
        agent.assailant.reset();  // the chance to strike back, or need to flee, passes with this choice
        agent.panicked = false;
        agent.alarm.reset();
        agent.herd_assailant.reset();
        agent.kill_alert.reset();
        choices[index] = choice;
    });
    insights_.record_all(agents.size(), [&](std::size_t index) -> std::optional<Insights::Choice> {
        const Situation& situation = plans[index].situation;
        if (situation.panic || situation.frozen) return std::nullopt;  // no choice of its own
        return Insights::Choice{agents[index]->species, agents[index]->profile(), plans[index].options, choices[index]};
    });
    if (gpu_) {  // the brains of all about to breed, and their mates', back from the GPUs at once
        std::vector<Bitzkey*> breeding;
        for (std::size_t index = 0; index < agents.size(); ++index) {
            if (choices[index] == Breed && plans[index].situation.mate) {
                breeding.push_back(agents[index]);
                breeding.push_back(plans[index].situation.mate);
            }
        }
        sync(breeding);
    }
    timings.choose += lap(clock);
    const bool tiled = act_in_tiles(agents, choices, plans);
    if (tiled) clock = std::chrono::steady_clock::now();
    for (std::size_t index = 0; !tiled && index < agents.size(); ++index) {  // each in turn, in the world as those before it left it
        if (!agents[index]->alive()) continue;
        act(*agents[index], choices[index], plans[index].situation);
        const double took = lap(clock);
        timings.actions[static_cast<std::size_t>(choices[index])] += took;
        timings.taken[static_cast<std::size_t>(choices[index])] += 1;
        timings.act += took;
    }
    conceive();  // before any parent learns again, which would copy the brain the young are made from
    const double conceiving = lap(clock);
    timings.actions[Breed] += conceiving;
    timings.act += conceiving;
    learn_witnessed();
    const double witnessed = lap(clock);
    timings.witnessed += witnessed;
    timings.act += witnessed;
}

thread_local Simulation::ActingTile* Simulation::acting_ = nullptr;

bool Simulation::act_in_tiles(const std::vector<Bitzkey*>& agents, const std::vector<int>& choices, std::vector<Sensed>& plans) {
    // The map parted into tiles, coloured as a chessboard of four colours, the tiles of a colour acting all at once, a
    // colour at a time. A tile is wider than what an animal acting may touch (as far as it goes, and a little more for
    // a blow and those beside it) and what it may look at (as far as it sees), together, so no two tiles acting at once
    // touch, or look at, the same squares or animals. Within a tile, its animals act in the turn's order, by the
    // tile's own stream of chance, drawn from the world's in the tiles' order. What reaches further (the herds' seeing
    // a death, alarms, parents' grief, conceptions, the deaths' records) is noted and done when all have acted.
    if (gpu_ || agents.empty()) return false;
    int fastest = 0;
    for (const Bitzkey* agent : agents) fastest = std::max(fastest, agent->pace());
    const int touches = fastest + HUNT_STEPS + 3, looks = std::max({MAX_SIGHT, FORAGE_RADIUS, ALARM_REACH});
    const int side = touches + looks + 1;
    int columns = width_ / side;
    columns -= columns % 2;  // even, so the colours alternate round the east-west wrap
    const int rows = height_ / side;
    if (columns < 2 || rows < 1) return false;
    const int tile_width = width_ / columns, tile_height = height_ / rows;
    std::vector<ActingTile> tiles(static_cast<std::size_t>(columns * rows));
    for (ActingTile& tile : tiles) tile.rng.seed(random_.getrandbits(64));
    for (std::size_t index = 0; index < agents.size(); ++index) {
        const Bitzkey& agent = *agents[index];
        const int column = std::min(columns - 1, py_mod(agent.x, width_) / tile_width);
        const int row = std::min(rows - 1, std::max(0, agent.y) / tile_height);
        tiles[static_cast<std::size_t>(column * rows + row)].members.push_back(index);
    }
    for (int colour = 0; colour < 4; ++colour) {
        std::vector<ActingTile*> now;
        for (int column = colour % 2; column < columns; column += 2) {
            for (int row = colour / 2; row < rows; row += 2) now.push_back(&tiles[static_cast<std::size_t>(column * rows + row)]);
        }
        std::for_each(std::execution::par, now.begin(), now.end(), [&](ActingTile* tile) {
            acting_ = tile;
            auto clock = std::chrono::steady_clock::now();
            for (const std::size_t index : tile->members) {
                if (!agents[index]->alive()) continue;
                act(*agents[index], choices[index], plans[index].situation);
                const double took = lap(clock);
                tile->took[static_cast<std::size_t>(choices[index])] += took;
                tile->taken[static_cast<std::size_t>(choices[index])] += 1;
                tile->acting += took;
            }
            acting_ = nullptr;
        });
    }
    // What reached beyond the tiles, in the tiles' order.
    for (ActingTile& tile : tiles) {
        for (const ActingTile::DeathNote& death : tile.deaths) record_death(*death.agent, death.cause, death.killer, death.killer_species, death.activity);
        kill_count_ = kill_count_ + static_cast<std::size_t>(tile.kills) - static_cast<std::size_t>(tile.kills_gone);
        carrion_count_ -= static_cast<std::size_t>(tile.carrion_gone);
        for (auto& brain : tile.dead_brains) dead_brains_.push_back(std::move(brain));
        for (Conception& young : tile.conceptions) conceptions_.push_back(std::move(young));
        for (std::size_t action = 0; action < tile.took.size(); ++action) {
            timings.actions[action] += tile.took[action];
            timings.taken[action] += tile.taken[action];
        }
        timings.act += tile.acting;
    }
    for (ActingTile& tile : tiles) {
        for (const auto& [actor, outcome] : tile.witnessed) witness(*actor, outcome);
    }
    for (ActingTile& tile : tiles) {
        for (const auto& [agent, about] : tile.alerts) alert(*agent, *about);
    }
    for (ActingTile& tile : tiles) {
        for (const auto& [parent, young] : tile.feeding) feed_young(*parent, young);
    }
    return true;
}

void Simulation::learn_witnessed() {
    if (gpu_) {
        for (const Witnessed& seen : witnessed_) {
            if (seen.observer->brain && seen.rate > 0) queue_lesson(*seen.observer, seen.senses, seen.choice, seen.outcome, seen.rate, false, 0.0);
        }
    } else {  // each observer its lessons in their order, on its own brain alone, so all at once on every core
        std::vector<std::size_t> order(witnessed_.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return witnessed_[a].observer < witnessed_[b].observer; });
        std::vector<std::pair<std::size_t, std::size_t>> runs;  // each observer's lessons, [from, to) in `order`
        for (std::size_t from = 0, to = 0; from < order.size(); from = to) {
            while (to < order.size() && witnessed_[order[to]].observer == witnessed_[order[from]].observer) ++to;
            runs.emplace_back(from, to);
        }
        std::for_each(std::execution::par, runs.begin(), runs.end(), [&](const std::pair<std::size_t, std::size_t>& run) {
            for (std::size_t at = run.first; at < run.second; ++at) {
                const Witnessed& seen = witnessed_[order[at]];
                if (!seen.observer->brain || seen.rate <= 0) continue;  // not since dead
                seen.observer->own_brain().learn(seen.senses, seen.choice, seen.outcome, seen.rate);
            }
        });
    }
    witnessed_.clear();
    learn_queued();
}

void Simulation::survey(Bitzkey& agent) {
    if (agent.species == Species::Predator || agent.species == Species::Omnivore) {
        std::tie(agent.rivalry, agent.own_rivalry) = territory(agent);
        std::tie(agent.hunts_grazers, agent.hunts_omnivores) = prey_switching(agent);
        if (agent.species == Species::Omnivore) agent.hunts_predators = other_food(agent) <= OMNIVORE_PREDATOR_FOOD;
    }
}

Simulation::Sensed Simulation::sense(Bitzkey& agent, bool surveyed) {
    if (!surveyed) survey(agent);
    Sensed sensed;
    Situation& s = sensed.situation;
    const std::vector<Bitzkey*> neighbors = nearby(agent, 1);
    for (Bitzkey* other : around(agent, MATE_REACH)) {  // close enough to breed with: the most dominant, the first found on a tie
        if (compatible(agent, *other) && other->can_breed() && (!s.mate || other->mind[Dominance] > s.mate->mind[Dominance])) {
            s.mate = other;
        }
    }
    s.nurseries = free_cells_near(agent);
    std::vector<Bitzkey*> prey, threats;
    for (Bitzkey* other : neighbors) {
        if (is_prey(agent, *other)) prey.push_back(other);
    }
    for (Bitzkey* other : neighbors) {
        if (is_prey(*other, agent)) threats.push_back(other);
    }
    double best = 0.0;
    for (Bitzkey* other : prey) {
        const double frailty = damage(agent, *other) / other->health;
        if (!s.prey || frailty > best) {
            s.prey = other;
            best = frailty;
        }
    }
    for (Bitzkey* other : threats) {
        const double danger = damage(*other, agent);
        if (!s.threat || danger > best) {
            s.threat = other;
            best = danger;
        }
    }
    s.threats = static_cast<int>(threats.size());
    for (const Bitzkey* other : around(agent, 2)) {
        s.kin += other->species == agent.species;
        s.others += other->species != agent.species && other->species != Species::Plant;
    }
    const int sight = agent.sight;
    const bool grazes = agent.species != Species::Predator;
    s.grass_here = grazes && grass_at(agent.x, agent.y) > 0;
    if (grazes) s.grass = nearest_grass(agent, sight);
    if (agent.species != Species::Grazer) s.carrion = nearest_carrion(agent, sight);  // grazers don't scavenge
    if (agent.species == Species::Omnivore || agent.species == Species::Predator) s.kill = nearest_kill(agent, sight);
    const int prey_sight = sight + (agent.species == Species::Predator ? PREDATOR_SIGHT : 0);
    const Seen seen = look_around(agent, sight, prey_sight);
    s.quarry = seen.quarry;
    s.hunter = seen.hunter;
    s.suitor = seen.suitor;
    if (!s.suitor && agent.species == Species::Predator && agent.can_breed()) {  // the mating call reaches further
        for (Bitzkey* other : around(agent, static_cast<int>(MATING_CALL_RANGE * sight))) {
            if (compatible(agent, *other) && other->can_breed()) {
                s.suitor = other;
                break;
            }
        }
    }
    if (s.suitor) s.suitor_at = Cell{s.suitor->x, s.suitor->y};
    s.flora = seen.flora;
    for (Bitzkey* other : neighbors) {
        if (agent.assailant && other->identifier == *agent.assailant) {
            s.assailant = other;
            break;
        }
    }
    Bitzkey* herd_assailant = nullptr;
    for (Bitzkey* other : neighbors) {
        if (agent.herd_assailant && other->identifier == *agent.herd_assailant) {
            herd_assailant = other;
            break;
        }
    }
    s.defend_target = s.assailant ? s.assailant : herd_assailant;
    const HerdView view = herd(agent);
    const double here = in_bounds(agent.x, agent.y) ? temperature(agent.y) : Medium;
    const double discomfort = agent.climate_steps(here);
    // The nearest row it would feel at home in, however far, and the same only if within sight.
    const std::optional<Cell> home = discomfort > 0 ? comfort_row(agent, height_) : std::nullopt;
    const std::optional<Cell> comfort = home && std::abs(home->y - agent.y) <= sight ? home : std::nullopt;
    const double home_distance = discomfort <= 0 ? 0.0 : home ? std::min(1.0, std::abs(home->y - agent.y) / (0.5 * height_)) : 1.0;
    Bitzkey* const seen_hunter = s.hunter;  // what it sees itself, to alert its herd to
    bool alarmed = false;
    if (agent.alarm) {
        for (Bitzkey* other : around(agent, ALARM_REACH)) {
            if (other->identifier == *agent.alarm) {
                alarmed = true;
                if (!s.hunter) s.hunter = other;  // the herd's alarm: flight from whatever struck a herd-mate, or it was alerted to
                break;
            }
        }
    }
    Bitzkey* const seen_kill = s.kill;
    Bitzkey* alerted_kill = nullptr;
    if (agent.kill_alert && in_bounds(agent.kill_alert->x, agent.kill_alert->y)) {  // a pack-mate's alert
        for (Bitzkey* corpse : kills_[static_cast<std::size_t>(agent.kill_alert->y) * width_ + agent.kill_alert->x]) {
            if (feeds_on(agent, *corpse)) {
                alerted_kill = corpse;
                break;
            }
        }
        if (!s.kill) s.kill = alerted_kill;  // a fresh kill it may not see itself
    }

    std::array<bool, ACTION_COUNT> available{};
    available[GoEast] = available[GoWest] = available[WalkEast] = available[WalkWest] = true;
    available[Rest] = !agent.aloft_at_sea();  // a flyer can't rest over the sea
    available[Eat] = s.grass_here;
    available[Forage] = s.grass.has_value();
    available[Browse] = s.flora != nullptr;
    available[Scavenge] = s.carrion != nullptr;
    available[Feast] = s.kill != nullptr;
    // A pack keeps to its temperature as a herd does: a predator courts and hunts only toward a row no further from
    // its home temperature than where it stands.
    const bool pack = agent.species == Species::Predator;
    const auto homeward = [&](int y) { return agent.climate_steps(temperature(y)) <= discomfort; };
    available[Court] = s.suitor && may_breed(agent, *s.suitor) && (!pack || homeward(s.suitor->y));  // courting only a partner it could then breed with
    available[Breed] = s.mate && !s.nurseries.empty() && may_breed(agent, *s.mate);
    available[Hunt] = s.quarry != nullptr && (!pack || homeward(s.quarry->y));
    available[Attack] = s.prey != nullptr;
    // Only from a hunter close by, unless the herd raised the alarm.
    available[Flee] = s.hunter && (alarmed || distance(s.hunter->x, s.hunter->y, agent.x, agent.y) <=
                                                  flee_range(agent, view.close));
    available[Defend] = s.defend_target != nullptr &&  // a grazer only when strong enough
                        (agent.species != Species::Grazer || agent.trait(Strength) > GRAZER_DEFENCE_STRENGTH);
    // The more fearful it is, the closer it keeps to the herd's middle, but not where the middle is
    // further from its home temperature than here.
    const double spread = agent.infant() ? 1.0 : HERD_SPREAD * (1 - agent.fearfulness());  // an infant wants to be right among them
    available[Herd] = view.middle && distance(view.middle->x, view.middle->y, agent.x, agent.y) > spread && homeward(view.middle->y);
    if (agent.species == Species::Grazer && agent.hunger >= UPROOT_HUNGER) {
        for (Bitzkey* other : neighbors) {
            if (other->species == Species::Plant) {
                s.uprootable = other;
                break;
            }
        }
        // Starving, with food scarce: it may tear into a plant beside it.
        available[Uproot] = s.uprootable && food_supply(agent) < UPROOT_FOOD;
    }
    if (view.any) {  // with a herd to tell: a grazer of a hunter it sees, a hunter of a fresh kill it sees
        if (agent.species == Species::Grazer && seen_hunter) s.alert_to = seen_hunter;
        else if ((agent.species == Species::Omnivore || agent.species == Species::Predator) && seen_kill) s.alert_to = seen_kill;
    }
    sensed.calls = {Silent};
    if (s.alert_to) sensed.calls.push_back(AlertCall);
    for (int call = FirstCall; call < CALL_OPTIONS; ++call) sensed.calls.push_back(call);
    const double fed_at = remembered(agent, agent.food_memory);
    if (agent.food_memory) s.revisit = Cell{agent.food_memory->x, agent.food_memory->y};
    available[Revisit] = fed_at > 0 && !(s.revisit->x == agent.x && s.revisit->y == agent.y);  // back to where it last fed
    // Its own infants within its sight, which it may feed once one of them is hungry.
    if (agent.children) {
        for (Bitzkey* other : around(agent, sight)) {
            if ((other->mother == agent.identifier || other->father == agent.identifier) && other->infant()) s.young.push_back(other);
        }
    }
    available[FeedYoung] = !agent.infant() && std::any_of(s.young.begin(), s.young.end(), [](const Bitzkey* child) { return child->hunger >= FEED_HUNGER; });
    available[GoNorth] = available[WalkNorth] = agent.y > 0;
    available[GoSouth] = available[WalkSouth] = agent.y < height_ - 1;
    for (int action = 0; action < ACTION_COUNT; ++action) {
        if (available[action]) sensed.options.push_back(action);
    }

    Senses& senses = sensed.senses;
    senses[SenseHunger] = std::min(1.0, agent.hunger / 100);
    senses[SenseHealth] = std::max(0.0, agent.health / agent.max_health);
    senses[SenseGrassHere] = s.grass_here;
    senses[SenseGrassSeen] = s.grass.has_value();
    senses[SenseGrassCloseness] = closeness(agent, s.grass, sight);
    senses[SenseMateNear] = s.mate != nullptr;
    senses[SenseMateCloseness] = closeness(agent, cell_of(s.suitor), sight);
    senses[SenseReadyToBreed] = agent.cooldown == 0 && agent.in_heat;  // rested, and in heat
    senses[SensePreyNear] = std::min<int>(4, static_cast<int>(prey.size())) / 4.0;
    senses[SensePreyFrailty] = s.prey ? std::min(1.0, damage(agent, *s.prey) / s.prey->health) : 0.0;
    senses[SensePreyCloseness] = closeness(agent, cell_of(s.quarry), prey_sight);
    senses[SensePreyPregnant] = s.quarry && !s.quarry->womb.empty();
    senses[SenseThreatNear] = std::min<int>(4, static_cast<int>(threats.size())) / 4.0;
    senses[SenseThreatDanger] = s.threat ? std::min(1.0, damage(*s.threat, agent) / agent.health) : 0.0;
    senses[SenseThreatCloseness] = closeness(agent, cell_of(s.hunter), sight);
    senses[SenseKinNear] = std::min(3, s.kin) / 3.0;
    senses[SenseOthersNear] = std::min(3, s.others) / 3.0;
    senses[SenseCarrionCloseness] = closeness(agent, cell_of(s.carrion), sight);
    senses[SenseKillCloseness] = closeness(agent, cell_of(s.kill), sight);
    senses[SensePlantCloseness] = closeness(agent, cell_of(s.flora), sight);
    senses[SenseAssailantNear] = s.assailant != nullptr;
    senses[SenseCounterFrailty] =
        s.defend_target ? std::min(1.0, damage(agent, *s.defend_target) / s.defend_target->health) : 0.0;
    senses[SenseRivalry] = agent.rivalry;
    senses[SenseHerdNear] = std::min(3, view.close) / 3.0;
    senses[SenseHerdCloseness] =
        view.middle ? std::max(0.0, 1 - static_cast<double>(distance(view.middle->x, view.middle->y, agent.x, agent.y)) / agent.sight)
                    : 0.0;
    senses[SenseAlarm] = agent.alarm.has_value();
    senses[SenseKillAlert] = alerted_kill != nullptr;
    senses[SenseHerdSuccess] = agent.seen_success;
    senses[SenseHerdFailure] = agent.seen_failure;
    senses[SenseClimateDiscomfort] = std::min(2.0, discomfort) / 2;
    senses[SenseComfortCloseness] = discomfort > 0 ? closeness(agent, comfort, sight) : 1.0;
    senses[SenseAge] = std::min(1.0, agent.age / 400.0);
    const double day = static_cast<double>(turn_ % YEAR_LENGTH) / YEAR_LENGTH;  // 0 at the year's start, nearly 1 at its end
    senses[SenseTimeOfYearA] = std::abs(2 * day - 1);
    senses[SenseTimeOfYearB] = std::abs(2 * std::fmod(day + 0.25, 1.0) - 1);
    senses[SenseLatitude] = 2 * (agent.y + 0.5) / height_ - 1;
    senses[SenseWarmth] = warmth(agent, agent.y);
    senses[SenseWarmthNorth] = warmth(agent, std::max(0, agent.y - sight));
    senses[SenseWarmthSouth] = warmth(agent, std::min(height_ - 1, agent.y + sight));
    senses[SenseFoodRemembered] = fed_at;
    senses[SenseDangerRemembered] =
        remembered(agent, agent.danger_memory) *
        std::max(0.0, closeness(agent, agent.danger_memory ? std::optional<Cell>(Cell{agent.danger_memory->x, agent.danger_memory->y})
                                                           : std::nullopt, sight));
    senses[SenseHomeFood] = home_food_sense(agent);
    senses[SenseHomeDirection] = home ? (home->y > agent.y ? 1.0 : -1.0) : 0.0;
    senses[SenseHomeDistance] = home_distance;
    const int young = static_cast<int>(s.young.size());  // its own young, still infants, within its sight
    senses[SenseYoungNear] = std::min(3, young) / 3.0;
    senses[SenseSeaHere] = agent.wet;
    senses[SenseStamina] = agent.stamina / FULL_STAMINA;
    senses[SenseNight] = night(agent.x);
    if (agent.infant()) {  // its nearest living parent in sight, and which way it lies
        const Bitzkey* nearest = nullptr;
        for (const int parent : {agent.mother, agent.father}) {
            const Bitzkey* found = parent >= 0 ? find_agent(parent) : nullptr;
            if (found && found->alive() && distance(found->x, found->y, agent.x, agent.y) <= sight &&
                (!nearest || distance(found->x, found->y, agent.x, agent.y) < distance(nearest->x, nearest->y, agent.x, agent.y))) {
                nearest = found;
            }
        }
        if (nearest) {
            senses[SenseParentCloseness] = closeness(agent, cell_of(nearest), sight);
            senses[SenseParentEast] = std::clamp(static_cast<double>(nearest->x - agent.x) / sight, -1.0, 1.0);
            senses[SenseParentSouth] = std::clamp(static_cast<double>(nearest->y - agent.y) / sight, -1.0, 1.0);
        }
    }
    {  // the calls made about it last turn, by any kind, each as its loudest caller made it
        std::array<double, CALL_COUNT> loudest{};
        const int furthest = std::min(MAX_CALL_REACH, static_cast<int>(std::ceil(CALL_REACH * 1.5)));  // the largest caller's
        for (const Bitzkey* other : around(agent, furthest)) {
            if (other->call < FirstCall || other->call_turn != turn_ - 1) continue;
            const int reach = call_reach(*other);
            const int away = distance(other->x, other->y, agent.x, agent.y);
            if (away > reach) continue;
            const double loudness = 1 - static_cast<double>(away) / (reach + 1);
            double& louder = loudest[static_cast<std::size_t>(other->call - FirstCall)];
            if (loudness <= louder) continue;
            louder = loudness;
            senses[heard_sense(other->call, HeardLoudness)] = loudness;
            senses[heard_sense(other->call, HeardEast)] =
                std::clamp(static_cast<double>(py_mod(other->x - agent.x + width_ / 2, width_) - width_ / 2) / reach, -1.0, 1.0);
            senses[heard_sense(other->call, HeardSouth)] = std::clamp(static_cast<double>(other->y - agent.y) / reach, -1.0, 1.0);
            senses[heard_sense(other->call, HeardKin)] = other->species == agent.species;
        }
    }
    if (has_sea() && agent.terrain != Terrain::Avian && agent.terrain != Terrain::Amphibious) {
        for (int heading = 0; heading < 4; ++heading) {  // north, south, east, west: the nearest shore it can't cross
            const Heading& way = HEADINGS[static_cast<std::size_t>(heading)];
            for (int cells = 1; cells <= sight; ++cells) {
                const int y = agent.y + way.dy * cells;
                if (y < 0 || y >= height_) break;
                if (!can_enter(agent, Cell{py_mod(agent.x + way.dx * cells, width_), y})) {
                    senses[SenseShoreNorth + heading] = 1 - static_cast<double>(cells - 1) / sight;
                    break;
                }
            }
        }
    }
    for (int held = 0; held < MEMORY_NEURONS; ++held) senses[SenseMemory + held] = agent.held[held];
    for (int trait = 0; trait < TEMPERAMENT_COUNT; ++trait) senses[SenseMind + trait] = agent.feeling(trait);
    gene_senses(agent.dna, senses);

    s.rivalry = agent.rivalry;
    s.for_herd = s.assailant == nullptr;
    s.herd = view.any;
    s.herd_near = view.close;
    s.herd_middle = view.middle;
    s.exposure = std::min(2.0, discomfort) / 2 * agent.exposure_share(here);
    s.home_distance = home_distance * agent.exposure_share(here);
    s.young_near = young;
    return sensed;
}

double Simulation::wellbeing(const Bitzkey& agent) const {
    const double appetite = 0.5 + agent.feeling(Appetite);
    return -appetite * std::min(100.0, agent.hunger) / 100 - (1 - std::max(0.0, agent.health) / agent.max_health);
}

double Simulation::comfort(const Bitzkey& agent, const Situation& s) const {
    const double tolerance = agent.feeling(Tolerance);
    const double instinct = agent.herd_instinct();
    const double herd_near = std::min(3, s.herd_near) / 3.0;
    const double company_around = std::min(3.0, s.kin + tolerance * s.others);
    const double company = COMPANY_JOY * agent.feeling(Sociability) * company_around / 3;
    const double dread = FEAR_DREAD * agent.fearfulness() * (1 - 0.5 * tolerance) * std::min(2, s.threats) / 2 *
                         (1 - HERD_SAFETY * instinct * herd_near);
    const double unease = UNEASE * (1 - tolerance) * std::min(3, s.others) / 3;
    const double crowded = RIVAL_UNEASE * (1 - 0.5 * tolerance) * s.rivalry *
                           (1 + DOMINANCE_EASE * (1 - 2 * agent.feeling(Dominance)));  // the submissive feel it most
    double herding = HERD_JOY * instinct * herd_near;
    if (!s.herd && !solitary(agent.species)) herding -= HERD_LONELINESS * instinct;  // a loner misses no herd
    herding *= 0.5 + agent.fearfulness();  // the fearful feel their herd, and its absence, most
    // Out of its home temperature, it is uneasy, the more so the further, and less if hardy to it.
    // and more the further home lies, so every step toward it is felt, not only the last.
    const double exposed = CLIMATE_UNEASE * s.exposure + HOMEWARD_UNEASE * s.home_distance;
    // A parent is glad of its own young close by, the more so the more empathetic.
    const double parental = PARENT_JOY * (0.5 + agent.feeling(Empathy)) * std::min(3, s.young_near) / 3;
    return company - dread - unease - crowded + herding - exposed + parental;
}

std::vector<Death> Simulation::deaths_so_far() const {
    std::vector<Death> deaths;
    for (const auto& [identifier, death] : death_records_) {
        if (death.turn <= turn_) deaths.push_back(death);
    }
    return deaths;
}

void Simulation::record_death(Bitzkey& agent, std::string_view cause, int killer, Species killer_species, std::string_view activity) {
    const Death& death = death_records_[agent.identifier] = {std::string(cause), agent.species, turn_, killer, killer >= 0 ? killer_species : Species::Plant,
                                                             std::string(activity), agent.age / DAY_LENGTH};
    death_tally_.add(death);
    death_order_.emplace_back(turn_, agent.identifier);
    if (agent.infant()) {  // its parents grieve, wherever they are, and learn from it what came before
        for (const int identifier : {agent.mother, agent.father}) {
            Bitzkey* parent = identifier >= 0 ? find_agent(identifier) : nullptr;
            if (parent && parent->alive() && parent->brain) parent->reward -= PARENT_GRIEF * (0.5 + parent->feeling(Empathy));
        }
    }
    deaths_ += 1;
    death_causes_[static_cast<std::size_t>(agent.species)][std::string(cause)] += 1;
    if (agent.species == Species::Plant) {
        plants_ -= 1;
    } else {
        population_ -= 1;
    }
}

void Simulation::die(Bitzkey& agent, std::string_view cause, const Bitzkey* killer, std::string_view activity) {
    ActingTile* tile = acting_;
    if (tile) {
        tile->deaths.push_back({&agent, std::string(cause), killer ? killer->identifier : -1, killer ? killer->species : Species::Plant,
                                std::string(activity)});
    } else {
        record_death(agent, cause, killer ? killer->identifier : -1, killer ? killer->species : Species::Plant, activity);
    }
    if (killer && !agent.womb.empty() && (killer->species == Species::Predator || killer->species == Species::Omnivore)) {
        // A hunter that killed a mother carrying young feels remorse for it.
        Bitzkey* hunter = find_agent(killer->identifier);
        if (hunter && hunter->alive() && hunter->brain) hunter->reward -= PREGNANT_REMORSE * (0.5 + hunter->feeling(Empathy));
    }
    // Its herd sees a sudden death fail; a slow one (old age, starving or the cold or heat) is not its last choice's
    // doing. Its own brain learns nothing from it, as it goes with it.
    if (agent.brain && agent.has_last_senses && !slow_death(cause)) witness(agent, -DEATH_PENALTY);
    std::vector<std::shared_ptr<Brain>>& dead_brains = tile ? tile->dead_brains : dead_brains_;
    if (agent.brain) dead_brains.push_back(std::move(agent.brain));
    agent.memories.clear();
    if (gpu_ && agent.gpu >= 0) {
        gpu_->detach(agent.gpu);
        agent.gpu = -1;
    }
    for (Bitzkey::Young& young : agent.womb) {  // the young it carried die with it
        if (young.brain) dead_brains.push_back(std::move(young.brain));
    }
    agent.womb.clear();
    agent.due_turn.reset();
    agent.health = std::min(agent.health, 0.0);  // whatever the cause, a recorded death leaves it dead
    agent.died_turn = turn_;
    agent.last_action = cause;
    const double nourished = agent.species == Species::Plant ? 0.0 : std::max(0.0, 1 - agent.hunger / 100);
    agent.meat = (CARRION_MEAT + CARRION_MEAT_FOR_SIZE * agent.physique(Size)) * nourished * (cause == "killed" ? 0.5 : 1);
    agent.rot = agent.meat / std::max(1, CORPSE_TURNS - FRESH_KILL_TURNS);  // it rots once no longer fresh
    agent.portions = agent.species == Species::Plant ? 0 : 1 + agent.trait(Size);
    if (agent.species != Species::Plant && in_bounds(agent.x, agent.y)) {
        kills_[static_cast<std::size_t>(agent.y) * width_ + agent.x].push_back(&agent);
        if (tile) ++tile->kills;
        else ++kill_count_;
    }
}

// ---------------------------------------------------------------------------------------------
// Actions

void Simulation::act(Bitzkey& agent, int action, Situation& s) {
    if (agent.call != Silent && agent.call_turn == turn_) {  // its call, as it sets about its action
        agent.hunger = std::min(100.0, agent.hunger + CALL_COST);
        if (agent.call == AlertCall && s.alert_to) alert(agent, *s.alert_to);
    }
    const std::size_t passed = agent.trail.size();
    perform(agent, action, s);
    const std::string_view did = agent.last_action;
    if (did == "ate" || did == "browsed" || did == "uprooted" || did == "feasted" || did == "scavenged") {
        agent.food_memory = Place{agent.x, agent.y, turn_};
    }
    if (!agent.alive()) return;
    if (did == "rested") {
        agent.stamina = std::min(FULL_STAMINA, agent.stamina + rest_recovery());
        return;
    }
    // Tired by what it did: each square it went (flying, the more so), each blow struck, and keeping aloft over the sea.
    const double cells = static_cast<double>(agent.trail.size() - passed);
    double cost = ACTION_FATIGUE + cells * STEP_FATIGUE * (agent.terrain == Terrain::Avian ? FLIGHT_FATIGUE : 1.0);
    if (did == "attacked" || did == "pounced" || did == "defended") cost += STRIKE_FATIGUE;
    if (agent.aloft_at_sea()) cost += HOVER_FATIGUE;
    agent.stamina = std::max(0.0, agent.stamina - fatigue(agent, cost));
    if (agent.aloft_at_sea() && agent.exhausted()) {
        agent.health = 0;
        die(agent, "drowned");  // too tired to stay aloft, with nowhere to land
    }
}

double Simulation::remembered(const Bitzkey& agent, const std::optional<Place>& memory) const {
    const double span = MEMORY_TURNS * agent.feeling(Memory);
    if (!memory || span <= 0) return 0.0;
    return std::max(0.0, 1 - static_cast<double>(turn_ - memory->turn) / span);
}

void Simulation::perform(Bitzkey& agent, int action, Situation& s) {
    switch (action) {
        case Eat:
            if (grass_cell(agent.x, agent.y) <= 0) {  // another got here first
                agent.last_action = "found it eaten";
                break;
            }
            grass_cell(agent.x, agent.y) -= 1;
            agent.nourish(GRASS_MEAL * agent.grazing);
            if (agent.last_action == "foraged") experience(agent, ForagedMeal);
            agent.last_action = "ate";
            break;
        case Forage:
            move_toward(agent, s.grass->x, s.grass->y);
            agent.last_action = "foraged";
            break;
        case Browse:
            browse(agent, *s.flora);
            break;
        case Uproot:
            uproot(agent, *s.uprootable);
            break;
        case Scavenge:
            scavenge(agent, *s.carrion);
            break;
        case Feast:
            feast(agent, *s.kill);
            break;
        case Rest:
            if (s.frozen) {
                agent.last_action = "froze";  // paralysed by fear: nothing done, nothing gained
                break;
            }
            agent.hunger = std::max(0.0, agent.hunger - agent.burn / 2);  // burns less, but eases no hunger
            agent.heal(0.015);
            agent.last_action = "rested";
            break;
        case Court: {
            // Toward where it saw its suitor; the suitor itself is looked at only if it was seen within sight (the
            // mating call reaches further), as one further off can't come within reach this turn.
            Bitzkey& suitor = *s.suitor;
            const Cell seen = s.suitor_at.value_or(Cell{suitor.x, suitor.y});
            const bool near = distance(seen.x, seen.y, agent.x, agent.y) <= MAX_SIGHT;
            move_toward(agent, seen.x, seen.y);
            agent.last_action = "courted";
            if (near && suitor.alive() && distance(suitor.x, suitor.y, agent.x, agent.y) <= MATE_REACH && may_breed(agent, suitor)) {
                agent.reward += mating_joy(agent, COURT_JOY);  // courted into reach, to breed
            }
            break;
        }
        case Breed: {
            Bitzkey& mate = *s.mate;
            if (!(mate.alive() && mate.can_breed() && agent.can_breed() && distance(mate.x, mate.y, agent.x, agent.y) <= MATE_REACH)) {
                agent.last_action = "was spurned";  // its mate died, moved off or bred with another first
                break;
            }
            agent.last_action = "mated";  // before breeding, so a parent that dies of it says so
            breed(agent, mate);
            break;
        }
        case Hunt: {
            Bitzkey& quarry = *s.quarry;
            move_toward(agent, quarry.x, quarry.y, agent.pace() + (agent.species == Species::Predator ? HUNT_STEPS : 0));  // predators chase further
            agent.last_action = "hunted";
            if (quarry.alive() && distance(quarry.x, quarry.y, agent.x, agent.y) <= 1) {
                attack(agent, quarry, "hunting");
                if (agent.alive()) agent.last_action = "pounced";
            }
            break;
        }
        case Attack:
            if (!(s.prey->alive() && distance(s.prey->x, s.prey->y, agent.x, agent.y) <= 1)) {
                agent.last_action = "missed";  // it fell or got away first
                break;
            }
            attack(agent, *s.prey);
            break;
        case Defend:
            if (!(s.defend_target->alive() && distance(s.defend_target->x, s.defend_target->y, agent.x, agent.y) <= 1)) {
                agent.last_action = "missed";
                break;
            }
            defend(agent, *s.defend_target, s.for_herd);
            break;
        case Herd:
            move_toward(agent, s.herd_middle->x, s.herd_middle->y);
            agent.last_action = "herded";
            break;
        case Revisit:
            move_toward(agent, s.revisit->x, s.revisit->y);
            agent.last_action = "revisited";
            break;
        case FeedYoung:
            feed_young(agent, s.young);
            break;
        case GoNorth:
        case GoSouth:
        case GoEast:
        case GoWest:
        case WalkNorth:
        case WalkSouth:
        case WalkEast:
        case WalkWest: {
            const Heading& heading = HEADINGS[static_cast<std::size_t>(action - GoNorth)];
            const int cells = heading.walk ? 1 : agent.pace();
            int moved = 0;  // as far as it goes before the map's edge (north and south) or a full square
            for (int step = 1; step <= cells; ++step) {
                const int x = py_mod(agent.x + heading.dx * step, width_), y = agent.y + heading.dy * step;
                if (y < 0 || y >= height_ || !can_enter(agent, Cell{x, y}) || room_in(Cell{x, y}, &agent) < agent.space()) break;
                moved = step;
            }
            if (moved) move(agent, py_mod(agent.x + heading.dx * moved, width_), agent.y + heading.dy * moved);
            if (heading.walk) {
                agent.hunger = std::max(0.0, agent.hunger - agent.burn * WALK_REST);  // burns less, but eases no hunger
            } else {
                effort(agent, moved);
                exhausted(agent);
            }
            if (moved) agent.reward += CURIOSITY_JOY * agent.feeling(Curiosity);  // setting off pleases the curious; walking into a wall doesn't
            agent.last_action = heading.went;
            break;
        }
        case Flee:
            move_away(agent, *s.hunter);
            agent.last_action = s.panic ? "panicked" : "fled";
            break;
    }
}

void Simulation::browse(Bitzkey& agent, Bitzkey& plant) {
    move_toward(agent, plant.x, plant.y);
    if (!plant.alive() || distance(plant.x, plant.y, agent.x, agent.y) > 1 || plant.cooldown) {
        agent.last_action = "sought plants";
        return;
    }
    agent.nourish((PLANT_MEAL + PLANT_MEAL_FOR_SIZE * plant.physique(Size)) * agent.browsing * plant_yield(plant));
    plant.health -= PLANT_BITE;
    plant.cooldown = PLANT_RECOVERY;
    agent.last_action = "browsed";
    if (!plant.alive()) {
        die(plant, "eaten", &agent, "browsing");
    } else if (plant.physique(Weapons) > 0) {
        defend(plant, agent);  // a plant can't flee, but an armed one strikes back
    }
}

void Simulation::uproot(Bitzkey& agent, Bitzkey& plant) {
    if (!plant.alive()) {  // another got to it first
        agent.last_action = "sought plants";
        return;
    }
    const double feast = FRESH_KILL_MEALS * (KILL_MEAL + KILL_MEAL_FOR_SIZE * plant.physique(Size));  // on a body of its size
    agent.nourish(UPROOT_MEALS * feast * plant_yield(plant));
    plant.health -= UPROOT_BITE;
    plant.cooldown = PLANT_RECOVERY;
    agent.last_action = "uprooted";
    if (!plant.alive()) {
        die(plant, "uprooted", &agent, "uprooting");
    } else if (plant.physique(Weapons) > 0) {
        defend(plant, agent);
    }
}

void add_instincts(const Bitzkey& agent, Values& valued, std::span<const int> options, double hunger, double hungriest_young) {
    if (agent.species == Species::Predator) {  // the instinct to hunt, on top of what its brain has learnt
        const double urge = HUNTING_INSTINCT * std::min(1.0, hunger / 100);
        valued[Hunt] += urge;
        valued[Attack] += urge;
        valued[Feast] += BLOODLUST;  // bloodlust: a fresh kill in sight is hard to pass by
    }
    const double call = agent.species == Species::Predator  ? MATING_CALL
                        : agent.species == Species::Omnivore ? OMNIVORE_MATING_CALL
                                                             : 0.0;
    if (call > 0 && agent.can_breed()) {
        // The mating call: to breed if it can, else to court a mate it seeks.
        const bool can = std::find(options.begin(), options.end(), Breed) != options.end();
        valued[can ? Breed : Court] += call;
    }
    const double scavenger = agent.scavenger();
    valued[Scavenge] += CARRION_CRAVING * scavenger;  // a carrion-eater's craving
    valued[Hunt] -= CARRION_AVERSION * scavenger;      // and its aversion to killing for itself
    valued[Attack] -= CARRION_AVERSION * scavenger;
    valued[Herd] += LOYALTY_PULL * agent.feeling(Loyalty);  // loyalty: keeping to its herd
    if (agent.infant()) valued[Herd] += INFANT_HERDING;      // an infant keeps close to its kind
    if (std::find(options.begin(), options.end(), FeedYoung) != options.end()) {
        // The instinct to feed its hungry young: closing on the best of its other options, however much those are worth.
        double best = -1e300;
        for (const int option : options) {
            if (option != FeedYoung) best = std::max(best, valued[option]);
        }
        const double gap = options.size() > 1 ? std::max(0.0, best - valued[FeedYoung]) : 0.0;
        valued[FeedYoung] += PARENTAL_INSTINCT * (0.5 + agent.feeling(Empathy)) * hungriest_young / 100 * gap;
    }
}

void Simulation::feed_young(Bitzkey& parent, const std::vector<Bitzkey*>& young) {
    // Its infants within its sight are fed full, at no cost to it, and it is glad of it, the more the hungrier they
    // were. Its young may lie in other tiles, so a parent feeds them once every tile has acted.
    parent.last_action = "fed its young";
    if (acting_) {
        acting_->feeding.emplace_back(&parent, young);
        return;
    }
    for (Bitzkey* child : young) {
        if (!child->alive()) continue;
        parent.reward += FEEDING_JOY * (0.5 + parent.feeling(Empathy)) * std::min(100.0, child->hunger) / 100;
        child->hunger = 0;
    }
}

void Simulation::alert(Bitzkey& agent, const Bitzkey& about) {
    // Its herd within its sight: a grazer alerts them to the hunter it sees, so they may flee it from further
    // than they would; a hunter to the fresh kill it sees, so they may come to feast on it. A herd-mate heeds
    // it only from TRUST_REACH times its trust times its own sight away, or nearer.
    if (acting_) {
        acting_->alerts.emplace_back(&agent, &about);
        return;
    }
    bool heeded = false;
    for (Bitzkey* other : around(agent, agent.sight)) {
        if (!herd_mate(agent, *other) ||
            distance(other->x, other->y, agent.x, agent.y) > TRUST_REACH * other->feeling(Trust) * other->sight) {
            continue;
        }
        heeded = true;
        if (agent.species == Species::Grazer) {
            if (!other->alarm) other->alarm = about.identifier;
        } else {
            other->kill_alert = Cell{about.x, about.y};
        }
    }
    if (heeded) agent.reward += ALERT_JOY;  // a pleasure only when a herd-mate took it in
}

void Simulation::scavenge(Bitzkey& agent, Bitzkey& corpse) {
    // Go to the carrion and, once alongside, eat a portion, its share of the meat left; a carrion-eater's
    // gut gets the most from it. The last portion eaten, the body is gone.
    move_toward(agent, corpse.x, corpse.y);
    if (distance(corpse.x, corpse.y, agent.x, agent.y) > 1 || corpse.portions <= 0) {
        agent.last_action = "sought carrion";
        return;
    }
    const double bite = corpse.meat / corpse.portions;
    corpse.meat -= bite;
    agent.nourish(bite * agent.digestion, true);
    agent.last_action = "scavenged";
    eat_portion(corpse);
}

void Simulation::feast(Bitzkey& agent, Bitzkey& corpse) {
    // Go to the fresh kill and, once alongside, eat a portion of it: a whole feast, FRESH_KILL_MEALS times
    // the meal of making the kill, so a large body is many feasts. It isn't scavenging; the carrion left
    // shrinks by the portion's share. The last portion eaten, the body is gone.
    move_toward(agent, corpse.x, corpse.y);
    if (distance(corpse.x, corpse.y, agent.x, agent.y) > 1 || !fresh(corpse) || corpse.meat <= 0 || corpse.portions <= 0) {
        agent.last_action = "sought a kill";  // out of reach, or already eaten
        return;
    }
    agent.nourish(FRESH_KILL_MEALS * (KILL_MEAL + KILL_MEAL_FOR_SIZE * corpse.physique(Size)), true);
    if (agent.species == Species::Predator) agent.reward += FEAST_JOY;
    corpse.meat -= corpse.meat / corpse.portions;
    agent.last_action = "feasted";
    eat_portion(corpse);
}

void Simulation::eat_portion(Bitzkey& corpse) {
    if (--corpse.portions > 0) return;
    corpse.meat = 0.0;
    corpse.died_turn = turn_ - CORPSE_TURNS;
    if (!in_bounds(corpse.x, corpse.y)) return;
    const std::size_t cell = static_cast<std::size_t>(corpse.y) * width_ + corpse.x;
    for (auto [bodies, count] : {std::pair{&kills_[cell], &kill_count_}, std::pair{&carrion_[cell], &carrion_count_}}) {
        const auto found = std::find(bodies->begin(), bodies->end(), &corpse);
        if (found != bodies->end()) {
            bodies->erase(found);
            if (!acting_) --*count;
            else if (count == &kill_count_) ++acting_->kills_gone;
            else ++acting_->carrion_gone;
        }
    }
}

bool Simulation::fresh(const Bitzkey& corpse) const {
    return corpse.died_turn && turn_ - *corpse.died_turn <= FRESH_KILL_TURNS;
}

double Simulation::defence_share(const Bitzkey& animal) {
    return animal.species == Species::Grazer    ? GRAZER_DEFENCE_WEAPONS
           : animal.species == Species::Omnivore ? OMNIVORE_DEFENCE_WEAPONS
                                                 : 1.0;
}

double Simulation::damage(const Bitzkey& attacker, const Bitzkey& defender, bool defending) {
    if (attacker.species == Species::Plant) {
        const double thorns = THORN_SIZE_SHARE * attacker.physique(Size) + (1 - THORN_SIZE_SHARE) * attacker.physique(Weapons);
        return THORN_FORCE * thorns * (1 - 0.6 * defender.toughness);
    }
    const double force = STRIKE_FORCE * (2 + 9 * attacker.might);
    const double weapons = armed(attacker) * (defending ? defence_share(attacker) : 1.0);
    return force * (1 + 0.75 * weapons) * (1 - 0.6 * defender.toughness);
}

double Simulation::armed(const Bitzkey& animal) {
    const double share = animal.species == Species::Predator  ? 1.0
                         : animal.species == Species::Omnivore ? 0.75
                         : animal.species == Species::Grazer   ? 0.5
                                                               : 0.0;
    return animal.physique(Weapons) * (share > 0 ? PREDATOR_WEAPONS * share : 1.0);
}

void Simulation::attack(Bitzkey& agent, Bitzkey& victim, std::string_view activity) {
    if (victim.species != Species::Plant && SMALL_HIDING > 0 && victim.body[Size] < TRAIT_MAX) {  // a small animal may slip away
        const double smallness = 1 - victim.physique(Size);
        if (rng().random() < SMALL_HIDING * smallness * smallness) {
            agent.last_action = "missed";
            exhausted(agent);
            return;
        }
    }
    const double empathy = agent.feeling(Empathy);
    const double joy = FIGHT_JOY * agent.feeling(Aggression) * (1 - 0.6 * empathy) * (1 - 0.75 * agent.feeling(Tolerance));
    double blow = damage(agent, victim);
    for (const Bitzkey* other : around(victim, 1)) {
        if (other != &agent && herd_mate(agent, *other)) {
            blow *= 1 + PACK_BONUS * agent.herd_instinct();  // hunting as a pack
            break;
        }
    }
    victim.health -= blow;
    agent.nourish(5, true);
    agent.hunger += STRIKE_EFFORT * blow / agent.stomach;  // the harder the blow, the more it costs
    agent.reward += joy;
    agent.last_action = "attacked";
    experience(agent, Hit);
    const bool killed = !victim.alive();
    if (!killed) {
        victim.last_action = "wounded";
        struck(victim, agent);
        experience(victim, Wounded);
        const auto close_by = nearby(victim, 2);
        if (std::none_of(close_by.begin(), close_by.end(), [&](const Bitzkey* other) { return other->species == victim.species; })) {
            experience(victim, WoundedAlone);
        }
    } else {
        const double meal = KILL_MEAL + KILL_MEAL_FOR_SIZE * victim.physique(Size);
        agent.nourish(meal, true);
        agent.reward += 2 * joy;
        witness(agent, meal / agent.stomach / 100);  // its pack sees it succeed: the fullness it gained
        if (rival_weight(agent, victim) > 0) {  // one fewer rival for the feeding ground
            agent.reward += RIVAL_KILL_JOY * agent.rivalry * (1 - 0.5 * agent.feeling(Tolerance));
        }
        experience(agent, Kill);
    }
    const double pain = EMPATHY_PAIN * (killed ? 2 : 1);
    agent.reward -= pain * empathy * (same_kind(agent, victim) ? 1.0 : OTHER_KIND);
    for (Bitzkey* witness : around(victim, 2)) {
        if (witness != &agent && witness->brain) {
            const double kinship = witness->species == victim.species ? 1.0 : OTHER_KIND;
            witness->reward -= pain * witness->feeling(Empathy) * kinship;
        }
    }
    if (killed) die(victim, "killed", &agent, activity);

    const double sting = (victim.species == Species::Omnivore ? 0.0  // an omnivore's weapons are for fighting, not spines
                                                              : 4.5 * armed(victim) * defence_share(victim) * (1 - 0.6 * agent.toughness)) *
                         (agent.species == Species::Predator ? PREDATOR_STING : 1.0);  // a predator takes spines half as hard
    if (sting > 0) {
        agent.health -= sting;
        experience(agent, Stung);
        if (!agent.alive()) die(agent, "killed", &victim, "spines");  // its victim's spines
    }
    exhausted(agent);
    if (agent.species == Species::Predator && agent.alive() && blow > std::max(0.0, sting)) agent.reward += ATTACK_JOY;  // a blow that went its way
}

void Simulation::exhausted(Bitzkey& agent) {
    // A blow's effort, or a long way gone, can leave an animal starved: it dies there, rather than
    // lingering as a body whose death was never recorded.
    if (!agent.alive() && !agent.died_turn) die(agent, "exhausted");
}

void Simulation::defend(Bitzkey& agent, Bitzkey& assailant, bool for_herd) {
    const double blow = damage(agent, assailant, true);
    assailant.health -= blow;
    agent.hunger += STRIKE_EFFORT * blow / agent.stomach;
    experience(assailant, Stung);
    if (agent.species != Species::Plant) {
        agent.reward += FIGHT_JOY * agent.feeling(Aggression) * (1 - 0.6 * agent.feeling(Empathy)) *
                        (1 - 0.75 * agent.feeling(Tolerance));  // as attacking is
        if (for_herd) agent.reward += HERD_DEFEND_JOY * agent.herd_instinct();
        agent.last_action = "defended";
        experience(agent, Defended);
    }
    if (!assailant.alive()) {
        die(assailant, "killed", &agent, "defending");
    } else if (assailant.species != Species::Plant) {
        struck(assailant, agent);
    }
    exhausted(agent);
}

void Simulation::struck(Bitzkey& victim, const Bitzkey& striker) {
    victim.assailant = striker.identifier;
    victim.danger_memory = Place{victim.x, victim.y, turn_};
    int herd_near = 0;
    for (Bitzkey* other : around(victim, 2)) {
        if (other == &striker || !other->brain) continue;
        herd_near += herd_mate(victim, *other);
        if (herd_mate(*other, victim)) {
            other->alarm = striker.identifier;
            if (distance(other->x, other->y, striker.x, striker.y) <= 1) other->herd_assailant = striker.identifier;
        }
    }
    const double unhurt = PANIC[0], dying = PANIC[1];
    const double hurt = 1 - std::max(0.0, victim.health) / victim.max_health;
    const double steadied = 1 - HERD_SAFETY * victim.herd_instinct() * std::min(3, herd_near) / 3;
    const bool coward = victim.species == Species::Omnivore && COWARDLY_OMNIVORES != 0;  // an omnivore struck always breaks
    const double chance = std::max(victim.fearfulness() * (unhurt + (dying - unhurt) * hurt) * steadied,
                                   CARRION_COWARDICE * victim.scavenger());  // and a carrion-eater, the more so the more it is one
    if (coward || rng().random() < chance) {
        victim.panicked = true;
        experience(victim, Panicked);
    }
}

bool Simulation::primal_fear(Bitzkey& agent, const Bitzkey* hunter) {
    if (!hunter || agent.species == Species::Plant) {
        agent.primal_fear = false;
        return false;
    }
    if (!agent.primal_fear) {
        const double lopsided = disparity(agent, *hunter);
        if (lopsided > 0) {
            const double chance =
                PRIMAL_FEAR * lopsided * (agent.species == Species::Predator ? PRIMAL_FEAR_PREDATORS : 1.0) * agent.unafraid();
            if (rng().random() < chance) {
                agent.primal_fear = true;
                experience(agent, Panicked);
            }
        }
    }
    return agent.primal_fear;
}

double Simulation::disparity(const Bitzkey& agent, const Bitzkey& opponent) {
    const double mine = looming_damage(agent, opponent) / std::max(1.0, looming_health(opponent));
    const double theirs = looming_damage(opponent, agent) / std::max(1.0, looming_health(agent));
    return theirs <= mine ? 0.0 : 1 - mine / theirs;
}

namespace {
// Size and strength, as shares of their full range, as they count in sizing up a fight.
std::pair<double, double> looming(const Bitzkey& animal) {
    const double more = animal.species == Species::Predator ? static_cast<double>(PREDATOR_LOOMS) / TRAIT_MAX : 0.0;
    return {animal.physique(Size) + more, animal.physique(Strength) + more};
}
}  // namespace

double Simulation::looming_damage(const Bitzkey& attacker, const Bitzkey& defender) {
    const auto [size, strength] = looming(attacker);
    const double defender_size = looming(defender).first;
    const double might = (1 - SIZE_IN_MIGHT) * strength + SIZE_IN_MIGHT * size;
    const double toughness = (1 - SIZE_IN_MIGHT) * defender.physique(Armour) + SIZE_IN_MIGHT * defender_size;
    return STRIKE_FORCE * (2 + 9 * might) * (1 + 0.75 * armed(attacker)) * (1 - 0.6 * toughness);
}

double Simulation::looming_health(const Bitzkey& animal) {
    return animal.health * (60.0 + 90 * looming(animal).first) / animal.max_health;
}

void Simulation::breed(Bitzkey& first, Bitzkey& second) {
    // The parent that chose to breed conceives the litter and carries it for its gestation.
    const std::array<std::pair<int, int>, 2> parents = {std::pair{first.trait(Litter), first.trait(Size)},
                                                        std::pair{second.trait(Litter), second.trait(Size)}};
    int litter = litter_size(parents);
    if (CROWDING_LIMIT) litter = std::max(1, static_cast<int>(litter * (1 - static_cast<double>(crowding(first, second, true)) / CROWDING_LIMIT) + 0.5));  // its own kind
    if (const double needed = food_needed(first); needed > 0) {  // litters follow food only while food limits breeding
        litter = std::max(1, static_cast<int>(litter * std::min(1.0, food_supply(first) / (2 * needed)) + 0.5));
    }
    // A grazer's litters follow the plants in its home band, a predator's the prey, an omnivore's both, half each.
    litter = std::max(1, static_cast<int>(litter * home_fed(first) + 0.5));
    if (NEWBORN_FOOD) {  // only as many young as the parents could feed, rather than many born starving
        // What the parents could give, BREEDING_COST_MOST of each stomach (or what each has left), whatever they pay:
        // neither a fertile parent's cheap breeding nor a hunter's halved cost shrinks its litters.
        const std::array<double, 2> could = {std::min(BREEDING_COST_MOST, std::max(0.0, 100 - first.hunger)) * first.stomach,
                                             std::min(BREEDING_COST_MOST, std::max(0.0, 100 - second.hunger)) * second.stomach};
        const double could_give = py_sum(could);
        const double stomach = (first.stomach + second.stomach) / 2;
        litter = std::max(1, std::min(litter, static_cast<int>(could_give / (NEWBORN_FOOD * stomach))));
    }
    litter = static_cast<int>(std::max(1.0, std::min(static_cast<double>(litter), room())));
    // Each parent pays its breeding cost, and at least its half of what its young need, NEWBORN_FOOD of a stomach each,
    // so a large litter costs what it is worth rather than being born starving.
    const std::array<double, 2> costs = {std::max(first.breeding_price(), litter * NEWBORN_FOOD / 2),
                                         std::max(second.breeding_price(), litter * NEWBORN_FOOD / 2)};
    const std::array<double, 2> gifts = {std::min(costs[0], std::max(0.0, 100 - first.hunger)) * first.stomach,
                                         std::min(costs[1], std::max(0.0, 100 - second.hunger)) * second.stomach};
    const double given = py_sum(gifts);
    const int generation = std::max(first.generation, second.generation) + 1;
    Bitzkey* couple[] = {&first, &second};
    sync(couple);  // their brains as they now are, to inherit from
    for (int young = 0; young < litter; ++young) {
        // The child's mind starts at the average of its parents' current minds, mutated, and its
        // dot is written from that mind, so experience is inherited and shows. Its brain, a mix of
        // its parents', is as deep as its own inborn intellect makes it.
        const Mind mind = inherit_mind(first.mind, second.mind, rng(), mind_mutation_);
        Dna dna = evolved(first.dna, cross(first.dna, second.dna, rng(), mind, MIND_MUTATION_CHANCE, body_mutation_, has_sea()));
        if (first.species == Species::Predator || second.species == Species::Predator) {
            dna = predator_dna(dna);  // a predator's young are predators, whatever crossing over or a mutation made
        }
        const auto [depth, width] = brain_shape(dna);
        auto brain = std::make_shared<Brain>();  // made in conceive(), with the turn's other young
        const bool born_hunter = static_cast<Species>(decode_body(dna)[SpeciesTrait]) == Species::Predator && first.species != Species::Predator;
        (acting_ ? acting_->conceptions : conceptions_).push_back({brain, first.brain, second.brain, rng().getrandbits(64), depth, width, born_hunter});
        first.womb.push_back({dna, mind, std::move(brain), given / litter, generation, second.identifier});
    }
    first.due_turn = turn_ + gestation(first.trait(Size));
    const std::array<Bitzkey*, 2> pair = {&first, &second};
    for (int index = 0; index < 2; ++index) {
        Bitzkey& parent = *pair[index];
        parent.hunger += costs[index];
        const double rest = parent.species == Species::Predator ? PREDATOR_BREED_COOLDOWN : BREED_COOLDOWN;
        parent.cooldown = static_cast<int>(py_round((60 - 45 * parent.physique(Fertility)) * rest *
                                                    (parent.terrain == Terrain::Amphibious ? AMPHIBIOUS_COOLDOWN : 1.0)));
        parent.children += litter;
        parent.stamina = std::max(0.0, parent.stamina - fatigue(parent, BREED_FATIGUE));
        parent.reward += mating_joy(parent, BREED_JOY);
        if (!parent.alive()) {
            die(parent, "bred to death");
        } else {
            experience(parent, parent.hunger >= 75 ? BredIntoHunger : Bred);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Space

void Simulation::Around::iterator::settle() {
    const std::vector<Offset>& offsets = nearby_offsets(around_->radius_);
    while (offset_ < offsets.size()) {
        if (!list_) {
            list_ = around_->simulation_.cell_if(around_->cell_.x + offsets[offset_].dx, around_->cell_.y + offsets[offset_].dy);
            inner_ = 0;
            if (!list_) {
                ++offset_;
                continue;
            }
        }
        for (; inner_ < list_->size(); ++inner_) {
            const Bitzkey* other = (*list_)[inner_];
            if (other != around_->except_ && other->alive()) return;
        }
        list_ = nullptr;
        ++offset_;
    }
    inner_ = 0;
}

Simulation::Around::iterator Simulation::Around::begin() const {
    iterator first(*this, 0);
    first.settle();
    return first;
}

Simulation::Around::iterator Simulation::Around::end() const { return iterator(*this, nearby_offsets(radius_).size()); }

std::vector<Bitzkey*> Simulation::nearby_cell(Cell cell, int radius, const Bitzkey* except) const {
    std::vector<Bitzkey*> found;
    for (const Offset& offset : nearby_offsets(radius)) {
        const auto* list = cell_if(cell.x + offset.dx, cell.y + offset.dy);
        if (!list) continue;
        for (Bitzkey* other : *list) {
            if (other != except && other->alive()) found.push_back(other);
        }
    }
    return found;
}

Bitzkey* Simulation::find_agent(int identifier) const {
    // Among those indexed, by their identifiers alone; then among those born since, by their own.
    const std::size_t indexed = identifiers_.size() <= agents_.size() ? identifiers_.size() : 0;
    const auto among = std::lower_bound(identifiers_.begin(), identifiers_.begin() + static_cast<std::ptrdiff_t>(indexed), identifier);
    if (among != identifiers_.begin() + static_cast<std::ptrdiff_t>(indexed) && *among == identifier) {
        Bitzkey* agent = agents_[static_cast<std::size_t>(among - identifiers_.begin())].get();
        if (agent && agent->identifier == identifier) return agent;
    }
    const auto found = std::lower_bound(agents_.begin() + static_cast<std::ptrdiff_t>(indexed), agents_.end(), identifier,
                                        [](const std::unique_ptr<Bitzkey>& agent, int wanted) { return agent->identifier < wanted; });
    return found != agents_.end() && (*found)->identifier == identifier ? found->get() : nullptr;
}

std::vector<Bitzkey*> Simulation::nearby(const Bitzkey& agent, int radius) const {
    return nearby_cell({agent.x, agent.y}, radius, &agent);
}

double Simulation::room() const {
    if (!population_cap_) return std::numeric_limits<double>::infinity();
    return static_cast<double>(*population_cap_ - population_);
}

int Simulation::crowding(const Bitzkey& agent, const Bitzkey& partner, bool own_kind) const {
    int count = 0;
    for (const Bitzkey* other : around(agent, agent.sight)) {  // within its sight
        count += other->species != Species::Plant && other != &partner && (!own_kind || same_kind(agent, *other));
    }
    return count;
}

void Simulation::conceive() {
    std::for_each(std::execution::par, conceptions_.begin(), conceptions_.end(), [](const Conception& young) {
        Brain& brain = *young.into;
        brain = Brain::inherit(*young.first, *young.second, young.seed, young.depth, young.width);
        if (young.born_hunter) {
            // Born a predator of other parents: what they learnt of eating and uprooting it brings to
            // hunting and feasting.
            for (const auto& [learnt, now] : {std::pair{Eat, Hunt}, std::pair{Uproot, Feast}}) {
                brain.output_weights[static_cast<std::size_t>(now)] = brain.output_weights[static_cast<std::size_t>(learnt)];
                brain.output_biases[static_cast<std::size_t>(now)] = brain.output_biases[static_cast<std::size_t>(learnt)];
            }
        }
    });
    conceptions_.clear();
}

void Simulation::give_birth(Bitzkey& mother) {
    conceive();  // young conceived outside a turn's thinking, as in a test
    // In the squares nearest it with room for each, of land or sea as each may live in, its own first and several to a
    // square while there is room, as many as the population cap allows; the rest are lost.
    int born = 0;
    for (Bitzkey::Young& young : mother.womb) {
        if (born >= room()) break;
        const int space = 1 + decode_body(young.dna)[Size];
        const Terrain terrain = decode_terrain(young.dna);
        std::optional<Cell> found;
        for (const Cell& cell : squares_near(mother)) {
            if (suits(terrain, cell) && room_in(cell) >= space) {
                found = cell;
                break;
            }
        }
        if (!found) continue;  // no room left near it
        Bitzkey& child = add_agent(young.dna, found->x, found->y, std::move(young.brain));
        child.mother = mother.identifier;
        child.father = young.father;
        child.mind = young.mind;
        child.hunger = std::max(0.0, 100 - young.energy / child.stomach);
        child.generation = young.generation;
        population_ += 1;
        born += 1;
    }
    mother.womb.clear();
    mother.due_turn.reset();
}

std::vector<Cell> Simulation::squares_near(const Bitzkey& agent) const {
    std::vector<Cell> cells{Cell{agent.x, agent.y}};
    for (const Offset& offset : birth_offsets(BIRTH_REACH)) {
        if (agent.y + offset.dy < 0 || agent.y + offset.dy >= height_) continue;  // beyond the top or bottom of the map
        cells.push_back(Cell{py_mod(agent.x + offset.dx, width_), agent.y + offset.dy});
    }
    return cells;
}

std::vector<Cell> Simulation::free_cells_near(const Bitzkey& agent) const {
    std::vector<Cell> cells;
    for (const Cell& cell : squares_near(agent)) {
        if (can_enter(agent, cell) && room_in(cell) > 0) cells.push_back(cell);
    }
    return cells;
}

int Simulation::room_in(Cell cell, const Bitzkey* leaving) const {
    int used = 0;
    for (const Bitzkey* other : cells_[static_cast<std::size_t>(cell.y) * width_ + cell.x]) {
        if (other->alive() && other != leaving) used += other->space();
    }
    return CELL_SPACE - used;
}

std::optional<Cell> Simulation::spot_near(Cell cell, int space, Terrain terrain) const {
    const int x = py_mod(cell.x, width_), y = within(cell.y);
    if (suits(terrain, Cell{x, y}) && room_in(Cell{x, y}) >= space) return Cell{x, y};
    for (const Offset& offset : birth_offsets(BIRTH_REACH)) {
        if (y + offset.dy < 0 || y + offset.dy >= height_) continue;
        const Cell near{py_mod(x + offset.dx, width_), y + offset.dy};
        if (suits(terrain, near) && room_in(near) >= space) return near;
    }
    return std::nullopt;
}

bool Simulation::step(Bitzkey& agent, int x, int y) {
    if ((x != agent.x || y != agent.y) && (!can_enter(agent, Cell{x, y}) || room_in(Cell{x, y}, &agent) < agent.space())) {
        return false;  // full, or sea (or land) it can't go into: it stops short
    }
    move(agent, x, y);
    return true;
}

Simulation::Side Simulation::tie_side(const Bitzkey& agent) const {
    const int turns = agent.identifier + turn_;
    return {turns & 1 ? -1 : 1, turns & 2 ? -1 : 1};
}

std::optional<Cell> Simulation::nearest_grass(const Bitzkey& agent, int sight) const {
    const double worst = agent.climate_steps(in_bounds(agent.x, agent.y) ? temperature(agent.y) : Medium);
    const Side side = tie_side(agent);
    for (const Offset& offset : SIGHT_WITHIN[static_cast<std::size_t>(sight)]) {
        const int x = agent.x + side.x * offset.dx, y = agent.y + side.y * offset.dy;
        if (grass_at(x, y) > 0 && can_enter(agent, Cell{x, y}) && agent.climate_steps(temperature(y)) <= worst) return Cell{x, y};
    }
    return std::nullopt;
}

double Simulation::warmth(const Bitzkey& agent, int y) const {
    return std::max(-2.0, std::min(2.0, agent.climate.temperature - temperature(y))) / 2;
}

Bitzkey* Simulation::nearest_carrion(const Bitzkey& agent, int sight) const {
    if (carrion_count_ == 0) return nullptr;
    const Side side = tie_side(agent);
    for (const Offset& offset : SEEN_WITHIN[static_cast<std::size_t>(sight)]) {
        const int x = agent.x + side.x * offset.dx, y = agent.y + side.y * offset.dy;
        if (!in_bounds(x, y)) continue;
        for (Bitzkey* corpse : carrion_[static_cast<std::size_t>(y) * width_ + x]) {
            if (corpse->meat > 0 && agent.can_be(corpse->wet)) return corpse;
        }
    }
    return nullptr;
}

Bitzkey* Simulation::nearest_kill(const Bitzkey& agent, int sight) const {
    if (kill_count_ == 0) return nullptr;
    const Side side = tie_side(agent);
    for (const Offset& offset : SEEN_WITHIN[static_cast<std::size_t>(sight)]) {
        const int x = agent.x + side.x * offset.dx, y = agent.y + side.y * offset.dy;
        if (!in_bounds(x, y)) continue;
        for (Bitzkey* corpse : kills_[static_cast<std::size_t>(y) * width_ + x]) {
            if (feeds_on(agent, *corpse) && agent.can_be(corpse->wet)) return corpse;
        }
    }
    return nullptr;
}

Simulation::Seen Simulation::look_around(const Bitzkey& agent, int sight, int prey_sight) const {
    Seen seen;
    if (prey_sight < 0) prey_sight = sight;
    const bool hunts = agent.species == Species::Omnivore || agent.species == Species::Predator;
    const bool hunted = agent.species != Species::Plant;
    const bool browses = agent.species == Species::Grazer || agent.species == Species::Omnivore;
    const bool courts = agent.can_breed();
    const Side side = tie_side(agent);
    for (const Offset& offset : SEEN_WITHIN[static_cast<std::size_t>(std::max(sight, prey_sight))]) {
        const bool far = std::abs(offset.dx) + std::abs(offset.dy) > sight;  // beyond its sight but for prey
        if ((seen.quarry || !hunts) && (far || ((seen.hunter || !hunted) && (seen.suitor || !courts) && (seen.flora || !browses)))) break;
        const auto* list = cell_if(agent.x + side.x * offset.dx, agent.y + side.y * offset.dy);
        if (!list) continue;
        for (Bitzkey* other : *list) {
            if (other == &agent || !other->alive()) continue;
            if (!seen.quarry && is_prey(agent, *other)) {
                seen.quarry = other;
            } else if (far) {
                continue;
            } else if (!seen.hunter && is_prey(*other, agent)) {
                seen.hunter = other;
            } else if (browses && !seen.flora && other->species == Species::Plant && !other->cooldown && agent.can_be(other->wet)) {
                seen.flora = other;
            } else if (courts && !seen.suitor && other->can_breed() && compatible(agent, *other)) {
                seen.suitor = other;
            }
        }
    }
    return seen;
}

void Simulation::move(Bitzkey& agent, int x, int y) {
    // Each cell left behind on the way, the shorter way round east or west; a move goes straight or
    // diagonally, so every cell on the way is a whole one.
    int dx = x - agent.x;
    const int dy = y - agent.y;
    if (dx > width_ / 2) dx -= width_;
    else if (dx < -(width_ / 2)) dx += width_;
    const int cells = std::max(std::abs(dx), std::abs(dy));
    for (int step = 0; step < cells; ++step) {
        agent.trail.emplace_back(py_mod(agent.x + dx * step / cells, width_), agent.y + dy * step / cells);
    }
    if (in_bounds(agent.x, agent.y)) {
        auto& from = cell_at(agent.x, agent.y);
        const auto found = std::find(from.begin(), from.end(), &agent);
        if (found != from.end()) from.erase(found);
    }
    agent.x = x;
    agent.y = y;
    agent.wet = sea(x, y);
    if (in_bounds(x, y)) cell_at(x, y).push_back(&agent);
}

void Simulation::move_toward(Bitzkey& agent, int x, int y, int steps) {
    if (steps < 0) steps = agent.pace();
    int cells = 0;
    for (int taken = 0; taken < steps; ++taken) {
        bool moved;
        if (agent.x != x) {
            moved = step(agent, agent.x + (x > agent.x ? 1 : -1), agent.y);
        } else if (agent.y != y) {
            moved = step(agent, agent.x, agent.y + (y > agent.y ? 1 : -1));
        } else {
            break;
        }
        if (!moved) break;
        ++cells;
    }
    effort(agent, cells);
}

void Simulation::move_away(Bitzkey& agent, const Bitzkey& threat) {
    int cells = 0;
    for (int taken = 0, pace = agent.pace(); taken < pace; ++taken) {
        const Side side = tie_side(agent);  // straight in line with it, either way
        const int dx = agent.x == threat.x ? side.x : agent.x > threat.x ? 1 : -1;
        const int dy = agent.y == threat.y ? side.y : agent.y > threat.y ? 1 : -1;
        if (!step(agent, py_mod(agent.x + dx, width_), within(agent.y + dy))) break;
        ++cells;
    }
    effort(agent, cells);
}

bool Simulation::compatible(const Bitzkey& first, const Bitzkey& second) {
    return first.species != Species::Plant && first.species == second.species && willing(first, second) &&
           willing(second, first);
}

bool Simulation::willing(const Bitzkey& agent, const Bitzkey& other) {
    const double tolerance = agent.feeling(Tolerance);
    const double accepted = agent.species == Species::Predator ? KIND_DIFFERENCE : MATE_RING_DIFFERENCE;  // a predator its kind
    return ring_difference(agent.dna, other.dna) <= accepted + (1 - accepted) * tolerance &&
           dot_difference(agent.dna, other.dna) <= MATE_DOT_DIFFERENCE + (1 - MATE_DOT_DIFFERENCE) * tolerance &&
           social_difference(agent.dna, other.dna) <= 1 - KINSHIP_SHUN * agent.feeling(Kinship) * (1 - tolerance);  // its kin
}

bool Simulation::same_kind(const Bitzkey& first, const Bitzkey& second) {
    return first.species == second.species &&
           (first.species != Species::Predator || ring_difference(first.dna, second.dna) <= KIND_DIFFERENCE);
}

double Simulation::cannibal_reluctance(const Bitzkey& animal) {
    return (animal.feeling(Empathy) + animal.feeling(Tolerance) + animal.feeling(Patience)) / 3;
}

double Simulation::rival_weight(const Bitzkey& hunter, const Bitzkey& other) {
    // Only its own species: predators for a predator, omnivores for an omnivore. Other species are prey.
    const bool hunts = hunter.species == Species::Predator || hunter.species == Species::Omnivore;
    return hunts && other.species == hunter.species ? 1.0 : 0.0;
}

void Simulation::witness(const Bitzkey& actor, double outcome) {
    if (!actor.has_last_senses) return;
    if (acting_) {
        acting_->witnessed.emplace_back(&actor, outcome);
        return;
    }
    for (const Offset& offset : nearby_offsets(MAX_SIGHT)) {  // as nearby() would list them, in the same order
        const auto* list = cell_if(actor.x + offset.dx, actor.y + offset.dy);
        if (!list) continue;
        for (Bitzkey* observer : *list) {
            if (observer == &actor || !observer->alive() || !observer->brain || !society(*observer, actor) ||
                distance(observer->x, observer->y, actor.x, actor.y) > observer->sight) {
                continue;
            }
            const double heed = observer->feeling(Empathy) * observer->intelligence() *
                                (1 + TRUST_HEED * (2 * observer->feeling(Trust) - 1));  // the trusting take in more
            const double heft = outcome <= -DEATH_PENALTY ? FATAL_LESSON : 1.0;  // a death seen teaches hard
            witnessed_.push_back({observer, actor.last_senses, actor.last_choice, outcome, LEARNING_RATE * heed * SOCIAL_LEARNING * heft * lesson_weight(actor.last_choice, outcome)});  // learnt at the end
            observer->reward += SOCIAL_MOOD * heed * outcome;
            if (outcome > 0) observer->seen_success = 1.0;
            else observer->seen_failure = 1.0;
        }
    }
}

bool Simulation::society(const Bitzkey& observer, const Bitzkey& actor) const {
    return &observer != &actor && !solitary(observer.species) && observer.species == actor.species &&
           (observer.species != Species::Predator || same_kind(observer, actor)) && willing(observer, actor);
}

bool Simulation::herd_mate(const Bitzkey& agent, const Bitzkey& other) const {
    return &other != &agent && other.alive() && (!solitary(agent.species) || agent.infant()) && other.species == agent.species &&
           (agent.species != Species::Predator || same_kind(agent, other)) && willing(agent, other);
}

Simulation::HerdView Simulation::herd(const Bitzkey& agent) const {
    std::vector<const Bitzkey*> mates;
    for (const Bitzkey* other : around(agent, agent.sight)) {  // its herd: herd-mates within its sight
        if (herd_mate(agent, *other)) mates.push_back(other);
    }
    if (mates.empty()) return {};
    HerdView view{true, 0, std::nullopt};
    long long sum_x = 0, sum_y = 0;
    for (const Bitzkey* other : mates) {
        view.close += distance(other->x, other->y, agent.x, agent.y) <= 2;
        sum_x += other->x;
        sum_y += other->y;
    }
    const double count = static_cast<double>(mates.size());
    view.middle = Cell{static_cast<int>(py_round(static_cast<double>(sum_x) / count)),
                       static_cast<int>(py_round(static_cast<double>(sum_y) / count))};
    return view;
}

std::pair<double, double> Simulation::territory(const Bitzkey& hunter) const {
    const Territory& rule = TERRITORY[hunter.species == Species::Predator ? 0 : 1];
    const double fewest = rule.fewest, most = rule.most, strength = rule.strength;
    const double tolerated = fewest + (most - fewest) * hunter.feeling(Empathy);
    double rivals = 0.0, own = 0.0;
    for (const Bitzkey* other : around(hunter, hunter.sight)) {  // its territory: the cells within its sight
        // A predator's pack-mates count only PACK_RIVALRY each: its pack mentality overcomes rivalry.
        const double pack = hunter.species == Species::Predator && herd_mate(hunter, *other) ? PACK_RIVALRY : 1.0;
        rivals += rival_weight(hunter, *other) * pack;
        own += (other->species == hunter.species) * pack;
    }
    const auto crowded = [&](double count) {
        return strength * std::min(1.0, std::max(0.0, count - tolerated) / TERRITORY_RAMP);
    };
    return {crowded(rivals), crowded(own)};
}

bool Simulation::feeds_on(const Bitzkey& eater, const Bitzkey& other) {
    // A food source, when judging whether to breed: predators live on grazers and omnivores, and
    // omnivores on grazers. Other predators, of any kind, are rivals for that prey rather than a
    // supply of food, though they may be hunted.
    if (eater.species == Species::Predator) {
        return other.species == Species::Grazer || other.species == Species::Omnivore;
    }
    return eater.species == Species::Omnivore && other.species == Species::Grazer;
}

double Simulation::diet_share(const Bitzkey& eater, const Bitzkey& other) {
    // Fully a mouth on its food for its own species (every predator, whatever its kind), half for another
    // species eating some of the same food.
    if (other.species == Species::Plant || feeds_on(eater, other)) return 0.0;
    if (other.species == eater.species) return 1.0;
    const auto grazes = [](Species species) { return species == Species::Grazer || species == Species::Omnivore; };
    if (grazes(eater.species) && grazes(other.species)) return SHARED_DIET;  // both eat grass and plants
    if (eater.species == Species::Omnivore && other.species == Species::Predator) return SHARED_DIET;  // both hunt grazers
    return 0.0;
}

double Simulation::plant_yield(const Bitzkey& plant) const {
    return 1 - (1 - PLANT_CLIMATE_YIELD) * std::min(1.0, plant.climate_steps(in_bounds(plant.x, plant.y) ? temperature(plant.y) : Medium));
}

std::optional<Cell> Simulation::comfort_row(const Bitzkey& agent, int sight) const {
    for (int rows_away = 1; rows_away <= sight; ++rows_away) {
        for (const int y : {agent.y - rows_away, agent.y + rows_away}) {
            if (0 <= y && y < height_ && agent.climate_steps(temperature(y)) < 0.5) return Cell{agent.x, y};
        }
    }
    return std::nullopt;
}

double Simulation::food_needed(const Bitzkey& agent) {
    // The meals a mouth an animal needs about it to breed: 0, none.
    return agent.species == Species::Predator ? PREDATOR_FOOD_NEEDED : FOOD_NEEDED;
}

double Simulation::food_supply(const Bitzkey& agent) const {
    // The meals within FORAGE_RADIUS per mouth sharing them, itself included: grass and plants for
    // those that graze, as well as they digest them, and prey for hunters. Grass is judged by what
    // will grow back over GRASS_OUTLOOK turns where it grows now, and a share of what stands now, so a
    // crowd can't breed on a stock it is about to eat.
    double food = 0.0;
    if (agent.grazing != 0.0) {
        double cells = 0.0;
        int tufts = 0;
        for (const Offset& offset : nearby_offsets(FORAGE_RADIUS)) {
            if (!in_bounds(agent.x + offset.dx, agent.y + offset.dy) || !can_enter(agent, Cell{agent.x + offset.dx, agent.y + offset.dy})) continue;
            cells += growth(agent.y + offset.dy);
            tufts += grass_at(agent.x + offset.dx, agent.y + offset.dy);
        }
        const double growing = grass_rows_.empty() ? 0.0 : width_ * grass_reach_.back();
        const double regrowth = growing ? cells * grass_regrowth_ / growing * GRASS_OUTLOOK : 0.0;
        food += agent.grazing * (STANDING_GRASS * tufts + regrowth);
    }
    double mouths = 1.0;
    for (const Bitzkey* other : around(agent, FORAGE_RADIUS)) {
        if (other->species == Species::Plant) {
            if (agent.can_be(other->wet)) food += PLANT_MEALS * agent.browsing * plant_yield(*other);
        } else if (feeds_on(agent, *other)) {
            food += PREY_MEALS;
        } else {
            double share = diet_share(agent, *other);
            if ((agent.species == Species::Omnivore || agent.species == Species::Predator) && herd_mate(agent, *other)) {
                share *= PACK_MOUTHS;  // a pack shares its kills
            }
            mouths += share;
        }
    }
    return food / mouths;
}

double Simulation::flee_range(const Bitzkey& agent, int herd_near) {
    // How close a hunter must be before fleeing it is worthwhile: less among a herd keeping watch.
    return FLEE_RANGE * (1 - HERD_VIGILANCE * agent.herd_instinct() * std::min(3, herd_near) / 3);
}

std::pair<bool, bool> Simulation::prey_switching(const Bitzkey& hunter) const {
    // Which prey a hunter goes after, grazers and omnivores. A predator always hunts both. An omnivore
    // hunts grazers only while they make up at least SWITCH_SHARE of the food weighed within
    // FORAGE_RADIUS, against plants, or while none of that food is near at all.
    if (hunter.species == Species::Predator) return {true, true};  // grazers are always prey to a predator, and omnivores it would beat
    int grazers = 0, omnivores = 0, plants = 0;
    for (const Bitzkey* other : around(hunter, FORAGE_RADIUS)) {
        grazers += other->species == Species::Grazer;
        omnivores += other->species == Species::Omnivore;
        plants += other->species == Species::Plant;
    }
    const auto common = [](int count, int total) {
        return total == 0 || static_cast<double>(count) / total >= SWITCH_SHARE;
    };
    return {common(grazers, grazers + plants), false};
}

int Simulation::other_food(const Bitzkey& omnivore) const {
    int food = 0;
    for (const Bitzkey* other : around(omnivore, FORAGE_RADIUS)) food += other->species == Species::Grazer || other->species == Species::Plant;
    for (int dx = -FORAGE_RADIUS; dx <= FORAGE_RADIUS; ++dx) {
        const int reach = FORAGE_RADIUS - std::abs(dx);
        for (int dy = -reach; dy <= reach; ++dy) {
            food += grass_at(omnivore.x + dx, omnivore.y + dy) > 0 && can_enter(omnivore, Cell{omnivore.x + dx, omnivore.y + dy});
        }
    }
    return food;
}

bool Simulation::would_win(const Bitzkey& agent, const Bitzkey& opponent) {
    return damage(agent, opponent) / std::max(1.0, opponent.health) >= damage(opponent, agent) / std::max(1.0, agent.health);
}

bool Simulation::within_reach(const Bitzkey& hunter, const Bitzkey& other) {
    if (!hunter.can_be(other.wet)) return false;
    const bool grounded = hunter.terrain == Terrain::Terrestrial || hunter.terrain == Terrain::Aquatic;
    return !(grounded && other.terrain == Terrain::Avian && other.species != Species::Plant && other.last_action != "rested");
}

bool Simulation::is_prey(const Bitzkey& hunter, const Bitzkey& other) {
    if (other.guarded) return false;  // a predator's infant, guarded by a grown one of its kind
    if (!within_reach(hunter, other)) return false;
    if (hunter.species == Species::Omnivore) {
        // Grazers while they are common enough to switch to, and other omnivores only in an
        // overcrowded territory, once hungry past its reluctance as that rivalry wears it down.
        // Predators only when it would win, blow for blow.
        return (other.species == Species::Grazer && hunter.hunts_grazers) ||
               (other.species == Species::Predator && hunter.hunts_predators && would_win(hunter, other)) ||
               (other.species == Species::Omnivore && hunter.own_rivalry > 0 &&
                hunter.hunger / 100 > cannibal_reluctance(hunter) * (1 - RIVALRY_WEAR * hunter.own_rivalry));
    }
    if (hunter.species != Species::Predator) return false;
    if (other.species == Species::Grazer) return hunter.hunts_grazers;
    if (other.species == Species::Omnivore) return hunter.hunts_omnivores && would_win(hunter, other);  // only one it would beat
    if (other.species != Species::Predator) return false;
    // Other kinds of predator only when hungry past its reluctance, which a territory overcrowded with predators
    // wears away, and so does a world glutted with them or running out of prey; its own kind only once that is
    // twice as bad.
    const double reluctance = cannibal_reluctance(hunter) * (1 - RIVALRY_WEAR * hunter.own_rivalry);
    if (same_kind(hunter, other)) {
        const double own = std::min(1.0, std::max(0.0, hunter.glut - 1));
        return own > 0 && hunter.hunger / 100 > reluctance * (1 - own);
    }
    return hunter.hunger / 100 > reluctance * (1 - std::min(1.0, hunter.glut));
}

}  // namespace bitzkey
