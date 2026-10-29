#pragma once
// Per-animal neural network: what a Bitzkey senses, what it can do, and how it learns.

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "core/Genome.hpp"
#include "core/PyRandom.hpp"

namespace bitzkey {

enum Sense : int {
    SenseHunger,           // 0..1
    SenseHealth,           // 0..1
    SenseGrassHere,        // 1 when grass lies on the animal's own cell
    SenseGrassSeen,        // 1 when grass is visible elsewhere
    SenseGrassCloseness,   // 1 next to the nearest visible grass, falling to 0 at the edge of sight
    SenseMateNear,         // 1 when a compatible, ready mate is adjacent
    SenseMateCloseness,    // 1 when a willing mate is adjacent, falling to 0 beyond sight
    SenseReadyToBreed,     // 1 when the animal's breeding cooldown has passed
    SensePreyNear,         // adjacent prey, 0..4 scaled to 0..1
    SensePreyFrailty,      // share of the frailest adjacent prey's health one blow would take
    SensePreyCloseness,    // 1 when prey is adjacent, falling to 0 beyond sight
    SensePreyPregnant,     // 1 when the nearest prey in sight carries young
    SenseThreatNear,       // adjacent hunters, 0..4 scaled to 0..1
    SenseThreatDanger,     // share of its own health the worst adjacent hunter's blow would take
    SenseThreatCloseness,  // 1 when a hunter is adjacent, falling to 0 beyond sight
    SenseKinNear,          // same-species animals within two cells, 0..3 scaled to 0..1
    SenseOthersNear,       // animals of other species within two cells, 0..3 scaled to 0..1
    SenseCarrionCloseness, // 1 when meat lies alongside, falling to 0 beyond sight (not grazers)
    SenseKillCloseness,    // 1 when a fresh kill it eats lies alongside, falling to 0 beyond sight (hunters only)
    SensePlantCloseness,   // 1 when a plant stands alongside, falling to 0 beyond sight
    SenseAssailantNear,    // 1 when the animal that just struck it is still alongside
    SenseCounterFrailty,   // share of that assailant's health one counterblow would take
    SenseRivalry,          // predators and omnivores: how overcrowded its territory is with rivals
    SenseHerdNear,         // herd-mates within two cells, 0..3 scaled to 0..1
    SenseHerdCloseness,    // 1 at the middle of its herd in range, falling to 0 at the range's edge
    SenseAlarm,            // 1 when a herd-mate close by was struck, or alerted it to a hunter, since it last acted
    SenseKillAlert,        // 1 when a pack-mate alerted it to a fresh kill, still there, since it last acted
    SenseHerdSuccess,      // 1 when it has just seen a herd-mate succeed (a kill), fading turn by turn
    SenseHerdFailure,      // 1 when it has just seen a herd-mate fail (die), fading turn by turn
    SenseClimateDiscomfort,  // how far the temperature here is from home, as it feels it: 0.5 a step, to 1 two or more
    SenseComfortCloseness,   // 1 where it feels at home or next to such a row, falling to 0 beyond sight
    SenseAge,              // 0 at birth, 1 at 400 turns or older
    // The time of year, as two hands of a clock a quarter of a year apart, so the year's turn is smooth:
    SenseTimeOfYearA,      // 1 at the start of the year, falling to 0 halfway through, and rising back to 1
    SenseTimeOfYearB,      // the same a quarter of a year later: 0 a quarter in, 1 three quarters in
    SenseLatitude,         // -1 at the top (north) edge of the map, 0 on the equator, 1 at the bottom (south) edge
    // How much warmer than home it is, in steps, 1 two or more too hot, -1 two or more too cold, part
    // way while the weather turns, as the zones' colours show:
    SenseWarmth,           // here
    SenseWarmthNorth,      // at the edge of its sight to the north, or the map's edge if nearer
    SenseWarmthSouth,      // and to the south
    SenseFoodRemembered,   // 1 just after it fed somewhere, fading as its memory lets the place go; 0 when forgotten
    SenseDangerRemembered, // 1 at the place it was last struck, while it remembers it, falling to 0 beyond sight
    SenseHomeFood,         // how well fed its home band is: plants per grazer, prey per predator, 1 at twice what breeding needs
    // Where the nearest row it would feel at home in lies, however far beyond sight:
    SenseHomeDirection,    // -1 to the north, 1 to the south, 0 at home or with no such row
    SenseHomeDistance,     // 0 at home, rising to 1 half the map's height away or with no such row
    SenseYoungNear,        // its own young, still infants, within its sight, 0..3 scaled to 0..1
    SenseSeaHere,          // 1 when it stands in the sea
    // A shore it can't cross, sea for a land animal or land for a sea animal: 1 when the next square that way is beyond
    // it, falling to 0 at the edge of its sight, and 0 with none in sight (and always, for the avian and amphibious):
    SenseShoreNorth,
    SenseShoreSouth,
    SenseShoreEast,
    SenseShoreWest,
    SenseStamina,          // 1 fresh, falling to 0 exhausted, when it can only rest
    SenseNight,            // 1 when it is night where it stands
    // An infant's nearest living parent within its sight: 1 beside it, falling to 0 at the edge of its sight, and 0 with
    // none in sight or grown; and which way it lies, -1 to 1, west to east and north to south:
    SenseParentCloseness,
    SenseParentEast,
    SenseParentSouth,
    // The calls it heard since it last chose, HEARD_SENSES for each of the five, the first call's first (see Heard):
    SenseHeardCall,
    SenseMemory = SenseHeardCall + 4 * 5,  // HEARD_SENSES * CALL_COUNT; then what its memory neurons held, -1..1 (see Memory)
    // Then its mental map (see Simulation's MapPlace): of the places it remembers, for this season, how good the best for
    // food was, 0..1, and which way it lies, -1 to 1, west to east and north to south, the nearest first among equals;
    // the same of the most dangerous; and of where it was most comfortable in the season to come.
    SenseMapFood = SenseMemory + 8,  // MEMORY_NEURONS
    SenseMapFoodEast,
    SenseMapFoodSouth,
    SenseMapDanger,
    SenseMapDangerEast,
    SenseMapDangerSouth,
    SenseMapComfort,
    SenseMapComfortEast,
    SenseMapComfortSouth,
    SenseMind,  // then its current temperament, mind and social traits, each 0..7 scaled to 0..1
};
enum Action : int { Eat, Forage, Browse, Uproot, Scavenge, Feast, Rest, Court, Breed, Hunt, Attack, Defend, Flee, Herd,
                    GoNorth, GoSouth, GoEast, GoWest, WalkNorth, WalkSouth, WalkEast, WalkWest, Revisit, FeedYoung };
constexpr int ACTION_COUNT = 24;
// Alongside its action, every animal that thinks keeps silent or calls: the alert, to its herd, of a hunter (grazers)
// or a fresh kill (hunters) it sees, or one of five calls that mean only what its kind come to make of them, heard by
// all close enough. A brain values them as it values its actions, after them, and learns them the same way.
enum Call : int { Silent, AlertCall, FirstCall };  // the five calls from FirstCall on
constexpr int CALL_COUNT = 5;
constexpr int CALL_OPTIONS = FirstCall + CALL_COUNT;
// Memory: a brain carries MEMORY_NEURONS values from one choice to the next, each read back the next time as a sense
// (SenseMemory). Each turn, beside valuing its actions and calls, it gives each a gate, z = sigmoid of its output, and a
// value to write, c = tanh of its output, and the neuron becomes (1 - z) times what it held plus z times c: kept as it
// was (z near 0), overwritten (z near 1), or cleared (writing 0). Its gates read the top hidden layer, which reads what
// the memory held with everything else it senses. They learn back in time (Brain::learn_memory): how a lesson's error
// moves with what the memory held is carried back to the gates that wrote it, as many choices back as Memory steps.
constexpr int MEMORY_NEURONS = 8;
constexpr int MEMORY_GATES = ACTION_COUNT + CALL_OPTIONS;   // the first gate's output, then each neuron's in turn,
constexpr int MEMORY_VALUES = MEMORY_GATES + MEMORY_NEURONS;  // then the first value to write, and each neuron's
constexpr int OUTPUT_COUNT = MEMORY_VALUES + MEMORY_NEURONS;
static_assert(SenseMapFood == SenseMemory + MEMORY_NEURONS);
constexpr int MAX_MEMORY_STEPS = 4;  // the furthest back Memory steps may reach
using Held = std::array<double, MEMORY_NEURONS>;  // what memory neurons hold
// What it senses of each call it heard, of the loudest who made it: how loud, 1 from beside it, falling to 0 at the
// call's reach (the larger the caller, the further it carries); which way it lay, -1 to 1, west to east and north to
// south; and whether it was of its own species, 1 or 0. All 0 for a call it didn't hear.
enum Heard : int { HeardLoudness, HeardEast, HeardSouth, HeardKin };
constexpr int HEARD_SENSES = 4;
static_assert(SenseMemory == SenseHeardCall + HEARD_SENSES * CALL_COUNT);
constexpr int heard_sense(int call, int heard) { return SenseHeardCall + HEARD_SENSES * (call - FirstCall) + heard; }
constexpr int call_output(int call) { return ACTION_COUNT + call; }  // the output valuing a call (or silence)
constexpr std::array<std::string_view, ACTION_COUNT> ACTIONS = {
    "eat", "forage", "browse", "uproot", "scavenge", "feast", "rest", "court", "breed", "hunt", "attack", "defend",
    "flee", "herd", "go_north", "go_south", "go_east", "go_west", "walk_north", "walk_south", "walk_east", "walk_west",
    "revisit", "feed_young"};
// A brain's outputs by name: its actions, then silence and the calls, then its memory's gates and values to write.
constexpr std::array<std::string_view, OUTPUT_COUNT> OUTPUTS = [] {
    std::array<std::string_view, OUTPUT_COUNT> names{};
    for (int action = 0; action < ACTION_COUNT; ++action) names[action] = ACTIONS[action];
    constexpr std::array<std::string_view, CALL_OPTIONS> calls = {"silent", "alert", "call_1", "call_2", "call_3", "call_4", "call_5"};
    for (int call = 0; call < CALL_OPTIONS; ++call) names[call_output(call)] = calls[call];
    constexpr std::array<std::string_view, 2 * MEMORY_NEURONS> memory = {
        "memory_gate_1", "memory_gate_2", "memory_gate_3", "memory_gate_4", "memory_gate_5", "memory_gate_6", "memory_gate_7", "memory_gate_8",
        "memory_value_1", "memory_value_2", "memory_value_3", "memory_value_4", "memory_value_5", "memory_value_6", "memory_value_7", "memory_value_8"};
    for (int output = 0; output < 2 * MEMORY_NEURONS; ++output) names[MEMORY_GATES + output] = memory[output];
    return names;
}();
constexpr int SenseInborn = SenseMind + TEMPERAMENT_COUNT;  // then its dot's and social background's traits, as born, 0..1
constexpr int SenseBody = SenseInborn + TEMPERAMENT_COUNT;  // then its ring's traits, 0..1 (species 0 plant to 1 predator)
constexpr int SenseHomeTemperature = SenseBody + BODY_TRAIT_COUNT;  // then its climate genes: home 0 hot to 1 very cold,
constexpr int SenseHeatTolerance = SenseHomeTemperature + 1;        // and the tolerance flags, 0 or 1
constexpr int SenseColdTolerance = SenseHeatTolerance + 1;
constexpr int SenseSwims = SenseColdTolerance + 1;  // then its terrain: whether it may go into the sea, 0 or 1,
constexpr int SenseWalks = SenseSwims + 1;          // onto the land,
constexpr int SenseFlies = SenseWalks + 1;          // and whether it flies
constexpr int SenseNocturnal = SenseFlies + 1;     // then its activity gene: 1 nocturnal, 0 diurnal
constexpr int INPUT_SIZE = SenseNocturnal + 1;
// The senses as a brain reads them: each that runs 0..1 centred, read as -1..1, so that a lesson moves a neuron's weights
// both ways (senses never below 0 push all its weights the same way, lesson after lesson, until the neuron jams at its
// limit, the same whatever it senses, and learns no more); those already running -1..1 read as they are.
constexpr bool signed_sense(int sense) {
    return sense == SenseLatitude || sense == SenseWarmth || sense == SenseWarmthNorth || sense == SenseWarmthSouth ||
           sense == SenseHomeDirection || sense == SenseParentEast || sense == SenseParentSouth ||
           (sense >= SenseHeardCall && sense < SenseMemory &&
            ((sense - SenseHeardCall) % HEARD_SENSES == HeardEast || (sense - SenseHeardCall) % HEARD_SENSES == HeardSouth)) ||
           (sense >= SenseMemory && sense < SenseMapFood) || sense == SenseMapFoodEast || sense == SenseMapFoodSouth ||
           sense == SenseMapDangerEast || sense == SenseMapDangerSouth || sense == SenseMapComfortEast || sense == SenseMapComfortSouth;
}
inline double read_sense(int sense, double value) { return signed_sense(sense) ? value : value * 2.0 - 1.0; }
constexpr std::array<std::string_view, INPUT_SIZE> SENSES = {
    "hunger", "health", "grass_here", "grass_seen", "grass_closeness", "mate_near", "mate_closeness",
    "ready_to_breed", "prey_near", "prey_frailty", "prey_closeness", "prey_pregnant", "threat_near", "threat_danger",
    "threat_closeness", "kin_near", "others_near", "carrion_closeness", "kill_closeness", "plant_closeness",
    "assailant_near", "counter_frailty", "rivalry", "herd_near", "herd_closeness", "alarm", "kill_alert", "herd_success", "herd_failure",
    "climate_discomfort", "comfort_closeness", "age", "time_of_year_a", "time_of_year_b", "latitude", "warmth",
    "warmth_north", "warmth_south", "food_remembered", "danger_remembered", "home_food", "home_direction", "home_distance", "young_near",
    "sea_here", "shore_north", "shore_south", "shore_east", "shore_west", "stamina", "night", "parent_closeness", "parent_east", "parent_south",
    "heard_call_1", "heard_call_1_east", "heard_call_1_south", "heard_call_1_kin",
    "heard_call_2", "heard_call_2_east", "heard_call_2_south", "heard_call_2_kin",
    "heard_call_3", "heard_call_3_east", "heard_call_3_south", "heard_call_3_kin",
    "heard_call_4", "heard_call_4_east", "heard_call_4_south", "heard_call_4_kin",
    "heard_call_5", "heard_call_5_east", "heard_call_5_south", "heard_call_5_kin",
    "memory_1", "memory_2", "memory_3", "memory_4", "memory_5", "memory_6", "memory_7", "memory_8",
    "map_food", "map_food_east", "map_food_south", "map_danger", "map_danger_east", "map_danger_south",
    "map_comfort", "map_comfort_east", "map_comfort_south", "aggression", "libido", "curiosity",
    "fear", "appetite", "patience", "intellect", "memory", "resilience", "moodiness", "sociability", "empathy",
    "tolerance", "loyalty", "dominance", "trust", "kinship", "inborn_aggression", "inborn_libido", "inborn_curiosity",
    "inborn_fear", "inborn_appetite", "inborn_patience", "inborn_intellect", "inborn_memory", "inborn_resilience",
    "inborn_moodiness", "inborn_sociability", "inborn_empathy", "inborn_tolerance", "inborn_loyalty",
    "inborn_dominance", "inborn_trust", "inborn_kinship", "species", "size", "strength",
    "armour", "weapons", "speed", "sight", "fertility", "litter", "carrion", "home_temperature",
    "heat_tolerance", "cold_tolerance", "swims", "walks", "flies", "nocturnal"};
int action_index(std::string_view name);  // -1 when unknown
int sense_index(std::string_view name);   // -1 when unknown

constexpr int MAX_DEPTH = 4;    // hidden layers: one for each point of inborn intellect, at least one and up to this many
constexpr int BASE_WIDTH = 38;  // neurons in each hidden layer, up to an intellect of MAX_DEPTH
constexpr int WIDTH_STEP = 5;   // neurons more in each layer for each point of intellect beyond MAX_DEPTH
constexpr int MAX_WIDTH = BASE_WIDTH + WIDTH_STEP * (TRAIT_MAX - MAX_DEPTH);
inline double LESSON_LIMIT = 2.0;  // the most one lesson's error counts for, so a rare but large surprise teaches, but no lesson runs wild
inline double TEMPERATURE = 0.5;  // loose enough that an action valued a little below the best is still tried
inline double MUTATION = 0.0;
inline double SMART_TRACE_DECAY = 0.95;  // lambda at the highest intellect: the clever trace credit further back
inline double PATIENT_DISCOUNT = 0.99;  // gamma at the highest patience: how far ahead the patient look
inline double IMPATIENT_DISCOUNT = 0.9;  // and at the lowest: even the least patient look some ten turns ahead
inline double TRACE_DECAY = 0.7;  // lambda: how much of each earlier choice's credit carries on to the next, beside the discount
// A new brain values setting off in a direction this much below its other actions, so an untaught
// brain doesn't march straight away from its range, its herd and its food before it has learned where to go.
inline double HEADING_RELUCTANCE = 0.5;
// And values each call this much below silence, so an untaught world isn't all calling at once.
inline double CALL_RELUCTANCE = 0.5;
// And its memory gates this far below their middle, a fifth open, so what it holds fades slowly until it learns otherwise.
inline double MEMORY_PERSISTENCE = 1.4;
// A brain holds its values in hundredths: what an action is worth over the turns it looks ahead runs to ten and more,
// and its neurons, each between -1 and 1, would jam at their limits to make that much, the same whatever it senses.
// Held small, the output layer makes them of neurons free to follow the senses. It gives and learns them whole.
constexpr double VALUE_SCALE = 0.01;
constexpr int RECORD_DIGITS = 6;  // decimal places kept when a brain is exported
// A new brain's output weights are drawn within this of zero: smaller the wider its layers, so its
// untaught values spread as they did when layers were twelve neurons wide.
inline double output_spread(int width) { return 0.3 * std::sqrt(12.0 / width); }

using Senses = std::array<double, INPUT_SIZE>;
using Values = std::array<double, OUTPUT_COUNT>;  // its actions', then its calls'

// How many hidden layers a brain has, by its owner's inborn intellect: one a point, from one to MAX_DEPTH.
inline int depth_for(int intellect) { return std::max(1, std::min(MAX_DEPTH, intellect)); }
// How many neurons each of its hidden layers has: BASE_WIDTH, and WIDTH_STEP more for each point of
// intellect beyond MAX_DEPTH.
inline int width_for(int intellect) { return BASE_WIDTH + WIDTH_STEP * std::max(0, intellect - MAX_DEPTH); }
// Fill in the inputs for a genome's traits, ring, dot and social background, its climate genes and its terrain.
void gene_senses(Dna dna, Senses& senses);

// What memory neurons hold after a choice valued so, holding `held` before it.
Held memory_after(const Values& values, const Held& held);

// Softmax over the available actions' values, in the options' order.
std::vector<std::pair<int, double>> choice_odds(const Values& values, std::span<const int> options);

// One hidden layer: a row of weights for each neuron, reading the senses (the first layer) or the
// layer before.
struct Layer {
    int reads = 0;
    std::vector<double> weights;  // a row of `reads` for each neuron
    std::vector<double> biases;   // one for each neuron

    int neurons() const { return static_cast<int>(biases.size()); }
    double* row(int neuron) { return weights.data() + static_cast<std::size_t>(neuron) * reads; }
    const double* row(int neuron) const { return weights.data() + static_cast<std::size_t>(neuron) * reads; }
    static Layer zeros(int neurons, int reads);  // every weight and bias zero
    static Layer identity(int width);  // passes its input on, near enough
    // A copy cut down or padded with zeros to `neurons` of `reads`: neurons, and weights from neurons, it no
    // longer has are dropped, and those it gains are idle until mutation moves them.
    Layer fitted(int neurons, int reads) const;
    bool operator==(const Layer&) const = default;
};

// A tanh network of hidden layers estimating the long-run value of each action. It learns online
// from temporal-difference targets and is inherited neuron by neuron. Learning from its own choices,
// it keeps an eligibility trace for every weight (TD(lambda)), so what comes of a choice reaches back
// to the choices that led to it; the traces belong to one life and are neither inherited nor exported.
class Brain {
public:
    using OutputWeights = std::array<std::vector<double>, OUTPUT_COUNT>;  // a row of the brain's width for each output
    using OutputBiases = std::array<double, OUTPUT_COUNT>;
    struct Traces {
        std::vector<Layer> layers;  // shaped as the brain's own
        OutputWeights output_weights{};
        OutputBiases output_biases{};
    };

    std::vector<Layer> layers;
    OutputWeights output_weights{};
    OutputBiases output_biases{};
    std::optional<Traces> traces;  // none until it first learns from its own choice

    int depth() const { return static_cast<int>(layers.size()); }
    int width() const { return layers.empty() ? 0 : layers.front().neurons(); }  // neurons in each hidden layer
    static Brain random(PyRandom& rng, int depth = 1, int width = BASE_WIDTH);
    // A child's brain, `depth` layers of `width` neurons. Where both parents have a layer, each of its
    // neurons comes from one parent or the other, or from the one that has it where only one does;
    // where only one has the layer, from that one; deeper still, it starts passing its input on. A
    // parent's layers are fitted to the child's width (see Layer::fitted). Each action's output comes
    // from one parent or the other. Then it mutates.
    static Brain inherit(const Brain& first, const Brain& second, PyRandom& rng, int depth = 1, int width = BASE_WIDTH,
                         double mutation = MUTATION);
    // The same from a seed drawn already: every choice and mutation comes from the child's own stream.
    static Brain inherit(const Brain& first, const Brain& second, std::uint64_t seed, int depth = 1, int width = BASE_WIDTH,
                         double mutation = MUTATION);
    // A copy `depth` layers of `width` neurons: deeper with layers that pass their input on, or
    // shallower without its last layers, and each layer fitted (see Layer::fitted).
    Brain resized(int depth, int width) const;
    // Made to read the senses centred (read_sense), from a brain that read them as they were, choosing just as before;
    // what it was learning from lets go.
    void centre_inputs();
    // Made to hold its values in hundredths (VALUE_SCALE), from a brain that held them whole, valuing just as before;
    // what it was learning from lets go.
    void scale_values();

    Values values(const Senses& inputs) const;
    // Move one action's value toward a target, with the error clipped for stability, back through
    // every layer.
    void learn(const Senses& inputs, int action, double target, double learning_rate);
    // Learn from its own choice by TD(lambda): every weight's eligibility fades by the discount times
    // TRACE_DECAY and gains how this action's value moves with it; then every weight moves by the
    // clipped error times its eligibility, so the choices before this one share in it.
    // Lambda runs from TRACE_DECAY up to SMART_TRACE_DECAY with `sharpness`, its owner's intellect, 0 to 1.
    void learn_traced(const Senses& inputs, int action, double target, double learning_rate, double discount,
                      double sharpness = 0.0);
    // The same, with the traces' decay (the discount times lambda) worked out already, as the GPUs are given it.
    void learn_traced_by(const Senses& inputs, int action, double target, double learning_rate, double decay);
    // Learn back in time, after the lesson on `inputs` (its senses at its choice): how that action's value, with its
    // error, moves with what its memory held then is carried back to the gates that wrote it, at `past` (its senses at
    // the choices before, most recent first, each the memory was written at), each moving its gates and values to write,
    // and the layers beneath them, to hold what would have valued the choice after rightly; and so on back.
    void learn_memory(const Senses& inputs, std::span<const Senses* const> past, int action, double target, double learning_rate);

    nlohmann::ordered_json to_record() const;
    // Weights are matched to senses and outputs by name; a brain saved with a single hidden
    // layer loads one layer deep. It keeps the width it was saved with, and the Bitzkey given it
    // resizes it to its own. Throws std::invalid_argument.
    static Brain from_record(const nlohmann::json& record);
    // The senses, then each hidden layer's output: for the probe to see which neurons have jammed at either end.
    std::vector<std::vector<double>> activations(const Senses& inputs) const;

    // The same weights; traces don't count.
    bool operator==(const Brain& other) const {
        return layers == other.layers && output_weights == other.output_weights && output_biases == other.output_biases;
    }
};

}  // namespace bitzkey
