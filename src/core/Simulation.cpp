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
    {{Aggression, 0.04}, {Fear, -0.02}, {Intellect, 0.002}},  // kill
    {{Aggression, 0.0067}},                       // hit
    {{Aggression, -0.0133}, {Fear, 0.0133}},      // stung
    {{Fear, 0.0333}, {Aggression, -0.0067}},      // wounded
    {{Sociability, 0.02}},                        // wounded_alone
    {{Libido, 0.02}, {Sociability, 0.0133}, {Intellect, 0.002}},  // bred
    {{Libido, -0.0533}},                          // bred_into_hunger
    {{Appetite, 0.0033}, {Patience, -0.0033}},    // starving
    {{Patience, 0.0067}, {Intellect, 0.0003}},    // foraged_meal
    {{Curiosity, 0.01}, {Intellect, 0.002}},      // discovery
    {{Curiosity, -0.02}, {Fear, 0.0133}},         // ambushed
    {{Fear, -0.0133}, {Aggression, 0.0067}, {Intellect, 0.002}},  // defended
    {{Fear, 0.0067}},                             // panicked
    {{Intellect, 0.0001}},                        // lesson: a choice that turned out well, learnt from
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

// Every offset within MAX_SIGHT, nearest first (by distance, then dx, then dy).
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
std::int64_t seed_offsets() { return static_cast<std::int64_t>(SIGHT_WITHIN[static_cast<std::size_t>(SEED_REACH)].size()); }

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

int zone_temperature(int y, int height) {
    const double place = (y + 0.5) / height;
    if (place < ARCTIC_SHARE || place > 1 - ARCTIC_SHARE) return Cold;
    return std::abs(place - 0.5) < EQUATOR_SHARE / 2 ? Hot : Medium;
}

bool in_winter(int y, int height, int quarter) {
    const double place = (y + 0.5) / height;
    const bool equator = std::abs(place - 0.5) < EQUATOR_SHARE / 2;
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

double season_progress(int turn) {
    const int quarter = season(turn);
    if ((turn < YEAR_LENGTH && quarter == 0) || SEASON_TURNING <= 0) return 1.0;
    const double into = static_cast<double>(turn % YEAR_LENGTH * 4 - quarter * YEAR_LENGTH) / YEAR_LENGTH;  // share of the season gone
    return std::min(1.0, into / SEASON_TURNING);
}

double temperature_at(int y, int height, int turn) {
    const int quarter = season(turn);
    const int now = season_temperature(y, height, quarter);
    const double progress = season_progress(turn);
    if (progress >= 1) return now;
    const int before = season_temperature(y, height, (quarter + 3) % 4);
    if (now == before) return now;
    const auto [band, bands] = wave_band(y, height, quarter);
    const double reach = progress * (bands + WAVE_FRONT);  // how far the front has come, in bands
    const double turned = std::min(1.0, std::max(0.0, (reach - band) / WAVE_FRONT));
    return before + (now - before) * turned;
}

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
    might = (1 - SIZE_IN_MIGHT) * physique(Strength) + SIZE_IN_MIGHT * size;
    toughness = (1 - SIZE_IN_MIGHT) * physique(Armour) + SIZE_IN_MIGHT * size;
    const int base_steps = 1 + trait(Speed) * 3 / TRAIT_MAX;
    steps = species == Species::Grazer ? static_cast<int>(base_steps * GRAZER_SPEED + 0.5) : base_steps;
    grazing = GRAZING[static_cast<std::size_t>(species)];
    browsing = BROWSING[static_cast<std::size_t>(species)];
    sight = 2 + SIGHT_PER_TRAIT * trait(Sight);
    breeding_cost = (25 - 15 * physique(Fertility)) *
                    (species == Species::Omnivore || species == Species::Predator ? HUNTER_BREEDING_COST : 1.0);  // hunters pay less
    digestion = 0.2 + 0.8 * physique(Carrion);
}

double Bitzkey::exposure_share(double here) const {
    const int home = climate.temperature;
    if (species != Species::Plant && ((here > home && climate.cold) || (here < home && climate.heat))) return HARDINESS;
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
    random_.seed(seed);
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
    for (std::size_t group = 0; group < sizes.size(); ++group) species.push_back(static_cast<int>(random_.randrange(4)));
    const std::vector<std::vector<int>> homes = plant_homes(species);
    for (std::size_t group = 0; group < sizes.size(); ++group) {
        spawn_group(make_dna(random_, species[group]), sizes[group], nullptr, std::nullopt, true, homes[group]);
    }
    for (const Imported& bitzkey : settings.imported) {
        spawn_group(bitzkey.dna, static_cast<int>(random_.randint(SPAWN_GROUP[0], SPAWN_GROUP[1])), bitzkey.brain,
                    bitzkey.mind);
    }
    take_census();
    record();
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
        const int size = static_cast<int>(random_.randint(smallest, std::min(largest, remaining - smallest)));
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

void Simulation::spawn_group(Dna dna, int size, const std::shared_ptr<const Brain>& brain,
                             const std::optional<PartialMind>& mind, bool native, const std::vector<int>& given_rows) {
    // In one of the rows given, if any; otherwise, with SPAWN_AT_HOME, in its home zone.
    const std::vector<int> rows = !given_rows.empty() || !SPAWN_AT_HOME ? given_rows : home_rows(decode_climate(dna).temperature);
    const int x = static_cast<int>(random_.randrange(SPAWN_SPREAD, width_ - SPAWN_SPREAD));
    const int y = rows.empty() ? static_cast<int>(random_.randrange(SPAWN_SPREAD, height_ - SPAWN_SPREAD))
                               : rows[static_cast<std::size_t>(random_.randrange(static_cast<std::int64_t>(rows.size())))];
    if (native) {  // tolerance is rare at the start
        for (const std::uint64_t flag : {HEAT_TOLERANCE, COLD_TOLERANCE}) {
            dna.bits = random_.random() < STARTING_TOLERANCE ? dna.bits | flag : dna.bits & ~flag;
        }
    }
    if (native && decode_body(dna)[SpeciesTrait] == static_cast<int>(Species::Plant)) {
        dna.bits = (dna.bits & ~(std::uint64_t{0x3} << TEMPERATURE_SHIFT)) |
                   static_cast<std::uint64_t>(zone_temperature(y, height_)) << TEMPERATURE_SHIFT;
    }
    const int space = 1 + decode_body(dna)[Size];
    for (int member = 0; member < size; ++member) {
        const int dx = static_cast<int>(random_.randint(-SPAWN_SPREAD, SPAWN_SPREAD));
        const int dy = static_cast<int>(random_.randint(-SPAWN_SPREAD, SPAWN_SPREAD));
        if (const auto spot = spot_near(Cell{x + dx, y + dy}, space)) {  // only where there is room
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
        const int depth = depth_for(thinking_intellect(agent->inborn[Intellect], agent->species));  // twice its inborn intellect, at least two
        if (!brain) brain = std::make_shared<Brain>(Brain::random(random_, depth));
        else if (brain->depth() != depth) brain = std::make_shared<Brain>(brain->resized(depth));
        agent->brain = std::move(brain);
        if (gpu_) agent->gpu = gpu_->attach(*agent->brain);
    }
    ++next_identifier_;
    if (lineage_.size() <= static_cast<std::size_t>(agent->identifier)) lineage_.resize(agent->identifier + 1);
    lineage_[agent->identifier] = agent->dna;
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

std::vector<int> Simulation::history_turns() const {
    std::vector<int> turns;
    for (const Snapshot& held : history_) turns.push_back(held.turn);
    return turns;
}

void Simulation::record() {
    // Keep a copy of the newest turn, then let the oldest go while the history holds more than
    // HISTORY_TURNS turns or HISTORY_MEMORY gigabytes.
    frontier_ = turn_;
    if (!history_.empty() && history_.back().turn == turn_) {  // recorded again, as it now is
        history_bytes_ -= history_.back().bytes;
        history_.pop_back();
    }
    if (gpu_) {  // brains change only on the GPUs, so there is no rewinding: the present alone, copied from nothing
        history_.clear();
        history_bytes_ = 0;
        history_.push_back(Snapshot{turn_});
        return;
    }
    Snapshot held{turn_, deaths_, next_identifier_, {}, grass_, seeds_, random_.getstate(), insights_.snapshot(), death_causes_};
    held.agents.reserve(agents_.size());
    for (const auto& agent : agents_) {
        held.agents.push_back(*agent);
        held.bytes += AGENT_BYTES + agent->brain_bytes();
    }
    history_bytes_ += held.bytes;
    history_.push_back(std::move(held));
    while (history_.size() > 1 &&
           (gpu_ || (HISTORY_TURNS < HISTORY_ALL && history_.size() > static_cast<std::size_t>(HISTORY_TURNS)) ||
            static_cast<double>(history_bytes_) > HISTORY_MEMORY * static_cast<double>(std::uintmax_t{1} << 30))) {
        history_bytes_ -= history_.front().bytes;
        history_.pop_front();
    }
}

std::vector<Bitzkey*> Simulation::deploy(const std::vector<Imported>& bitzkeys, int x, int y) {
    if (turn_ < frontier_) {
        while (!history_.empty() && history_.back().turn > turn_) {
            history_bytes_ -= history_.back().bytes;
            history_.pop_back();
        }
        std::erase_if(death_records_, [&](const auto& entry) { return entry.second.turn > turn_; });  // deaths in the future let go
    }
    std::vector<Bitzkey*> deployed;
    for (const Imported& bitzkey : bitzkeys) {
        const int space = 1 + decode_body(bitzkey.dna)[Size];
        for (int member = 0; member < DEPLOY_GROUP; ++member) {
            const int cell_x = x + static_cast<int>(random_.randint(-SPAWN_SPREAD, SPAWN_SPREAD));
            const int cell_y = y + static_cast<int>(random_.randint(-SPAWN_SPREAD, SPAWN_SPREAD));
            if (const auto spot = spot_near(Cell{cell_x, cell_y}, space)) {  // only where there is room
                deployed.push_back(&add_agent(bitzkey.dna, spot->x, spot->y, bitzkey.brain ? std::make_shared<Brain>(*bitzkey.brain) : nullptr,
                                              bitzkey.mind));
            }
        }
    }
    census_.forget_from(turn_);  // this turn again, as it now is
    take_census();
    record();
    return deployed;
}

void Simulation::rules_changed() {
    grass_regrowth_ = initial_grass_ * GRASS_REGROWTH;
    band_of_row_.clear();  // the bands are worked out again as the cells are next indexed
    update_climate();
    for (auto& agent : agents_) agent->derive();
}

void Simulation::restore(const Snapshot& held) {
    turn_ = held.turn;
    deaths_ = held.deaths;
    next_identifier_ = held.next_identifier;
    for (auto& agent : agents_) departed_.push_back(std::move(agent));
    agents_.clear();
    for (const Bitzkey& agent : held.agents) {
        agents_.push_back(std::make_unique<Bitzkey>(agent));
        agents_.back()->derive();  // under the rules as they now stand, which may have changed since
    }
    grass_ = held.grass;
    seeds_ = held.seeds;
    random_.setstate(held.random);
    insights_.restore(held.insights);
    death_causes_ = held.death_causes;
    update_climate();
    index_cells();
}

void Simulation::close() {
    history_.clear();
    history_bytes_ = 0;
}

void Simulation::seek(int target) {
    target = std::max(earliest(), std::min(frontier_, target));
    if (target != turn_) restore(history_[static_cast<std::size_t>(target - earliest())]);
}

bool Simulation::rewind() {
    if (turn_ <= earliest()) return false;
    seek(turn_ - 1);
    return true;
}

void Simulation::advance() {
    if (turn_ < frontier_) {
        seek(turn_ + 1);
        return;
    }
    if (end_reason() != EndReason::None) return;
    take_turn();
    auto clock = std::chrono::steady_clock::now();
    record();
    timings.record += lap(clock);
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
    random_.shuffle(order);
    for (Bitzkey* agent : order) {
        if (!agent->alive()) continue;
        agent->age += 1;
        agent->grow();
        agent->health = std::min(agent->health, agent->max_health);  // growing old, it grows feeble
        const bool plant = agent->species == Species::Plant;
        const double here = in_bounds(agent->x, agent->y) ? climate_[static_cast<std::size_t>(agent->y)] : Medium;
        const double steps = agent->climate_steps(here);
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
        if (felt > 0) agent->health -= CLIMATE_DAMAGE * felt * share * agent->max_health;
        const bool starving = agent->hunger >= 100;
        if (starving) agent->health -= STARVING_DAMAGE * agent->max_health;  // it wastes away
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
        if (agent->due_turn && *agent->due_turn <= turn_) give_birth(*agent);
        agent->seen_success *= SOCIAL_MEMORY;
        agent->seen_failure *= std::pow(SOCIAL_MEMORY, 1 + RESILIENCE * (2 * agent->feeling(Resilience) - 1));  // got over sooner
        settle(*agent);
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
    sow(static_cast<int>(whole) + ((fraction != 0.0 && random_.random() < fraction) ? 1 : 0));
    for (auto& agent : agents_) {
        // no longer fresh by next turn: it rots
        if (agent->died_turn && turn_ - *agent->died_turn >= FRESH_KILL_TURNS) agent->meat = std::max(0.0, agent->meat - agent->rot);
    }
    for (auto& agent : agents_) {
        if (agent->died_turn && turn_ - *agent->died_turn >= CORPSE_TURNS) departed_.push_back(std::move(agent));
    }
    std::erase_if(agents_, [](const std::unique_ptr<Bitzkey>& agent) { return !agent; });
    insights_.decay();
    take_census();
    timings.after += lap(clock);
}

void Simulation::take_census() {
    std::vector<int> identifiers;
    for (const Bitzkey* agent : living()) identifiers.push_back(agent->identifier);
    census_.record(turn_, std::move(identifiers));
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
    for (const auto& agent : agents_) {
        if (!in_bounds(agent->x, agent->y)) continue;
        if (agent->alive()) {
            cell_at(agent->x, agent->y).push_back(agent.get());
            if (agent->species == Species::Plant) {
                ++band_plants_[static_cast<std::size_t>(band_of_row_[static_cast<std::size_t>(agent->y)])];
            } else {
                const auto home = static_cast<std::size_t>(home_band_[static_cast<std::size_t>(agent->climate.temperature)][static_cast<std::size_t>(agent->y)]);
                if (agent->species == Species::Grazer) ++band_grazers_[home];
                else if (agent->species == Species::Predator) ++band_predators_[home];
                if (agent->species == Species::Grazer || agent->species == Species::Omnivore) {  // what predators live on
                    ++band_prey_[static_cast<std::size_t>(band_of_row_[static_cast<std::size_t>(agent->y)])];
                }
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
    departed_.clear();
}

double Simulation::home_prey(const Bitzkey& predator) const {
    if (band_prey_.empty()) return 0.0;  // none before the cells are first indexed
    const auto band = static_cast<std::size_t>(home_band_[static_cast<std::size_t>(predator.climate.temperature)][static_cast<std::size_t>(predator.y)]);
    return static_cast<double>(band_prey_[band]) / std::max(1, band_predators_[band]);
}

double Simulation::home_food(const Bitzkey& agent) const {
    if (agent.species == Species::Grazer) return home_plants(agent);
    return agent.species == Species::Predator ? home_prey(agent) : 0.0;
}

double Simulation::home_food_needed(const Bitzkey& agent) {
    return agent.species == Species::Grazer ? PLANTS_PER_GRAZER : agent.species == Species::Predator ? PREY_PER_PREDATOR : 0.0;
}

double Simulation::home_food_sense(const Bitzkey& agent) const {
    if (agent.species != Species::Grazer && agent.species != Species::Predator) return 0.0;
    const double needed = home_food_needed(agent);
    return needed ? std::min(1.0, home_food(agent) / (2 * needed)) : 1.0;
}

double Simulation::home_plants(const Bitzkey& grazer) const {
    if (band_plants_.empty()) return 0.0;  // none before the cells are first indexed
    const auto band = static_cast<std::size_t>(home_band_[static_cast<std::size_t>(grazer.climate.temperature)][static_cast<std::size_t>(grazer.y)]);
    return static_cast<double>(band_plants_[band]) / std::max(1, band_grazers_[band]);
}

int gestation(int size) {
    return GESTATION[0] + static_cast<int>(static_cast<double>((GESTATION[1] - GESTATION[0]) * size) / TRAIT_MAX + 0.5);
}

void Simulation::seed_plant(Bitzkey& plant) {
    if (plant.hunger > SEED_HUNGER) return;
    // Plants compete for light: the more plants grow within LIGHT_RADIUS, the less likely a plant
    // is to seed, and with LIGHT_LIMIT it doesn't.
    const double least = SEED_CHANCE[0], most = SEED_CHANCE[1];
    const double roll = random_.random();
    const double chance = least + (most - least) * plant.physique(Fertility);
    if (roll >= chance) return;
    int shade = 0;
    for (const Bitzkey* other : nearby(plant, LIGHT_RADIUS)) shade += other->species == Species::Plant;
    if (roll >= chance * std::max(0.0, 1 - static_cast<double>(shade) / LIGHT_LIMIT)) return;
    const Offset offset = SIGHT_OFFSETS[static_cast<std::size_t>(random_.randrange(seed_offsets()))];
    if (plant.y + offset.dy < 0 || plant.y + offset.dy >= height_) return;  // off the top or bottom of the map: the seed is lost
    const Cell cell{py_mod(plant.x + offset.dx, width_), plant.y + offset.dy};
    if (!room_for_seedling(cell)) return;
    const Dna dna = evolved(plant.dna, cross(plant.dna, plant.dna, random_, std::nullopt, mind_mutation_, body_mutation_));
    plant.hunger += SEED_COST;
    plant.children += 1;
    plant.last_action = "seeded";
    if (!plant.alive()) die(plant, "seeded to death");  // it spent the last of its food on the seed
    seeds_.push_back({cell.x, cell.y, dna, plant.generation + 1, turn_});  // it lies PLANT_GESTATION turns before it can sprout
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
    int crowding = 0;
    for (const Bitzkey* other : nearby_cell(cell, 2, nullptr)) crowding += other->species == Species::Plant;
    return crowding <= PLANT_CROWDING;
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
    std::vector<Seed> waiting;
    for (const Seed& seed : seeds_) {
        if (turn_ - seed.sown >= viable) continue;
        if (turn_ - seed.sown >= PLANT_GESTATION &&
            std::abs(climate_[static_cast<std::size_t>(seed.y)] - decode_climate(seed.dna).temperature) < 0.5 &&
            room_for_seedling({seed.x, seed.y})) {
            sprout(seed.dna, {seed.x, seed.y}, seed.generation);
        } else {
            waiting.push_back(seed);
        }
    }
    seeds_ = std::move(waiting);
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
        const int x = static_cast<int>(random_.randrange(width_));
        const auto reached = std::upper_bound(grass_reach_.begin(), grass_reach_.end(), random_.random() * grass_reach_.back());
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
    // How fast its brain learns: with its intellect, boosted further the cleverer it is, and a predator's
    // PREDATOR_LEARNING times as fast, an omnivore's with half that bonus.
    const double sharpness = agent.intelligence();
    const double hunter = agent.species == Species::Predator  ? PREDATOR_LEARNING
                          : agent.species == Species::Omnivore ? 1 + (PREDATOR_LEARNING - 1) / 2
                                                               : 1.0;
    return LEARNING_RATE * sharpness * (1 + LEARNING_BOOST * sharpness) * hunter;
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

void Simulation::learn(Bitzkey& agent, double target, bool replay) {
    // From its own last choice, by TD(lambda), so the choices before it share in what came of it. With
    // `replay`, it then re-learns from its most recent experiences, as many as its intellect allows,
    // and remembers this one. On the GPU, the lessons are queued, to be learnt in one batch.
    const double rate = learning_rate(agent);
    const std::size_t count = replay ? static_cast<std::size_t>(REPLAY_PER_INTELLECT * thinking_intellect(agent.inborn[Intellect], agent.species)) : 0;
    const std::size_t first = agent.memories.size() > count ? agent.memories.size() - count : 0;
    if (gpu_) {
        if (rate > 0) {
            const double decay = discount(agent) * (TRACE_DECAY + (SMART_TRACE_DECAY - TRACE_DECAY) * agent.intelligence());
            queue_lesson(agent, agent.last_senses, agent.last_choice, target, rate * choice_learning(agent.last_choice), true, decay);
            for (std::size_t index = first; count && index < agent.memories.size(); ++index) {
                const Bitzkey::Memory& memory = agent.memories[index];
                queue_lesson(agent, memory.senses, memory.choice, memory.target, rate * choice_learning(memory.choice), false, 0.0);
            }
        }
    } else {
        agent.own_brain().learn_traced(agent.last_senses, agent.last_choice, target, rate * choice_learning(agent.last_choice), discount(agent),
                                       agent.intelligence());
        for (std::size_t index = first; count && index < agent.memories.size(); ++index) {
            const Bitzkey::Memory& memory = agent.memories[index];
            agent.own_brain().learn(memory.senses, memory.choice, memory.target, rate * choice_learning(memory.choice));
        }
    }
    if (!count) return;
    agent.memories.push_back({agent.last_senses, agent.last_choice, target});
    if (agent.memories.size() > count) agent.memories.erase(agent.memories.begin(), agent.memories.end() - static_cast<std::ptrdiff_t>(count));
}

void Simulation::queue_lesson(const Bitzkey& agent, const Senses& senses, int action, double target, double rate, bool traced,
                              double decay) {
    lesson_senses_.push_back(senses);
    lessons_.push_back({agent.gpu, &lesson_senses_.back(), action, target, rate, traced, decay});
}

void Simulation::learn_queued() {
    if (gpu_ && !lessons_.empty()) {
        gpu_->learn(lessons_);
    }
    lessons_.clear();
    lesson_senses_.clear();
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
    if (!handles.empty()) gpu_->download(handles, into);
}

void Simulation::sync_brains() {
    if (!gpu_) return;
    std::vector<gpu::BrainEngine::Handle> handles;
    std::vector<Brain*> into;
    for (const auto& agent : agents_) {
        if (agent->brain && agent->gpu >= 0) {
            handles.push_back(agent->gpu);
            into.push_back(&agent->own_brain());
        }
    }
    if (!handles.empty()) gpu_->download(handles, into);
}

bool Simulation::dies_of_age(Bitzkey& agent) {
    const double onset = AGEING * agent.lifespan;
    if (agent.age <= onset) return false;
    const double steepness = 2 * std::log(2.0) / std::pow(agent.lifespan - onset, 2.0);
    return random_.random() < steepness * (agent.age - onset);
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
    std::vector<Values> values;  // one batch of brains
    std::vector<double> comforts;
    values.reserve(agents.size());
    comforts.reserve(agents.size());
    if (gpu_) {  // every brain at once, on the GPUs
        std::vector<gpu::BrainEngine::Handle> handles;
        std::vector<Senses> senses;
        handles.reserve(agents.size());
        senses.reserve(agents.size());
        for (std::size_t index = 0; index < agents.size(); ++index) {
            handles.push_back(agents[index]->gpu);
            senses.push_back(plans[index].senses);
        }
        values.resize(agents.size());
        gpu_->values(handles, senses, values);
    } else {
        for (std::size_t index = 0; index < agents.size(); ++index) values.push_back(agents[index]->brain->values(plans[index].senses));
    }
    for (std::size_t index = 0; index < agents.size(); ++index) comforts.push_back(comfort(*agents[index], plans[index].situation));
    timings.think += lap(clock);
    for (std::size_t index = 0; index < agents.size(); ++index) {
        Bitzkey& agent = *agents[index];
        if (!agent.has_last_senses) continue;
        // What came of its last choice: whatever raised its health (fullness counting with it) or its
        // mood was a success, whatever lowered them a failure, by how much; both may happen.
        const double reward = agent.reward + wellbeing(agent) - agent.last_wellbeing + comforts[index] - agent.last_comfort;
        const std::vector<int>& options = plans[index].options;
        double best = values[index][options[0]];
        for (const int option : options) best = std::max(best, values[index][option]);
        learn(agent, reward + discount(agent) * best, true);
        if (reward > 0) experience(agent, Lesson);  // a lesson learnt from a choice that turned out well
    }
    learn_queued();  // on the GPUs, every brain's lessons at once
    timings.learn += lap(clock);
    std::vector<int> choices;
    choices.reserve(agents.size());
    for (std::size_t index = 0; index < agents.size(); ++index) {
        Bitzkey& agent = *agents[index];
        Situation& situation = plans[index].situation;
        std::vector<int>& options = plans[index].options;
        if (primal_fear(agent, situation.hunter)) {
            // Overcome: it panics, fleeing the hunter, or freezes where it stands, whatever its brain wants.
            if (random_.random() < PRIMAL_FREEZE * (1 - agent.intelligence())) {
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
        Values valued = values[index];
        if (agent.species == Species::Predator) {  // the instinct to hunt, on top of what its brain has learnt
            const double urge = HUNTING_INSTINCT * std::min(1.0, agent.hunger / 100);
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
        valued[Scavenge] += CARRION_CRAVING * std::max(0.0, 2 * agent.physique(Carrion) - 1);  // a carrion-eater's craving
        valued[Herd] += LOYALTY_PULL * agent.feeling(Loyalty);  // loyalty: keeping to its herd
        const auto odds = choice_odds(valued, options);
        double roll = random_.random();
        int choice = options.back();
        for (const auto& [option, chance] : odds) {
            roll -= chance;
            if (roll < 0) {
                choice = option;
                break;
            }
        }
        if (!situation.panic && !situation.frozen) insights_.record(agent.species, agent.profile(), options, choice);  // no choice
        agent.odds.fill(0.0);
        agent.odds_offered = 0;
        for (const auto& [option, chance] : odds) {
            agent.odds[option] = chance;
            agent.odds_offered |= 1U << option;
        }
        agent.has_last_senses = true;
        agent.last_senses = plans[index].senses;
        agent.last_choice = choice;
        agent.last_wellbeing = wellbeing(agent);
        agent.last_comfort = comforts[index];
        agent.reward = 0.0;
        agent.assailant.reset();  // the chance to strike back, or need to flee, passes with this choice
        agent.panicked = false;
        agent.alarm.reset();
        agent.herd_assailant.reset();
        agent.kill_alert.reset();
        choices.push_back(choice);
    }
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
    for (std::size_t index = 0; index < agents.size(); ++index) {  // each in turn, in the world as those before it left it
        if (agents[index]->alive()) act(*agents[index], choices[index], plans[index].situation);
    }
    learn_witnessed();
    timings.act += lap(clock);
}

void Simulation::learn_witnessed() {
    for (const Witnessed& seen : witnessed_) {
        if (!seen.observer->brain || seen.rate <= 0) continue;  // not since dead
        if (gpu_) queue_lesson(*seen.observer, seen.senses, seen.choice, seen.outcome, seen.rate, false, 0.0);
        else seen.observer->own_brain().learn(seen.senses, seen.choice, seen.outcome, seen.rate);
    }
    witnessed_.clear();
    learn_queued();
}

void Simulation::survey(Bitzkey& agent) {
    if (agent.species == Species::Predator || agent.species == Species::Omnivore) {
        std::tie(agent.rivalry, agent.own_rivalry) = territory(agent);
        std::tie(agent.hunts_grazers, agent.hunts_omnivores) = prey_switching(agent);
    }
}

Simulation::Sensed Simulation::sense(Bitzkey& agent, bool surveyed) {
    if (!surveyed) survey(agent);
    Sensed sensed;
    Situation& s = sensed.situation;
    const std::vector<Bitzkey*> neighbors = nearby(agent, 1);
    for (Bitzkey* other : nearby(agent, MATE_REACH)) {  // close enough to breed with: the most dominant, the first found on a tie
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
    for (const Bitzkey* other : nearby(agent, 2)) {
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
        for (Bitzkey* other : nearby(agent, static_cast<int>(MATING_CALL_RANGE * sight))) {
            if (compatible(agent, *other) && other->can_breed()) {
                s.suitor = other;
                break;
            }
        }
    }
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
    const std::optional<Cell> comfort = discomfort > 0 ? comfort_row(agent, sight) : std::nullopt;
    Bitzkey* const seen_hunter = s.hunter;  // what it sees itself, to alert its herd to
    bool alarmed = false;
    if (agent.alarm) {
        for (Bitzkey* other : nearby(agent, ALARM_REACH)) {
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
    available[Rest] = available[GoEast] = available[GoWest] = available[WalkEast] = available[WalkWest] = true;
    available[Eat] = s.grass_here;
    available[Forage] = s.grass.has_value();
    available[Browse] = s.flora != nullptr;
    available[Scavenge] = s.carrion != nullptr;
    available[Feast] = s.kill != nullptr;
    available[Court] = s.suitor != nullptr;
    available[Breed] = s.mate && agent.can_breed() && !s.nurseries.empty() && room() > 0 &&
                       (!CROWDING_LIMIT || crowding(agent, *s.mate, true) < CROWDING_LIMIT) &&  // its own kind crowds it
                       (!food_needed(agent) || food_supply(agent) >= food_needed(agent)) &&
                       home_food(agent) >= home_food_needed(agent);  // the food at home: plants, or prey
    available[Hunt] = s.quarry != nullptr;
    available[Attack] = s.prey != nullptr;
    // Only from a hunter close by, unless the herd raised the alarm.
    available[Flee] = s.hunter && (alarmed || distance(s.hunter->x, s.hunter->y, agent.x, agent.y) <=
                                                  flee_range(agent, view.close));
    available[Defend] = s.defend_target != nullptr &&  // a grazer only when strong enough
                        (agent.species != Species::Grazer || agent.trait(Strength) > GRAZER_DEFENCE_STRENGTH);
    // The more fearful it is, the closer it keeps to the herd's middle, but not where the middle is
    // further from its home temperature than here.
    available[Herd] = view.middle && distance(view.middle->x, view.middle->y, agent.x, agent.y) >
                                         HERD_SPREAD * (1 - agent.fearfulness()) &&
                      agent.climate_steps(temperature(view.middle->y)) <= discomfort;
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
    available[Alert] = s.alert_to != nullptr;
    const double fed_at = remembered(agent, agent.food_memory);
    if (agent.food_memory) s.revisit = Cell{agent.food_memory->x, agent.food_memory->y};
    available[Revisit] = fed_at > 0 && !(s.revisit->x == agent.x && s.revisit->y == agent.y);  // back to where it last fed
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
    senses[SenseReadyToBreed] = agent.cooldown == 0;
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
    for (int trait = 0; trait < TEMPERAMENT_COUNT; ++trait) senses[SenseMind + trait] = agent.feeling(trait);
    gene_senses(agent.dna, senses);

    s.rivalry = agent.rivalry;
    s.for_herd = s.assailant == nullptr;
    s.herd = view.any;
    s.herd_near = view.close;
    s.herd_middle = view.middle;
    s.exposure = std::min(2.0, discomfort) / 2 * agent.exposure_share(here);
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
    if (!s.herd) herding -= HERD_LONELINESS * instinct;
    herding *= 0.5 + agent.fearfulness();  // the fearful feel their herd, and its absence, most
    // Out of its home temperature, it is uneasy, the more so the further, and less if hardy to it.
    const double exposed = CLIMATE_UNEASE * s.exposure;
    return company - dread - unease - crowded + herding - exposed;
}

std::vector<Death> Simulation::deaths_so_far() const {
    std::vector<Death> deaths;
    for (const auto& [identifier, death] : death_records_) {
        if (death.turn <= turn_) deaths.push_back(death);
    }
    return deaths;
}

void Simulation::die(Bitzkey& agent, std::string_view cause, const Bitzkey* killer, std::string_view activity) {
    death_records_[agent.identifier] = {std::string(cause), agent.species, turn_, killer ? killer->identifier : -1,
                                        killer ? killer->species : Species::Plant, std::string(activity)};
    if (agent.brain && agent.has_last_senses) {
        const double penalty = cause == "old age" ? 0.0 : DEATH_PENALTY;
        if (!gpu_) learn(agent, agent.reward + wellbeing(agent) - agent.last_wellbeing - penalty);  // on the GPU, a brain about to go learns nothing that lasts
        if (cause != "old age") witness(agent, -DEATH_PENALTY);  // its herd sees it fail
    }
    agent.brain.reset();
    if (gpu_ && agent.gpu >= 0) {
        gpu_->detach(agent.gpu);
        agent.gpu = -1;
    }
    agent.womb.clear();  // the young it carried die with it
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
        ++kill_count_;
    }
    deaths_ += 1;
    death_causes_[static_cast<std::size_t>(agent.species)][std::string(cause)] += 1;
    if (agent.species == Species::Plant) {
        plants_ -= 1;
    } else {
        population_ -= 1;
    }
}

// ---------------------------------------------------------------------------------------------
// Actions

void Simulation::act(Bitzkey& agent, int action, Situation& s) {
    perform(agent, action, s);
    const std::string_view did = agent.last_action;
    if (did == "ate" || did == "browsed" || did == "uprooted" || did == "feasted" || did == "scavenged") {
        agent.food_memory = Place{agent.x, agent.y, turn_};
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
            Bitzkey& suitor = *s.suitor;
            move_toward(agent, suitor.x, suitor.y);
            agent.last_action = "courted";
            if (suitor.alive() && suitor.can_breed() && agent.can_breed() && distance(suitor.x, suitor.y, agent.x, agent.y) <= MATE_REACH) {
                agent.reward += mating_joy(agent, COURT_JOY);  // courted into reach
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
            move_toward(agent, quarry.x, quarry.y, agent.steps + (agent.species == Species::Predator ? HUNT_STEPS : 0));  // predators chase further
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
        case Alert:
            alert(agent, *s.alert_to);
            break;
        case Revisit:
            move_toward(agent, s.revisit->x, s.revisit->y);
            agent.last_action = "revisited";
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
            const int cells = heading.walk ? 1 : agent.steps;
            int moved = 0;  // as far as it goes before the map's edge (north and south) or a full square
            for (int step = 1; step <= cells; ++step) {
                const int x = py_mod(agent.x + heading.dx * step, width_), y = agent.y + heading.dy * step;
                if (y < 0 || y >= height_ || room_in(Cell{x, y}, &agent) < agent.space()) break;
                moved = step;
            }
            if (moved) move(agent, py_mod(agent.x + heading.dx * moved, width_), agent.y + heading.dy * moved);
            if (heading.walk) {
                agent.hunger = std::max(0.0, agent.hunger - agent.burn * WALK_REST);  // burns less, but eases no hunger
            } else {
                agent.hunger += agent.burn * GO_EFFORT * std::max(0, moved - 1);
                exhausted(agent);
            }
            agent.reward += CURIOSITY_JOY * agent.feeling(Curiosity);
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

void Simulation::alert(Bitzkey& agent, const Bitzkey& about) {
    // Its herd within its sight: a grazer alerts them to the hunter it sees, so they may flee it from further
    // than they would; a hunter to the fresh kill it sees, so they may come to feast on it. A herd-mate heeds
    // it only from TRUST_REACH times its trust times its own sight away, or nearer.
    for (Bitzkey* other : nearby(agent, agent.sight)) {
        if (!herd_mate(agent, *other) ||
            distance(other->x, other->y, agent.x, agent.y) > TRUST_REACH * other->feeling(Trust) * other->sight) {
            continue;
        }
        if (agent.species == Species::Grazer) {
            if (!other->alarm) other->alarm = about.identifier;
        } else {
            other->kill_alert = Cell{about.x, about.y};
        }
    }
    agent.reward += ALERT_JOY;
    agent.last_action = "alerted";
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
            --*count;
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
    const double empathy = agent.feeling(Empathy);
    const double joy = FIGHT_JOY * agent.feeling(Aggression) * (1 - 0.6 * empathy) * (1 - 0.75 * agent.feeling(Tolerance));
    double blow = damage(agent, victim);
    for (const Bitzkey* other : nearby(victim, 1)) {
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
        const auto around = nearby(victim, 2);
        if (std::none_of(around.begin(), around.end(), [&](const Bitzkey* other) { return other->species == victim.species; })) {
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
    for (Bitzkey* witness : nearby(victim, 2)) {
        if (witness != &agent && witness->brain) {
            const double kinship = witness->species == victim.species ? 1.0 : OTHER_KIND;
            witness->reward -= pain * witness->feeling(Empathy) * kinship;
        }
    }
    if (killed) die(victim, "killed", &agent, activity);

    const double sting = 4.5 * armed(victim) * defence_share(victim) * (1 - 0.6 * agent.toughness);
    if (sting > 0) {
        agent.health -= sting;
        experience(agent, Stung);
        if (!agent.alive()) die(agent, "killed", &victim, "spines");  // its victim's spines
    }
    exhausted(agent);
    if (agent.species == Species::Predator && agent.alive()) agent.reward += ATTACK_JOY;  // a blow it lived through
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
        agent.reward += FIGHT_JOY * agent.feeling(Aggression) * (1 - 0.6 * agent.feeling(Empathy));
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
    for (Bitzkey* other : nearby(victim, 2)) {
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
    if (random_.random() < victim.fearfulness() * (unhurt + (dying - unhurt) * hurt) * steadied) {
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
            if (random_.random() < chance) {
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
    if (const double needed = home_food_needed(first); needed > 0) {  // a grazer's litters follow the plants in its home band, a predator's the prey
        litter = std::max(1, static_cast<int>(litter * std::min(1.0, home_food(first) / (2 * needed)) + 0.5));
    }
    const std::array<double, 2> costs = {first.breeding_cost, second.breeding_cost};
    const std::array<double, 2> gifts = {std::min(costs[0], std::max(0.0, 100 - first.hunger)) * first.stomach,
                                         std::min(costs[1], std::max(0.0, 100 - second.hunger)) * second.stomach};
    const double given = py_sum(gifts);
    if (NEWBORN_FOOD) {  // only as many young as the parents can feed, rather than many born starving
        const double stomach = (first.stomach + second.stomach) / 2;
        litter = std::max(1, std::min(litter, static_cast<int>(given / (NEWBORN_FOOD * stomach))));
    }
    litter = static_cast<int>(std::max(1.0, std::min(static_cast<double>(litter), room())));
    const int generation = std::max(first.generation, second.generation) + 1;
    Bitzkey* couple[] = {&first, &second};
    sync(couple);  // their brains as they now are, to inherit from
    for (int young = 0; young < litter; ++young) {
        // The child's mind starts at the average of its parents' current minds, mutated, and its
        // dot is written from that mind, so experience is inherited and shows. Its brain, a mix of
        // its parents', is as deep as its own inborn intellect makes it.
        const Mind mind = inherit_mind(first.mind, second.mind, random_, mind_mutation_);
        Dna dna = evolved(first.dna, cross(first.dna, second.dna, random_, mind, MIND_MUTATION_CHANCE, body_mutation_));
        if (first.species == Species::Predator || second.species == Species::Predator) {
            dna = predator_dna(dna);  // a predator's young are predators, whatever crossing over or a mutation made
        }
        auto brain = std::make_shared<Brain>(
            Brain::inherit(*first.brain, *second.brain, random_,
                           depth_for(thinking_intellect(decode_temperament(dna)[Intellect], static_cast<Species>(decode_body(dna)[SpeciesTrait])))));
        if (static_cast<Species>(decode_body(dna)[SpeciesTrait]) == Species::Predator && first.species != Species::Predator) {
            // Born a predator of other parents: what they learnt of eating and uprooting it brings to
            // hunting and feasting.
            for (const auto& [learnt, now] : {std::pair{Eat, Hunt}, std::pair{Uproot, Feast}}) {
                brain->output_weights[static_cast<std::size_t>(now)] = brain->output_weights[static_cast<std::size_t>(learnt)];
                brain->output_biases[static_cast<std::size_t>(now)] = brain->output_biases[static_cast<std::size_t>(learnt)];
            }
        }
        first.womb.push_back({dna, mind, std::move(brain), given / litter, generation});
    }
    first.due_turn = turn_ + gestation(first.trait(Size));
    const std::array<Bitzkey*, 2> pair = {&first, &second};
    for (int index = 0; index < 2; ++index) {
        Bitzkey& parent = *pair[index];
        parent.hunger += costs[index];
        const double rest = parent.species == Species::Predator ? PREDATOR_BREED_COOLDOWN : BREED_COOLDOWN;
        parent.cooldown = static_cast<int>(py_round((20 - 15 * parent.physique(Fertility)) * rest));
        parent.children += litter;
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

std::vector<Bitzkey*> Simulation::nearby(const Bitzkey& agent, int radius) const {
    return nearby_cell({agent.x, agent.y}, radius, &agent);
}

double Simulation::room() const {
    if (!population_cap_) return std::numeric_limits<double>::infinity();
    return static_cast<double>(*population_cap_ - population_);
}

int Simulation::crowding(const Bitzkey& agent, const Bitzkey& partner, bool own_kind) const {
    int count = 0;
    for (const Bitzkey* other : nearby(agent, agent.sight)) {  // within its sight
        count += other->species != Species::Plant && other != &partner && (!own_kind || same_kind(agent, *other));
    }
    return count;
}

void Simulation::give_birth(Bitzkey& mother) {
    // In the squares nearest it with room for each, its own first and several to a square while there is room, as many
    // as the population cap allows; the rest are lost.
    int born = 0;
    for (Bitzkey::Young& young : mother.womb) {
        if (born >= room()) break;
        const int space = 1 + decode_body(young.dna)[Size];
        std::optional<Cell> found;
        for (const Cell& cell : squares_near(mother)) {
            if (room_in(cell) >= space) {
                found = cell;
                break;
            }
        }
        if (!found) continue;  // no room left near it
        Bitzkey& child = add_agent(young.dna, found->x, found->y, std::move(young.brain));
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
        if (room_in(cell) > 0) cells.push_back(cell);
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

std::optional<Cell> Simulation::spot_near(Cell cell, int space) const {
    const int x = py_mod(cell.x, width_), y = within(cell.y);
    if (room_in(Cell{x, y}) >= space) return Cell{x, y};
    for (const Offset& offset : birth_offsets(BIRTH_REACH)) {
        if (y + offset.dy < 0 || y + offset.dy >= height_) continue;
        const Cell near{py_mod(x + offset.dx, width_), y + offset.dy};
        if (room_in(near) >= space) return near;
    }
    return std::nullopt;
}

bool Simulation::step(Bitzkey& agent, int x, int y) {
    if ((x != agent.x || y != agent.y) && room_in(Cell{x, y}, &agent) < agent.space()) return false;  // full: it stops short
    move(agent, x, y);
    return true;
}

std::optional<Cell> Simulation::nearest_grass(const Bitzkey& agent, int sight) const {
    const double worst = agent.climate_steps(in_bounds(agent.x, agent.y) ? temperature(agent.y) : Medium);
    for (const Offset& offset : SIGHT_WITHIN[static_cast<std::size_t>(sight)]) {
        if (grass_at(agent.x + offset.dx, agent.y + offset.dy) > 0 &&
            agent.climate_steps(temperature(agent.y + offset.dy)) <= worst) {
            return Cell{agent.x + offset.dx, agent.y + offset.dy};
        }
    }
    return std::nullopt;
}

double Simulation::warmth(const Bitzkey& agent, int y) const {
    return std::max(-2.0, std::min(2.0, agent.climate.temperature - temperature(y))) / 2;
}

Bitzkey* Simulation::nearest_carrion(const Bitzkey& agent, int sight) const {
    if (carrion_count_ == 0) return nullptr;
    for (const Offset& offset : SEEN_WITHIN[static_cast<std::size_t>(sight)]) {
        const int x = agent.x + offset.dx, y = agent.y + offset.dy;
        if (!in_bounds(x, y)) continue;
        for (Bitzkey* corpse : carrion_[static_cast<std::size_t>(y) * width_ + x]) {
            if (corpse->meat > 0) return corpse;
        }
    }
    return nullptr;
}

Bitzkey* Simulation::nearest_kill(const Bitzkey& agent, int sight) const {
    if (kill_count_ == 0) return nullptr;
    for (const Offset& offset : SEEN_WITHIN[static_cast<std::size_t>(sight)]) {
        const int x = agent.x + offset.dx, y = agent.y + offset.dy;
        if (!in_bounds(x, y)) continue;
        for (Bitzkey* corpse : kills_[static_cast<std::size_t>(y) * width_ + x]) {
            if (feeds_on(agent, *corpse)) return corpse;
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
    for (const Offset& offset : SEEN_WITHIN[static_cast<std::size_t>(std::max(sight, prey_sight))]) {
        const bool far = std::abs(offset.dx) + std::abs(offset.dy) > sight;  // beyond its sight but for prey
        if ((seen.quarry || !hunts) && (far || ((seen.hunter || !hunted) && (seen.suitor || !courts) && (seen.flora || !browses)))) break;
        const auto* list = cell_if(agent.x + offset.dx, agent.y + offset.dy);
        if (!list) continue;
        for (Bitzkey* other : *list) {
            if (other == &agent || !other->alive()) continue;
            if (!seen.quarry && is_prey(agent, *other)) {
                seen.quarry = other;
            } else if (far) {
                continue;
            } else if (!seen.hunter && is_prey(*other, agent)) {
                seen.hunter = other;
            } else if (browses && !seen.flora && other->species == Species::Plant && !other->cooldown) {
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
    if (in_bounds(x, y)) cell_at(x, y).push_back(&agent);
}

void Simulation::move_toward(Bitzkey& agent, int x, int y, int steps) {
    if (steps < 0) steps = agent.steps;
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
    }
}

void Simulation::move_away(Bitzkey& agent, const Bitzkey& threat) {
    for (int taken = 0; taken < agent.steps; ++taken) {
        const int dx = agent.x >= threat.x ? 1 : -1;
        const int dy = agent.y >= threat.y ? 1 : -1;
        if (!step(agent, py_mod(agent.x + dx, width_), within(agent.y + dy))) break;
    }
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
    for (Bitzkey* observer : nearby(actor, MAX_SIGHT)) {
        if (!observer->brain || !society(*observer, actor) || distance(observer->x, observer->y, actor.x, actor.y) > observer->sight) {
            continue;
        }
        const double heed = observer->feeling(Empathy) * observer->intelligence() *
                            (1 + TRUST_HEED * (2 * observer->feeling(Trust) - 1));  // the trusting take in more
        witnessed_.push_back({observer, actor.last_senses, actor.last_choice, outcome, LEARNING_RATE * heed * SOCIAL_LEARNING * choice_learning(actor.last_choice)});  // learnt at the end
        observer->reward += SOCIAL_MOOD * heed * outcome;
        if (outcome > 0) observer->seen_success = 1.0;
        else observer->seen_failure = 1.0;
    }
}

bool Simulation::society(const Bitzkey& observer, const Bitzkey& actor) const {
    return &observer != &actor && observer.species != Species::Plant && observer.species == actor.species &&
           (observer.species != Species::Predator || same_kind(observer, actor)) && willing(observer, actor);
}

bool Simulation::herd_mate(const Bitzkey& agent, const Bitzkey& other) const {
    return &other != &agent && other.alive() && agent.species != Species::Plant && other.species == agent.species &&
           (agent.species != Species::Predator || same_kind(agent, other)) && willing(agent, other);
}

Simulation::HerdView Simulation::herd(const Bitzkey& agent) const {
    std::vector<const Bitzkey*> mates;
    for (const Bitzkey* other : nearby(agent, agent.sight)) {  // its herd: herd-mates within its sight
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
    for (const Bitzkey* other : nearby(hunter, hunter.sight)) {  // its territory: the cells within its sight
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
            if (!in_bounds(agent.x + offset.dx, agent.y + offset.dy)) continue;
            cells += growth(agent.y + offset.dy);
            tufts += grass_at(agent.x + offset.dx, agent.y + offset.dy);
        }
        const double growing = grass_rows_.empty() ? 0.0 : width_ * grass_reach_.back();
        const double regrowth = growing ? cells * grass_regrowth_ / growing * GRASS_OUTLOOK : 0.0;
        food += agent.grazing * (STANDING_GRASS * tufts + regrowth);
    }
    double mouths = 1.0;
    for (const Bitzkey* other : nearby(agent, FORAGE_RADIUS)) {
        if (other->species == Species::Plant) {
            food += PLANT_MEALS * agent.browsing * plant_yield(*other);
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
    for (const Bitzkey* other : nearby(hunter, FORAGE_RADIUS)) {
        grazers += other->species == Species::Grazer;
        omnivores += other->species == Species::Omnivore;
        plants += other->species == Species::Plant;
    }
    const auto common = [](int count, int total) {
        return total == 0 || static_cast<double>(count) / total >= SWITCH_SHARE;
    };
    return {common(grazers, grazers + plants), false};
}

bool Simulation::would_win(const Bitzkey& agent, const Bitzkey& opponent) {
    return damage(agent, opponent) / std::max(1.0, opponent.health) >= damage(opponent, agent) / std::max(1.0, agent.health);
}

bool Simulation::is_prey(const Bitzkey& hunter, const Bitzkey& other) {
    if (hunter.species == Species::Omnivore) {
        // Grazers while they are common enough to switch to, and other omnivores only in an
        // overcrowded territory, once hungry past its reluctance as that rivalry wears it down.
        // Predators only when it would win, blow for blow.
        return (other.species == Species::Grazer && hunter.hunts_grazers) ||
               (other.species == Species::Predator && would_win(hunter, other)) ||
               (other.species == Species::Omnivore && hunter.own_rivalry > 0 &&
                hunter.hunger / 100 > cannibal_reluctance(hunter) * (1 - RIVALRY_WEAR * hunter.own_rivalry));
    }
    if (hunter.species != Species::Predator) return false;
    if (other.species == Species::Grazer) return hunter.hunts_grazers;
    if (other.species == Species::Omnivore) return hunter.hunts_omnivores && would_win(hunter, other);  // only one it would beat
    if (other.species != Species::Predator || same_kind(hunter, other)) return false;  // never its own kind
    // Other kinds of predator only when hungry past its reluctance, which a territory overcrowded
    // with predators wears away.
    return hunter.hunger / 100 > cannibal_reluctance(hunter) * (1 - RIVALRY_WEAR * hunter.own_rivalry);
}

}  // namespace bitzkey
