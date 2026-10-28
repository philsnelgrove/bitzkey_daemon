#pragma once
// Turn-based Bitzkey world and its deterministic history.

#include <array>
#include <cmath>
#include <map>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <vector>

#include "core/Brain.hpp"
#include "core/Census.hpp"
#include "core/Exchange.hpp"
#include "core/Genome.hpp"
#include "core/GpuBrains.hpp"
#include "core/Insights.hpp"
#include "core/PyRandom.hpp"

namespace bitzkey {

constexpr int GRID_SIZE = 100;
// A new world from the setup panel: twice as wide as it is tall, as a globe has it.
constexpr int DEFAULT_WIDTH = 360;
constexpr int DEFAULT_HEIGHT = 180;
constexpr int INITIAL_POPULATION = 300;
constexpr double GRASS_DENSITY = 50;      // percent of cells that start with grass
inline double GRASS_REGROWTH = 0.032;  // share of the starting grass that grows back each turn
constexpr int MAX_POPULATION = 20000;     // the most Bitzkeys spawned at the start
inline int CROWDING_LIMIT = 10;  // 0: no limit
constexpr std::array<int, 2> SPAWN_GROUP = {3, 5};
constexpr int MAX_IMPORTED = 120;  // imported Bitzkeys, each spawning a group of its own
constexpr int MAX_GRID = 1000;     // the widest and tallest a world can be, in cells
// The sea: a new world's share of the map under it, in percent, and how broad its land and sea are, as a share of the
// map's width (smaller, and they break into more and smaller islands and seas). East and west, the coasts wrap round.
constexpr double DEFAULT_OCEAN = 33.0;
constexpr double CONTINENT_BREADTH = 0.25;
// Terrain. An avian animal's size, weapons and armour count AVIAN_BODY of its genes' (light enough to fly), and it burns
// FLIGHT_HUNGER more food; the terrestrial and aquatic can hunt or attack it only while it rests. An amphibious animal
// goes AMPHIBIOUS_SPEED of its usual steps on land, and rests AMPHIBIOUS_COOLDOWN times as long after breeding. An avian
// plant's seeds fly AVIAN_SEED_REACH times as far.
inline double AVIAN_BODY = 0.5;
inline double FLIGHT_HUNGER = 0.25;
inline double AMPHIBIOUS_SPEED = 0.5;
inline double AMPHIBIOUS_COOLDOWN = 2.0;
inline double AVIAN_SEED_REACH = 2.0;
// Stamina, 0 to 100: every action but rest tires an animal, and resting restores it. Its pace follows the day: of the
// half of each day an animal is awake, the average one acts ACTIVE_SHARE and rests the rest, spending its stamina in
// the one and getting it back in the other, however many turns a day is (see stamina_pace and rest_recovery). Before
// that pace is applied, any action but rest costs ACTION_FATIGUE, each square it moves STEP_FATIGUE more
// (a flyer's FLIGHT_FATIGUE times that), each blow it strikes STRIKE_FATIGUE and mating BREED_FATIGUE (each parent);
// a flyer over the sea, where it can't rest, tires HOVER_FATIGUE a turn more; an average turn of acting costs
// TYPICAL_FATIGUE. An animal with no stamina left can only rest, and a flyer exhausted over the sea drowns. An
// amphibious animal tires AMPHIBIOUS_FATIGUE times as fast at everything.
inline double ACTIVE_SHARE = 2.0 / 3;
constexpr double TYPICAL_FATIGUE = 20;  // an action and two squares' going
inline double ACTION_FATIGUE = 10;
inline double STEP_FATIGUE = 5;
inline double FLIGHT_FATIGUE = 2.0;
inline double STRIKE_FATIGUE = 50;
inline double BREED_FATIGUE = 75;
inline double HOVER_FATIGUE = 5;
inline double AMPHIBIOUS_FATIGUE = 2.0;
// What tiring by `cost` takes from an animal: at the day's pace, and an amphibious one's AMPHIBIOUS_FATIGUE times as much.
inline double fatigue(const Bitzkey& agent, double cost);
constexpr double FULL_STAMINA = 100;
double stamina_pace();   // the share of the listed fatigue an action costs, for a day of DAY_LENGTH turns
double rest_recovery();  // stamina a turn's rest restores
inline int SPAWN_SPREAD = 2;
// A square holds CELL_SPACE spaces, and every Bitzkey, plant or animal, takes 1 + its size of them. Bitzkeys share a
// square, and meet there as they would alongside, but none is born, moves or spawns into one without room for it.
inline int CELL_SPACE = 10;
// Full hunger doesn't kill at once: a starving Bitzkey loses this share of its full health each turn.
inline double STARVING_DAMAGE = 0.1;
// What a large body costs, against all it gains, so that size isn't simply better: it burns 1 + SIZE_HUNGER times its
// size squared as much (a share of the size range: twice as much at the largest, by default); it wastes away
// 1 + SIZE_STARVING times its size as fast starving; a hunter's blow misses a small animal outright with a chance of
// SMALL_HIDING times its smallness squared (40% at the smallest, none at the largest); and the smallest are infants
// SMALL_MATURITY as long as their lifespans would make them, rising to the whole of it at the largest.
inline double SIZE_HUNGER = 1.0;
inline double SIZE_STARVING = 1.0;
inline double SMALL_HIDING = 0.4;
inline double SMALL_MATURITY = 0.1;  // the smallest an infant some 4 days, the largest 360
// Meat lasts: the share of its fullness that is meat empties at this share of the usual rate.
// Meat lasts seven times as long as plant food: a meal of meat a week to a meal of plants a day. An omnivore's mixed
// fullness empties at the mix of the two rates.
inline double MEAT_DIGESTION = 1.0 / 7;
// Feeble: an animal in its infancy (the first FEEBLE_YOUTH of its lifespan) or its old age (past AGEING of it)
// has every body trait, and its health, at FEEBLE_SHARE of its own.
inline double FEEBLE_YOUTH = 1.0 / 30;  // 360 days for the largest; the smallest SMALL_MATURITY of their 36
inline double PREDATOR_INFANCY = 0.5;  // a predator is an infant this share as long
inline double FEEBLE_SHARE = 0.5;
constexpr int DEPLOY_GROUP = 5;  // Bitzkeys deployed from the catalog onto a running world, of each chosen, a click
inline int SPAWN_AT_HOME = 1;  // 1: a group appears in rows whose usual temperature is its home (the nearest, if none); 0: anywhere
// Biomes: each stretch of land or sea within one climate zone, joined square to square (east and west wrapping round),
// of BIOME_SIZE squares or more. A world started with any Bitzkeys has BIOME_GROUPS more groups in every biome, on
// top of its population, each at home there: the zone's usual temperature its home, and a terrain that suits it, the
// first the surface's own (terrestrial on land, aquatic at sea) and the rest any that may live there.
inline int BIOME_GROUPS = 2;
// Each death is kept whole, for the probe and the graph's tooltips, for this many turns; the death report counts
// every death since the world began as it happens (see DeathTally), and the tally lets go of the killers gone, but
// the deadliest, every KILLERS_LET_GO turns.
constexpr int RECENT_DEATH_TURNS = 1000;
constexpr int KILLERS_LET_GO = 100;
inline int BIOME_SIZE = 25;
// 1: every brain lives on the GPUs, thinking and learning there in batches, in floats. 0: on the CPU, exactly as
// the Python version. Read when a world is made.
inline int USE_GPU = 0;
// Earlier versions kept the history on disk, in folders among the temporary files.
constexpr std::string_view HISTORY_PREFIX = "bitzkey-history-";  // their names start so
constexpr int STALE_HISTORY_HOURS = 24;  // one left this long by a run that never closed it is cleared away

// Clear away history folders that earlier versions left among the temporary files, once they are
// STALE_HISTORY_HOURS old, so they don't go on filling the disk.
void remove_stale_histories();
// Text kept for as long as the program runs, for a name read back from a file to be held where the
// names written into the program are: an animal's last action.
std::string_view kept_text(std::string_view text);

inline int CORPSE_FERTILITY = 2;
inline int CORPSE_TURNS = 90;  // under four days
inline double CORPSE_PLANT = 0.1;  // chance an animal's body that rots away entirely leaves the seed of a new plant where it lay
inline double BREED_RESERVE = 0;
inline double MATE_RING_DIFFERENCE = 0.05;
inline double MATE_DOT_DIFFERENCE = 0.20;
inline std::array<int, 2> LITTER_RANGE = {1, 6};
inline double LITTER_SIZE_WEIGHT = 0.5;
inline double SMALL_LITTERS = 2.0;  // the smallest bodies' litters are this many times the usual, falling evenly to the usual for the largest
// Turns an animal carries its young, from mating to birth: from the first for the smallest body to the
// second for the largest, evenly between, rounded half up.
inline std::array<int, 2> GESTATION = {30, 150};  // a day and a quarter to six and a quarter days
// Heat: an animal is fertile only while in heat, a stretch of the turns before its next winter, the winter of the row
// it stands in. Heat ends HEAT_CLOSEST of a year before that winter, and HEAT_FORESIGHT more for the most patient, who
// breed further from it; it begins HEAT_LENGTH of a year before it ends. No animal is in heat through its own winter,
// and one where no winter comes, at the polar caps or the equator's heart, always is. By species, plants aside.
inline std::array<double, 4> HEAT_CLOSEST = {0.0, 0.1, 0.1, 0.1};
inline std::array<double, 4> HEAT_LENGTH = {0.0, 0.5, 0.5, 0.5};
inline double HEAT_FORESIGHT = 0.15;
inline int BIRTH_REACH = 2;  // young are born on the free cells nearest their mother, up to this many cells away
// Years a Bitzkey, plant or animal, is expected to live: the smallest bodies, with the fastest
// metabolisms; middling ones; and the largest, with the slowest. Between them, lifespan rises steadily
// with the metabolism slowing.
inline std::array<double, 3> LIFESPAN_YEARS = {3.0, 15.0, 30.0};  // long enough for even the smallest to see its winter come round again
inline std::array<double, 2> METABOLISM = {1.6, 0.6};
inline std::array<double, 2> STOMACH = {0.5, 2.0};
inline double SIZE_IN_MIGHT = 0.5;
inline double AGEING = 0.7;
inline double DEATH_PENALTY = 2.0;
// Deaths no last choice is blamed for: the health lost on the way has taught their lessons already.
inline bool slow_death(std::string_view cause) {
    return cause == "old age" || cause == "starved" || cause == "froze" || cause == "overheated";
}
// Breeding is the greatest pleasure there is, and courting a mate into reach the next: each at three quarters of its
// full strength, and the rest by libido, so even the coldest animal learns to seek a mate. Courting is rewarded only
// once it brings a ready, willing partner within MATE_REACH; neither costs anything when it fails.
inline double BREED_JOY = 4.0;
inline double COURT_JOY = 2.5;
inline double LIBIDO_IN_JOY = 0.25;  // the share of those pleasures that libido sets
inline double FIGHT_JOY = 0.5;
inline double COMPANY_JOY = 0.1;
inline double CURIOSITY_JOY = 0.15;
// Moving costs food. A walk, a single cell, saves this share of the turn's burn, as resting does; going as
// far as its speed allows costs this share of a turn's burn more for each cell beyond the first.
inline double WALK_REST = 0.5;
inline double GO_EFFORT = 0.25;
inline double FEAR_DREAD = 0.3;
inline double UNEASE = 0.1;
inline double CLIMATE_UNEASE = 2.0;  // discomfort two or more steps from home, half that one step; less for the hardy
inline double HOMEWARD_UNEASE = 3.0;  // more discomfort away from home, rising with the distance to home to this at half the map's height
inline double EMPATHY_PAIN = 0.15;
// Parental attachment: a parent is glad of its own young, still infants, within its sight, up to PARENT_JOY with
// three or more, and grieves PARENT_GRIEF when one of them dies, wherever it is; both by half again and up to one
// and a half times as much with its empathy. Neither tells it what to do: what keeps its young alive is for its
// brain to learn.
inline double PARENT_JOY = 0.3;
inline double FEEDING_JOY = 1.0;  // how glad a parent is of feeding a starving infant of its own, and in proportion for one less hungry; more if empathetic
inline double FEED_HUNGER = 25.0;  // a parent may feed its infants within its sight once one of them is this hungry
inline double PARENT_GRIEF = 2.0;
// A hunter that kills a mother carrying young feels remorse, by half again and up to one and a half times as much
// with its empathy: a lesson, not a law.
inline double PREGNANT_REMORSE = 1.0;
// Society: an animal sees what happens to its herd-mates (its pack, for a predator) within its sight. A
// herd-mate's kill is a success, its death (but of old age) a failure; the witness learns from it as
// if it had made the herd-mate's last choice itself, and feels it, by its empathy times its intellect.
inline double SOCIAL_LEARNING = 1.0;  // how fast it learns from what it sees, beside learning from its own choices
inline double SOCIAL_MOOD = 0.3;      // how much what it sees moves its mood
inline double SOCIAL_MEMORY = 0.93;   // the share of a seen success or failure it still senses a turn later (0.8 every three hours)
inline double OTHER_KIND = 0.4;
// The social background's traits. Loyalty: whenever it can herd, an animal values keeping to its herd this much
// more, times its loyalty. Dominance: a ready animal breeds with the most dominant willing partner in reach, and
// a territory's crowding troubles the dominant less and the submissive more, by up to this share.
inline double LOYALTY_PULL = 0.25;
inline double PARENTAL_INSTINCT = 10.0;  // how much more a parent values feeding its young, times their hunger and its empathy
inline double INFANT_HERDING = 1.0;  // how much more an infant values keeping to its kind, and it herds once a cell from their middle
inline int NURSING_REACH = 2;        // each turn, a grown animal feeds the hungriest infant of its kind this close, a turn's burn of food,
inline int NURSING_LIMIT = 80;       // while it stays below this hunger itself
inline double DOMINANCE_EASE = 0.5;
// Trust: how much more, or less, the most and least trusting take in what they see of their herd-mates, by
// this share; and an alert reaches it from a herd-mate this many times its trust times its sight away.
inline double TRUST_HEED = 0.5;
inline double TRUST_REACH = 2.0;
// Kinship: the most a herd-mate's or partner's social background may differ from its own, at full kinship and
// no tolerance, is 1 less this; less kinship, or more tolerance, accepts more.
inline double KINSHIP_SHUN = 0.8;
// The mind's new traits. Memory: an animal remembers where it last fed, and where it was last struck, for up to
// MEMORY_TURNS at the best memory, fewer the poorer. Resilience: the most resilient get over a herd-mate's failure
// faster, and fear a bad experience less, by up to this share, the least resilient slower and more. Moodiness:
// experience moves the moodiest temperament by up to this share more, the steadiest by this share less.
inline int MEMORY_TURNS = 300;
inline double RESILIENCE = 0.5;
inline double MOODINESS = 0.5;
inline double LEARNING_RATE = 0.027;
// The clever learn faster. The learning rate rises with intellect, and on top of that by up to
// LEARNING_BOOST times again at the highest; the least intelligent get no boost. And each turn an
// animal re-learns from REPLAY_PER_INTELLECT of its most recent experiences for each point of its
// inborn intellect, so the least replay none.
inline double LEARNING_BOOST = 1.0;
inline int REPLAY_PER_INTELLECT = 2;
inline int MATE_REACH = 2;            // a partner within this many steps is close enough to breed with
inline double MATING_CALL = 0.5;      // how much more a predator ready to breed values breeding, or else courting a mate it sees
inline double MATING_CALL_RANGE = 2.0;    // a predator ready to breed that sees no mate seeks one this many times its sight off
constexpr double MAX_MATING_CALL_RANGE = 3.0;
inline double OMNIVORE_MATING_CALL = 0.25;  // an omnivore's milder mating call
inline double HUNTER_BREEDING_COST = 0.5;   // share of the usual breeding cost a predator or omnivore pays
inline double GRAZER_BREEDING_COST = 0.5;   // share of the usual breeding cost a grazer pays
inline double PREDATOR_STING = 0.5;         // share of a victim's spines a predator striking it takes
// A species grown scarce breeds more cheaply: below SCARCE_POPULATION living, its breeding cost falls evenly, to
// RARE_BREEDING_COST of the usual at RARE_POPULATION or fewer.
inline int SCARCE_POPULATION = 100;
inline int RARE_POPULATION = 10;
inline double RARE_BREEDING_COST = 1.0 / 3;
inline double FULL_LITTER_FOOD = 4.0;  // a full litter needs this many times the home food a mouth needs to breed; at the need, 1/this
inline int RARE_PREY = 1800;          // a hunter doesn't breed while its prey species (all together) number under this percentage of its own
inline int GRAZER_SCARCE_POPULATION = 300;          // grazers' cheap breeding begins below this many,
inline double GRAZER_RARE_BREEDING_COST = 1.0 / 9;  // and falls to this share of the usual cost at RARE_POPULATION or fewer
inline double PREDATOR_FULL_LITTER_FOOD = 8.0;      // a predator's full litter needs this many times the prey a predator needs to breed
// Predators glutting the world turn on one another: above PREDATOR_GLUT alive, their reluctance to hunt other kinds of
// predator wears away, to none at twice it; past that their own kind become prey too, fully at three times it. So do
// predators whose prey are dying out: below twice the rare-prey line, other kinds, to none at it; below
// that, their own kind too, fully with none left.
inline int PREDATOR_GLUT = 600;
// The share of the usual breeding cost a species of this many living pays: all of it at SCARCE_POPULATION or more,
// falling evenly to RARE_BREEDING_COST at RARE_POPULATION or fewer; for grazers, from GRAZER_SCARCE_POPULATION to
// GRAZER_RARE_BREEDING_COST.
inline double scarcity_share(int population, Species species = Species::Plant) {
    const bool grazer = species == Species::Grazer;
    const int scarce = grazer ? GRAZER_SCARCE_POPULATION : SCARCE_POPULATION;
    const double rare_cost = grazer ? GRAZER_RARE_BREEDING_COST : RARE_BREEDING_COST;
    if (population >= scarce) return 1.0;
    if (population <= RARE_POPULATION || scarce <= RARE_POPULATION) return rare_cost;
    return rare_cost + (1 - rare_cost) * (population - RARE_POPULATION) / (scarce - RARE_POPULATION);
}
inline double NEWBORN_FOOD = 15.0;    // parents conceive only as many young as can each start with this much of a stomach of food; 0: no limit
constexpr double BREEDING_COST_MOST = 25.0;  // the hunger the least fertile pay to breed: the most a parent could give its young
inline double PACK_MOUTHS = 0.5;      // a hunter's pack-mates count as this many mouths each on its food, sharing kills
inline double BLOODLUST = 1.0;        // how much more a predator values feasting whenever it sees a fresh kill
inline double CARRION_CRAVING = 1.0;  // how much more an animal values scavenging carrion it sees, at the highest
                                      // carrion trait; nothing up to the middle of the trait's range
inline double CARRION_AVERSION = 1.0;   // how much less a carrion-eater values hunting and attacking, at the highest carrion trait, likewise
inline double CARRION_COWARDICE = 1.0;  // a carrion-eater's least chance of breaking when struck, at the highest carrion trait, likewise
inline double ALERT_JOY = 0.1;        // pleasure of alerting its herd: to a hunter, for a grazer; to a kill, for a hunter
inline int ALARM_REACH = 6;           // an alarmed animal knows where the hunter is within this many steps
inline double PREDATOR_LEARNING = 1.0;  // a predator's brain learns this many times as fast, from its own choices and replays;
                                         // an omnivore's gets half the bonus
// Food that takes a kill is harder to learn than grass: a choice to hunt, attack or feast is learnt from
// KILL_FOOD_LEARNING times as much, by any animal, and one to eat, forage, browse or uproot PLANT_FOOD_LEARNING.
// Breeding and courting may be learnt from more than other choices, BREED_LEARNING and COURT_LEARNING times as much;
// by default no more, as learning them harder unsettles the brain for all else (their pleasures teach them instead).
inline double KILL_FOOD_LEARNING = 2.0;
inline double PLANT_FOOD_LEARNING = 0.5;
inline double BREED_LEARNING = 1.0;
inline double COURT_LEARNING = 1.0;
// Forced lessons: 1 learns from every turn, even one with no choice in it (resting through its night or exhausted,
// panicking or frozen); 0 learns only from the turns it chose what to do.
inline int FORCED_LESSONS = 0;
// How much more, or less, a choice is learnt from: for the food it seeks, or the mate.
double choice_learning(int choice);  // how much more, or less, a success from a choice is learnt from
// How much a lesson from a choice counts: a success (an outcome above 0) by the choice's learning weight, a failure
// as any other, so that a weight makes success more, or less, memorable without making failure so.
inline double lesson_weight(int choice, double outcome) { return outcome > 0 ? choice_learning(choice) : 1.0; }
// Primal fear: facing a hunter stronger than itself, an animal may be overcome, each turn, with a chance of
// PRIMAL_FEAR times how lopsided the fight would be (predators PRIMAL_FEAR_PREDATORS as often, and less
// the hungrier, none as they starve). While a
// hunter is in sight it then panics every turn, fleeing, or, with a chance of PRIMAL_FREEZE at the lowest
// intellect and none at the highest, freezes and does nothing at all.
// The instinct to hunt: whenever hunting or attacking is open to a predator, it values each this much more,
// times its hunger as a share, whatever its brain has learnt; the brain's learning adds to it.
inline double HUNTING_INSTINCT = 1.0;
constexpr int SIGHT_PER_TRAIT = 3;  // each step of the sight trait lets an animal see this many cells further
constexpr int MAX_PREDATOR_SIGHT = 3;
inline int PREDATOR_SIGHT = 3;  // predators see prey this many cells further than they otherwise see, up to MAX_PREDATOR_SIGHT
inline int HUNT_STEPS = 1;      // and chase it this many more steps a turn
inline double PRIMAL_FEAR = 1.0;
inline double PREDATOR_INTELLIGENCE = 2.0;  // a predator's intellect counts this many times over in all it does, up to the full range
inline double BRAIN_COST = 0.15;  // an animal's hunger burns this much faster at the highest inborn intellect, in proportion below
inline double PREDATOR_BRAIN_COST = 0.5;  // the share of that brain cost a predator pays: meat affords a big brain
inline double PREDATOR_FEAR = 0.5;  // a predator feels this share of its fear's effects: dread, panic, keeping close to its herd
inline int PREDATOR_LOOMS = 2;      // sizing up a fight for primal fear, a predator counts as this many steps larger and stronger
inline double PRIMAL_FEAR_PREDATORS = 0.0;  // predators are never overcome
inline double PRIMAL_FREEZE = 1.0;
inline std::array<double, 2> PANIC = {0.3, 1.0};
inline int COWARDLY_OMNIVORES = 1;  // 1: an omnivore struck always breaks and flees, whatever its fear; 0: it checks its morale
inline double THORN_FORCE = 15;
inline double THORN_SIZE_SHARE = 0.2;
inline double PACK_RIVALRY = 0.0;  // how much a predator's pack-mates count as rivals crowding its territory: its pack is no rival
inline int TERRITORY_RAMP = 3;
inline double RIVAL_UNEASE = 0.3;
inline double RIVALRY_WEAR = 0.5;  // how much an overcrowded territory wears down a hunter's reluctance to kill its own species
inline double RIVAL_KILL_JOY = 0.5;
inline int HERD_SPREAD = 3;
inline double HERD_JOY = 0.15;
inline double HERD_LONELINESS = 0.1;
inline int SOLITARY_OMNIVORES = 1;  // 1: omnivores live alone, in neither herd nor pack; 0: they herd with their own kind
// Whether a species lives alone, with no herd or pack: plants, and omnivores while SOLITARY_OMNIVORES.
inline bool solitary(Species species) {
    return species == Species::Plant || (species == Species::Omnivore && SOLITARY_OMNIVORES != 0);
}
inline double HERD_SAFETY = 0.6;
inline double HERD_DEFEND_JOY = 0.5;
inline double PACK_BONUS = 0.5;
// Predators whose ring colours differ by no more than this are one kind: they hunt together, and breed
// together as readily as other animals do with this much less difference.
inline double KIND_DIFFERENCE = 0.25;
// Breeding responsibly: before breeding, an animal weighs the food it eats within FORAGE_RADIUS
// against the mouths that share it, itself included. Below FOOD_NEEDED meals a mouth it doesn't
// breed; between that and twice that, its litter shrinks in proportion.
inline int FORAGE_RADIUS = 6;
inline double FOOD_NEEDED = 0.0;  // 0: no limit, and litters don't follow food
inline double UPROOT_FOOD = 3.0;  // a starving grazer uproots a plant only with fewer meals a mouth than this about it
inline double BREED_COOLDOWN = 1.0;  // grazers' and omnivores' share of the usual rest after breeding, 60 turns for the least fertile to 15 for the most
inline double PREDATOR_BREED_COOLDOWN = 0.5;  // the same for predators: a short rest
inline double PREDATOR_FOOD_NEEDED = 0.0;  // the same for predators: none, so the food about them never stops them breeding
// Plants each grazer needs in its home band, the climate band of its home temperature nearest it, shared
// among the grazers at home there, to breed; between this and twice this its litter shrinks. 0: no limit.
inline double PLANTS_PER_GRAZER = 0.1;
// The same for predators: the prey in its home band (the grazers and omnivores there, what it lives on), shared among
// the predators at home there. Below this a predator doesn't breed; between this and twice this its litter shrinks.
inline double PREY_PER_PREDATOR = 2.0;
inline double PLANT_MEALS = 2.0;   // meals a plant is worth to a full-efficiency browser
inline double PREY_MEALS = 10.0;  // meals a prey animal is worth: meat fills about ten times what grass does
inline double SHARED_DIET = 0.5;
// Prey switching: a hunter goes after a kind of prey only while it makes up at least SWITCH_SHARE
// of the food it weighs within FORAGE_RADIUS (for omnivores, grazers against plants; for predators,
// grazers against omnivores), so a scarce prey is left alone to recover.
inline double SWITCH_SHARE = 0.5;    // a prey is hunted while it is at least this share of the food
inline int OMNIVORE_PREDATOR_FOOD = 0;  // an omnivore hunts predators only while no more of its other food (grazers, plants and
                                        // grassy cells) is within FORAGE_RADIUS; a predator's infant with a grown one of its kind
                                        // within NURSING_REACH is guarded, and no prey to anyone
inline double STRIKE_EFFORT = 0.2;
inline double STRIKE_FORCE = 3.5;  // the best-armed, mightiest predator kills the largest, best-armoured grazer in three
inline double GRAZER_DEFENCE_WEAPONS = 0.5;    // share of its weapons a grazer brings to defence: striking back, and its spines
inline double OMNIVORE_DEFENCE_WEAPONS = 0.75;  // the same for an omnivore; a predator defends with all of its weapons
inline double PREDATOR_WEAPONS = 3.0;  // a predator's natural weapons count this many times over, striking and stinging;
                                        // an omnivore's three quarters of that and a grazer's half
inline int GRAZER_DEFENCE_STRENGTH = 5;  // a grazer can strike back only with strength above this
inline double FEAST_JOY = 1.5;   // a predator's pleasure in feasting on a fresh kill
inline double SATED_JOY = 1.0;   // pleasure in hunger relieved by eating, hunting, feasting or scavenging, per its full range
inline double ATTACK_JOY = 0.1;  // a predator's pleasure in a blow it lives through
inline double KILL_MEAL = 185;
inline double KILL_MEAL_FOR_SIZE = 200;  // meat is about ten times as filling as grass, and five times plants
inline double CARRION_MEAT = 30;
inline double CARRION_MEAT_FOR_SIZE = 50;
inline double GRAZER_SPEED = 1.5;
inline int FLEE_RANGE = 3;          // a hunter is only worth fleeing within this many steps, unless alarmed
inline double HERD_VIGILANCE = 0.5; // how far three herd-mates close by, at full instinct, shrink that range
inline double GRASS_OUTLOOK = 450;  // judging grass before breeding, count what regrows in this many turns
inline double STANDING_GRASS = 0.25;  // and this share of the grass standing now
inline double GRASS_MEAL = 18;  // hunger a tuft of grass sates: about a tenth of a middle-sized kill
// A plant is eaten in portions, a bite each: every bite sates PLANT_MEAL hunger (and PLANT_MEAL_FOR_SIZE more for the
// largest, none by default) and takes PLANT_BITE of its health, so a plant's food is in proportion to its size.
inline double PLANT_MEAL = 70;
inline double PLANT_MEAL_FOR_SIZE = 0;
inline double PLANT_BITE = 15;
inline double PLANT_REGROWTH = 0.02;
inline int PLANT_RECOVERY = 24;  // a day
// A desperate grazer, starving with food scarce about it, may uproot a plant beside it: it eats this
// many times a bite's meal, and tears this much of the plant's health away, recovering or not.
inline int UPROOT_HUNGER = 80;  // starving: hunger at least this
inline double UPROOT_MEALS = 0.5;  // share of a feast on a body of the plant's size an uprooting grazer eats
inline int UPROOT_BITE = 45;
inline std::array<double, 2> SEED_CHANCE = {0.03, 0.10};
inline double SEED_COST = 20;
inline double SEED_HUNGER = 40;
inline double SEEDLING_HUNGER = 50;
inline int SEED_REACH = 5;
inline int LIGHT_RADIUS = 3;  // plants compete for light with the plants within this many cells
inline int LIGHT_LIMIT = 3;   // each cuts a plant's chance of seeding by this share, to none at this many
inline int PLANT_CROWDING = 3;
inline double TEMPERAMENT_RELAPSE = 0.0005;
// Climate. The map is banded into zones across its height: ARCTIC_SHARE at the top and at the
// bottom is arctic, cold, its outermost POLAR_SHARE a polar cap, very cold all year; a strip of
// EQUATOR_SHARE across the middle is the equator, hot; the rest is temperate, medium. Every
// YEAR_LENGTH turns is a year, starting mild. Through its second quarter the north, from the top
// down to and through the equator, is a step colder; through its fourth the south, from the bottom up
// to and through the equator, is; but the polar caps never change, and a strip of WINTER_EQUATOR_SHARE
// across the equator's middle stays hot through both winters. Every Bitzkey has a home temperature,
// and may tolerate heat or cold, suffering HARDINESS of its penalties, and FRAILTY of the other's; plants tolerate nothing, and a plant
// spawned at the start is at home in its zone's usual temperature.
// Days: every DAY_LENGTH turns is a day, and a new world's year is 360 of them. Night covers half the map, its
// columns from the top to the bottom, and moves west round it once a day. With SLEEP, an animal rests through its
// night, the dark for the diurnal and the light for the nocturnal (see the activity gene): it may only rest, but for
// fleeing a hunter or striking back at what struck it, startled awake, and a flyer over the sea, which can't rest.
inline int DAY_LENGTH = 24;  // a turn is an hour
constexpr int FORESIGHT_DAY = 8;  // the day the Mind rules' discounts and trace decays are set for (see discount)
inline int SLEEP = 1;
// An infant within this many squares of a living parent is sheltered: the climate's extra hunger and harm pass it by.
constexpr int SHELTER_REACH = 2;
constexpr int DAYS_IN_YEAR = 360;  // four seasons of 90 days
inline int DEFAULT_YEAR_LENGTH = 24 * DAYS_IN_YEAR;  // a new world's, unless its settings say otherwise
inline int YEAR_LENGTH = DEFAULT_YEAR_LENGTH;  // each world sets it from its year length setting
inline double ARCTIC_SHARE = 0.2;
inline double EQUATOR_SHARE = 0.2;
inline double POLAR_SHARE = 0.025;  // at the top and again at the bottom: 5% of the map in all
inline double WINTER_EQUATOR_SHARE = 0.05;
inline double CLIMATE_HUNGER = 2.0;   // an animal a step or more from home, or a plant too hot, needs this many times the food
inline double CLIMATE_DAMAGE = 0.025;  // for each step from home, a Bitzkey loses this share of its full health a turn; plants from two
inline double WEATHER_GRACE = 4.0;  // days an animal endures out of its temperature, uneasy but unharmed, before the weather harms it; half as long for the least resilient, half as long again for the most; twice as long weather it is hardy to
inline double PLANT_CLIMATE_YIELD = 0.0;  // a plant out of its temperature gives this share of its food
inline int GRASS_DIEBACK = 60;     // where it isn't medium, a cell's grass loses a tuft every this many turns
// The weather turns in waves, one in each climate zone, all setting off together, so no zone waits on
// another. In each, a winter washes in from the side toward its hemisphere's edge toward the equator
// over the first SEASON_TURNING of its season, stays through the middle, and ebbs back the way it came
// over the last SEASON_TURNING (at most half each). The wave moves in bands of WAVE_BAND rows; at its
// front, WAVE_FRONT bands stand part way between the old weather and the new, in even steps, so the
// change comes as a staircase. The climate's effects move with it.
inline double SEASON_TURNING = 0.4;
inline int WAVE_BAND = 2;
inline int WAVE_FRONT = 4;
// A dead animal is a fresh kill, not yet carrion, through the turn it died and FRESH_KILL_TURNS more;
// then it starts to rot. A predator or omnivore may eat a fresh kill of what it lives on whole,
// FRESH_KILL_MEALS times the meal of making the kill, and nothing is left.
inline int FRESH_KILL_TURNS = 9;
inline double FRESH_KILL_MEALS = 3.0;
inline double SEED_VIABILITY = 0.75;  // years a seed that falls where it can't grow stays able to germinate
inline int PLANT_GESTATION = 30;  // turns a seed lies in the ground before it can sprout
inline double STARTING_TOLERANCE = 0.1;  // the chance a random starting genome carries each tolerance flag
inline double HARDINESS = 0.5;  // the share of exposure's extra hunger and damage an animal tolerant of it suffers
inline double FRAILTY = 2.0;    // the share an animal tolerant of the other extreme suffers
enum Temperature : int { Hot, Medium, Cold, VeryCold };
constexpr std::array<std::string_view, 4> SEASONS = {"mild", "northern winter", "mild", "southern winter"};

int season(int turn);  // the quarter of the year a turn falls in, 0 to 3
int year(int turn);    // the year a turn falls in, from 1
int zone_temperature(int y, int height);       // a row's temperature outside the winters
// Whether a row is in winter in a quarter of the year: the north and the equator in the second, the
// south and the equator in the fourth.
bool in_winter(int y, int height, int quarter);
int season_temperature(int y, int height, int quarter);  // a row's settled temperature in a quarter of the year
// As the weather turns into a quarter of the year, which band of its zone's wave a row is in, counted
// in the order the wave reaches them, and how many bands the wave crosses. Every zone's wave sets off
// at once. A winter washes in from the side toward its hemisphere's edge toward the equator (the
// second and fourth quarters), and ebbs back the other way (the third and first).
std::pair<int, int> zone_rows(int y, int height);  // the first and last rows of a row's climate zone
std::pair<int, int> wave_band(int y, int height, int quarter);
// How far a winter's wave has washed in: 0 as the winter begins, 1 once SEASON_TURNING of it has
// passed; always 1 in the mild seasons.
double season_progress(int turn);
// A row's temperature on a turn, from 0 (hot) to 3 (very cold): its mild one, its winter's, or part
// way between the two while a winter's wave washes in or ebbs out across it.
double temperature_at(int y, int height, int turn);
// A cell's fixed share, 0 to 1, below which a row's shortfall from medium makes its grass die back.
double dieback_threshold(int x, int y);
constexpr int MAX_SIGHT = 2 + SIGHT_PER_TRAIT * TRAIT_MAX + MAX_PREDATOR_SIGHT;  // the furthest anything is seen: a predator's sight for prey
// For each territorial species, predators then omnivores: how many rivals it tolerates from the
// least to the most empathetic, and how strongly overcrowding moves it (its rivalry at most).
struct Territory {
    int fewest, most;
    double strength;
};
inline std::array<Territory, 2> TERRITORY = {{{1, 5, 1.0}, {2, 10, 0.5}}};
// Separate niches, by species: grazers are the specialists, getting more from grass and plants than
// anyone; omnivores live on meat, getting only three quarters of the food in plants and half of grass.
inline std::array<double, 4> GRAZING = {0.0, 1.25, 0.5, 0.0};   // share of the food in grass each species gets
inline std::array<double, 4> BROWSING = {0.0, 1.25, 0.75, 0.0}; // share of the food in plants
// How experiences reshape temperament, in shares of a trait's full range before scaling by
// plasticity: for each experience, the mind traits it shifts.
struct Shift {
    int trait;
    double change;
};
constexpr std::array<std::string_view, 14> EXPERIENCE_NAMES = {
    "kill", "hit", "stung", "wounded", "wounded_alone", "bred", "bred_into_hunger", "starving", "foraged_meal",
    "discovery", "ambushed", "defended", "panicked", "lesson"};
extern std::array<std::vector<Shift>, 14> EXPERIENCES;

struct Cell {
    int x = 0, y = 0;
    bool operator==(const Cell&) const = default;
};

struct Place {  // a place remembered, and the turn it happened there
    int x = 0, y = 0, turn = 0;
};

struct Bitzkey {
    Bitzkey(int identifier, Dna dna, int x, int y);
    // Work out again what its genome makes of it under the rules as they now stand: its health pool,
    // metabolism, stomach, burn, might, toughness, steps, diet, sight, lifespan, breeding cost and digestion.
    void derive();

    int identifier;
    Dna dna;
    int x, y;
    bool wet = false;  // in the sea, as the world keeps it while it moves
    double hunger = 0.0;
    double health = 100.0;
    double stamina = FULL_STAMINA;  // spent by acting, restored by resting (see ACTIVE_SHARE)
    int age = 0;
    int cooldown = 0;
    std::string_view last_action = "new";
    std::shared_ptr<Brain> brain;  // shared with history snapshots; copied before it learns
    int generation = 0;
    int children = 0;
    int mother = -1, father = -1;  // its parents' identifiers; -1 for one spawned, imported or deployed
    std::optional<int> died_turn;
    std::optional<int> assailant;       // the animal that struck it since it last acted
    bool panicked = false;              // failed a morale check when struck
    bool primal_fear = false;           // overcome by a stronger hunter: it panics, or freezes, while one is in sight
    double rivalry = 0.0;               // how overcrowded its territory was with rivals
    double own_rivalry = 0.0;           // the same, counting only its own species
    bool hunts_grazers = true;          // hunters: whether grazers were common enough to hunt
    bool hunts_omnivores = true;        // predators: the same for omnivores
    bool hunts_predators = true;        // omnivores: whether its other food was scarce enough to hunt predators
    bool guarded = false;               // a predator's infant: whether a grown one of its kind was close by as the turn began
    std::optional<int> alarm;           // what struck a herd-mate close by since it last acted, or it was alerted to
    std::optional<Cell> kill_alert;     // where a pack-mate alerted it to a fresh kill since it last acted
    double seen_success = 0.0;          // a herd-mate's success it saw, fading turn by turn
    double seen_failure = 0.0;          // a herd-mate's failure it saw, the same
    double weathered = 0.0;             // turns endured out of its temperature, one regained each turn back in it; the weather harms it once past its grace
    std::optional<int> herd_assailant;  // what struck a herd-mate close by, from alongside it
    std::optional<Place> food_memory;    // where it last fed
    std::optional<Place> danger_memory;  // where it was last struck
    double meat = 0.0;                  // a corpse's meat left for scavengers
    double meat_fill = 0.0;             // how much of its fullness is meat, which it digests more slowly
    int population = 0;                 // how many of its species live, as of the turn's start: its awareness of its own numbers
    double cost_share = 1.0;            // the share of its breeding cost its species' scarcity leaves it, as of the turn's start
    double glut = 0.0;                  // for a predator, what drives it to hunt other predators: 1 for other kinds fully, 2 its own kind
    double rot = 0.0;                   // meat lost to rot each turn
    int portions = 0;                   // a body's portions left to eat, feasting or scavenging: one for each size class
    std::vector<std::pair<int, int>> trail;  // the cells it passed through this turn, for the map
    struct Memory {  // an experience to re-learn from
        Senses senses{};
        int choice = 0;
        double target = 0.0;
        double weight = 1.0;  // how much it counted, by what came of it
    };
    std::vector<Memory> memories;  // its most recent experiences, newest last
    int gpu = -1;  // its brain's handle on the GPUs, when the world thinks there
    int synced = -1;  // the turn its brain was last brought back from them
    struct Young {  // conceived at mating, born at the end of the gestation
        Dna dna = 0;
        Mind mind{};
        std::shared_ptr<Brain> brain;
        double energy = 0.0;
        int generation = 0;
        int father = -1;
    };
    std::vector<Young> womb;      // the young it carries
    std::uintmax_t brain_bytes() const;  // the memory its brain, and those of the young it carries, take
    std::optional<int> due_turn;  // the turn they are born
    Mind mind{};                        // current temperament, mind and social traits, reshaped by experience
    // The last decision, kept until the next one so the brain can learn from how it turned out.
    bool has_last_senses = false;
    Senses last_senses{};
    int last_choice = 0;
    bool last_forced = false;  // whether it had no choice but last_choice
    double last_wellbeing = 0.0;
    double last_comfort = 0.0;  // how comfortable it felt at its last decision
    double reward = 0.0;
    std::array<double, ACTION_COUNT> odds{};  // chance of each action at the last choice
    std::uint32_t odds_offered = 0;           // which actions were open then, one bit each

    // Fixed by the genome.
    Species species;
    Terrain terrain = Terrain::Terrestrial;  // kept up by derive()
    bool nocturnal = false;                  // active by night, resting by day; else the other way round (derive())
    Traits inborn;
    Body body;
    double max_health, metabolism, stomach, burn, might, toughness, lifespan, breeding_cost, digestion;
    Climate climate;
    double grazing;   // share of the food in grass it gets
    double browsing;  // share of the food in plants
    int steps, sight;
    // In its infancy, the first FEEBLE_YOUTH of its lifespan, or its old age, past AGEING of it, an animal is
    // feeble: its body traits and health are at FEEBLE_SHARE. Plants never are. Kept up by derive().
    bool feeble = false;

    // A body trait as it is now: its genes' value, or FEEBLE_SHARE of it while feeble; an avian animal's size,
    // weapons and armour AVIAN_BODY of that.
    int trait(int name) const {
        if (name == SpeciesTrait) return body[name];
        double value = feeble ? static_cast<int>(body[name] * FEEBLE_SHARE) : body[name];
        if (terrain == Terrain::Avian && species != Species::Plant && (name == Size || name == Weapons || name == Armour)) value *= AVIAN_BODY;
        return static_cast<int>(value);
    }
    // Whether it may be in the sea, or on the land, by its terrain.
    bool can_be(bool sea) const { return fits(terrain, sea); }
    bool exhausted() const { return species != Species::Plant && stamina <= 0; }  // it can only rest
    // The squares it goes a step from where it stands: its steps, but an amphibious animal on land AMPHIBIOUS_SPEED of
    // them, rounded half up, at least one.
    int pace() const {
        return terrain == Terrain::Amphibious && species != Species::Plant && !wet ? std::max(1, static_cast<int>(steps * AMPHIBIOUS_SPEED + 0.5))
                                                                                    : steps;
    }
    bool aloft_at_sea() const { return terrain == Terrain::Avian && species != Species::Plant && wet; }  // it can't rest
    double physique(int name) const { return share(trait(name)); }  // a body trait, as it is now, as a share of its range
    // Whether it is feeble at its age: if that has changed, its body is worked out afresh.
    void grow() {
        if (feeble != feeble_at_age()) derive();
    }
    // Whether it is an animal in its infancy: the first FEEBLE_YOUTH of its lifespan, a predator's PREDATOR_INFANCY as long.
    // How long its infancy lasts, in turns: FEEBLE_YOUTH of its lifespan (a predator's PREDATOR_INFANCY as long), the
    // smallest SMALL_MATURITY of that, the larger more of it.
    double infancy() const {
        const double maturity = SMALL_MATURITY + (1 - SMALL_MATURITY) * share(body[Size]);
        return FEEBLE_YOUTH * (species == Species::Predator ? PREDATOR_INFANCY : 1.0) * maturity * lifespan;
    }
    bool infant() const { return species != Species::Plant && age < infancy(); }
    bool feeble_at_age() const { return species != Species::Plant && (age < infancy() || age > AGEING * lifespan); }
    double feeling(int trait) const { return share(mind[trait]); }
    // Its intellect now, as a share of the full range, as it counts in all it does: a predator's
    // PREDATOR_INTELLIGENCE times over, up to the full range.
    double intelligence() const { return std::min(1.0, feeling(Intellect) * (species == Species::Predator ? PREDATOR_INTELLIGENCE : 1.0)); }
    // Its fear, as its effects feel it: a predator's PREDATOR_FEAR as much, and less the hungrier it is.
    double fearfulness() const { return feeling(Fear) * (species == Species::Predator ? PREDATOR_FEAR * unafraid() : 1.0); }
    // The share of its fear's effects hunger leaves a predator: all when fed, falling with its hunger to none
    // as it starves; any other animal feels them all.
    double unafraid() const { return species == Species::Predator ? std::max(0.0, 1 - hunger / 100) : 1.0; }
    double herd_instinct() const { return std::min(feeling(Empathy), feeling(Patience)); }
    // How much a carrion-eater it is: 0 up to the middle of its carrion trait's range, rising to 1 at the top.
    double scavenger() const { return std::max(0.0, 2 * physique(Carrion) - 1); }
    Profile profile() const;
    // Ready, not carrying young, and able to pay for breeding with energy to spare.
    bool can_breed() const { return in_heat && cooldown == 0 && womb.empty() && hunger + breeding_price() <= 100 - BREED_RESERVE; }
    bool in_heat = true;  // in heat now, and so fertile: kept turn by turn (see Simulation::heat)
    double breeding_price() const { return breeding_cost * cost_share; }  // what it pays to breed now, its species' scarcity counted
    bool alive() const { return health > 0; }  // full hunger wears its health away (STARVING_DAMAGE) rather than killing
    // Eat: the same food fills a small stomach more than a large one. Meat is kept track of, as it lasts longer.
    void nourish(double food, bool is_meat = false) {
        const double eased = relieve(food / stomach);
        if (is_meat) meat_fill += eased;
    }
    // Ease its hunger by an amount, eating, and take SATED_JOY's pleasure in what was eased. Returns what was eased.
    double relieve(double amount) {
        const double eased = std::min(hunger, amount);
        hunger -= eased;
        reward += SATED_JOY * eased / 100;
        return eased;
    }
    // The hunger a turn's burn brings: the share of its fullness that is meat empties at MEAT_DIGESTION of the rate.
    double digest(double burned) {
        const double fullness = 100 - hunger;
        meat_fill = std::min(meat_fill, std::max(0.0, fullness));
        const double meat_share = fullness > 0 ? meat_fill / fullness : 0.0;
        meat_fill = std::max(0.0, meat_fill - burned * meat_share * MEAT_DIGESTION);
        return burned * (1 - meat_share * (1 - MEAT_DIGESTION));
    }
    int space() const { return 1 + trait(Size); }  // the spaces of a square it takes
    void heal(double amount) { health = std::min(max_health, health + max_health * amount * metabolism); }
    Brain& own_brain();  // the brain, made this animal's own before it changes
    // How many steps a temperature is from home.
    double climate_steps(double here) const { return std::abs(here - climate.temperature); }  // part steps while the weather turns
    // The share of exposure's penalties it suffers here: HARDINESS for an animal tolerant of heat
    // where it is hotter than home, or of cold where it is colder; FRAILTY for one tolerant of the
    // other; otherwise all of them. Plants tolerate nothing.
    double exposure_share(double here) const;
    Rgb dot_color() const { return color_from_word(mind_word(dna)); }
    Rgb ring_color() const { return color_from_word(body_word(dna)); }
    Rgb social_color() const { return color_from_word(social_word(dna)); }
};

inline double fatigue(const Bitzkey& agent, double cost) {
    cost *= stamina_pace();
    return agent.terrain == Terrain::Amphibious ? cost * AMPHIBIOUS_FATIGUE : cost;
}

int litter_size(std::span<const std::pair<int, int>> parents);
// A pleasure of mating as an animal feels it: LIBIDO_IN_JOY of it by its libido, the rest whatever it is.
inline double mating_joy(const Bitzkey& agent, double joy) { return joy * (1 - LIBIDO_IN_JOY + LIBIDO_IN_JOY * agent.feeling(Libido)); }
int gestation(int size);  // turns an animal of this body size carries its young
// The genome with its species trait made a predator's (3, or 7 where its top bit was set: the species is the
// trait mod 4), the rest unchanged.
Dna predator_dna(Dna dna);
// A child's genome with its species moved along the chain of evolution, grazer, omnivore, predator, by a
// mutation of the species: a flip landing on predator makes any animal a predator; otherwise a flip of the
// species' lower bit moves it a step up the chain, and of its upper bit a step down. A step off the chain is
// undone, a plant stays a plant and an animal never becomes one; the rest of the genome is as it is.
Dna evolved(Dna parent, Dna child);
// An inborn intellect as it counts for the brain's shape and how much it replays: a predator's
// PREDATOR_INTELLIGENCE times over, up to the full range.
int thinking_intellect(int intellect, Species species);
// The depth and width of a genome's brain, by its thinking intellect: deeper up to MAX_DEPTH, then wider.
std::pair<int, int> brain_shape(Dna dna);

enum class EndReason { None, Extinct, Full };

class Simulation {
public:
    struct Settings {
        int width = GRID_SIZE;
        int height = GRID_SIZE;
        int population = INITIAL_POPULATION;
        std::optional<std::uint64_t> seed;
        double grass_density = GRASS_DENSITY;
        std::vector<Imported> imported;
        std::optional<int> population_cap;
        double mind_mutation = 100 * MIND_MUTATION_CHANCE;
        double body_mutation = 100 * BODY_MUTATION_CHANCE;
        // Turns in a year, for the seasons and lifespans; like the advanced rules, it holds for the
        // whole program while the world runs.
        int year_length = DEFAULT_YEAR_LENGTH;
        // Percent of the map under the sea, laid out by the seed; none unless asked for (a new world's setup offers
        // DEFAULT_OCEAN).
        double ocean = 0.0;
    };

    explicit Simulation(Settings settings);
    Simulation(int width, int height, int population, std::optional<std::uint64_t> seed);

    int width() const { return width_; }
    int height() const { return height_; }
    int turn() const { return turn_; }
    int deaths() const { return deaths_; }
    // Every death so far, by species and then cause.
    using DeathCauses = std::array<std::map<std::string, int, std::less<>>, 4>;
    const DeathCauses& death_causes() const { return death_causes_; }
    int next_identifier() const { return next_identifier_; }
    std::optional<int> population_cap() const { return population_cap_; }
    void set_population_cap(std::optional<int> cap) { population_cap_ = cap; }
    double mind_mutation() const { return mind_mutation_; }
    double body_mutation() const { return body_mutation_; }
    void set_mutation(double mind, double body) { mind_mutation_ = mind; body_mutation_ = body; }

    const std::vector<std::unique_ptr<Bitzkey>>& agents() const { return agents_; }
    std::vector<Bitzkey*> living() const;
    int grass_at(int x, int y) const;
    // The sea: whether a square lies under it (none off the map, nor in a world without sea).
    // Whether a column lies in the night now: half the map, moving west a whole turn round each day.
    bool night(int x) const;
    int night_start() const;  // the westernmost column of the night, which runs east half the map's width from it
    // Whether an animal is in its night where it stands: dark for the diurnal, light for the nocturnal.
    bool resting_time(const Bitzkey& agent) const { return agent.nocturnal != night(agent.x); }
    // Whether an infant is sheltered: a living parent within SHELTER_REACH of it. It is spared the climate.
    bool sheltered(const Bitzkey& agent) const;
    bool sea(int x, int y) const { return !sea_.empty() && in_bounds(x, y) && sea_[static_cast<std::size_t>(y) * width_ + x]; }
    bool has_sea() const { return !sea_.empty(); }
    const std::vector<std::uint8_t>& sea_map() const { return sea_; }  // a square a cell, row by row; empty without sea
    double ocean() const { return ocean_; }  // the share of the map asked to be sea, in percent
    // Whether a Bitzkey of this terrain may be in a square, sea or land.
    bool suits(Terrain terrain, Cell cell) const { return fits(terrain, sea(cell.x, cell.y)); }
    bool can_enter(const Bitzkey& agent, Cell cell) const { return suits(agent.terrain, cell); }
    // The biomes, each its squares, in the order they are first met row by row (see BIOME_GROUPS); those smaller
    // than BIOME_SIZE left out.
    std::vector<std::vector<Cell>> biomes() const;
    int& grass_cell(int x, int y) { return grass_[static_cast<std::size_t>(y) * width_ + x]; }
    void clear_grass() { std::fill(grass_.begin(), grass_.end(), 0); }
    std::size_t grass_tufts() const;
    const Insights& insights() const { return insights_; }
    const Census& census() const { return census_; }
    // The genome of a Bitzkey from lineage_first() on; those before it are off the graph and forgotten.
    Dna lineage(int identifier) const { return lineage_.at(static_cast<std::size_t>(identifier - lineage_first_)); }
    bool remembered(int identifier) const {
        return identifier >= lineage_first_ && identifier - lineage_first_ < static_cast<int>(lineage_.size());
    }
    int lineage_first() const { return lineage_first_; }
    using DeathRecord = Death;  // each death of the last RECENT_DEATH_TURNS turns, by the dead Bitzkey's identifier
    const DeathRecord* death_record(int identifier) const {
        const auto found = death_records_.find(identifier);
        return found == death_records_.end() ? nullptr : &found->second;
    }
    std::vector<Death> deaths_so_far() const;  // every death of the last RECENT_DEATH_TURNS turns
    const DeathTally& death_tally() const { return death_tally_; }  // every death since the world began, counted
    PyRandom& random() { return random_; }
    double grass_regrowth() const { return grass_regrowth_; }
    // The advanced rules were changed while the world runs: work out afresh what the world settled from them as
    // it began, the grass that grows back each turn and its climate bands, and what each Bitzkey's genome makes
    // of it. Everything else reads the rules as it goes.
    void rules_changed();
    // Put DEPLOY_GROUP of each of these Bitzkeys, each with its own copy of its saved brain and temperament,
    // within SPAWN_SPREAD cells of a cell of the running world, between its turns. A world rewound lets the future
    // it had go, as a turn played afresh would. Returns those deployed.
    std::vector<Bitzkey*> deploy(const std::vector<Imported>& bitzkeys, int x, int y);
    // A row's temperature now, part way between two while the weather turns.
    double temperature(int y) const { return climate_[static_cast<std::size_t>(y)]; }
    // How well grass grows in a row: fully where it is medium, less the further it has turned from medium.
    double growth(int y) const { return growth_[static_cast<std::size_t>(y)]; }
    // A seed waiting where it fell for the temperature it can grow in.
    struct Seed {
        int x, y;
        Dna dna;
        int generation, sown;
        bool operator==(const Seed&) const = default;
    };
    std::vector<Seed>& seeds() { return seeds_; }
    const std::vector<Seed>& seeds() const { return seeds_; }
    const std::vector<int>& grass_rows() const { return grass_rows_; }  // the rows where grass grows at all
    void set_grass_regrowth(double tufts) { grass_regrowth_ = tufts; }

    // Step one turn, simulating it, unless the world has ended.
    void advance();
    // Called after each new turn is simulated: to watch the world as it goes.
    std::function<void(const Simulation&)> on_turn;
    EndReason end_reason() const;
    bool ended() const { return end_reason() != EndReason::None; }

    // The rest is public so tests can drive single steps, as the Python tests do.
    // Which way an animal breaks ties between targets equally near, or flees one straight in line: east (1) or west (-1),
    // south (1) or north (-1), turning with its number and the turn, so no way is favoured.
    struct Side {
        int x, y;
    };
    Side tie_side(const Bitzkey& agent) const;
    void take_turn();
    // The rows a group's centre may fall in whose usual temperature is `home`, or nearest it: a very cold
    // home has no zone of its own outside the winters, so it takes the arctic.
    std::vector<int> home_rows(int home) const;
    Bitzkey& add_agent(Dna dna, int x, int y, std::shared_ptr<Brain> brain = nullptr,
                       const std::optional<PartialMind>& mind = std::nullopt);
    void index_cells();
    void update_climate();  // each row's temperature this turn, and the medium rows grass grows in
    // How the climate killed it: colder than home, it froze; hotter, it overheated.
    static std::string_view exposure(const Bitzkey& agent, double here);
    void seed_plant(Bitzkey& plant);
    void germinate();

    struct Situation {
        Bitzkey* mate = nullptr;
        std::vector<Cell> nurseries;
        Bitzkey* prey = nullptr;
        Bitzkey* threat = nullptr;
        int threats = 0;
        Bitzkey* quarry = nullptr;
        Bitzkey* hunter = nullptr;
        Bitzkey* suitor = nullptr;
        std::optional<Cell> suitor_at;  // where it was when seen
        std::optional<Cell> grass;
        bool grass_here = false;
        Bitzkey* carrion = nullptr;
        Bitzkey* kill = nullptr;
        Bitzkey* flora = nullptr;
        Bitzkey* alert_to = nullptr;  // what it may alert its herd to
        std::optional<Cell> revisit;  // where it last fed, to go back to
        std::vector<Bitzkey*> young;  // its own infants within its sight, to feed
        Bitzkey* assailant = nullptr;
        double rivalry = 0.0;
        int kin = 0;
        int others = 0;
        Bitzkey* defend_target = nullptr;
        Bitzkey* uprootable = nullptr;  // a plant beside a starving grazer, with food scarce
        bool for_herd = false;
        bool herd = true;
        int herd_near = 0;
        std::optional<Cell> herd_middle;
        double exposure = 0.0;  // how far from home it is, 0.5 a step to 1, times the share of exposure it suffers
        double home_distance = 0.0;  // how far the nearest row it is at home in lies, as sensed, times the same share
        int young_near = 0;  // its own young, still infants, within its sight
        bool panic = false;
        bool frozen = false;  // in primal fear, frozen: it does nothing this turn
    };
    struct Sensed {
        Senses senses{};
        std::vector<int> options;
        Situation situation;
    };

    // A hunter's view of its territory: how crowded with rivals, and whether its prey are common enough to hunt.
    void survey(Bitzkey& agent);
    Sensed sense(Bitzkey& agent, bool surveyed = false);  // surveying first, unless it already has this turn
    void think(Bitzkey& agent);  // one animal senses, learns, chooses and acts
    // Animals think at once, in phases, so that their brains can be worked as one batch: all sense the
    // world as it stands; all their brains value the actions open to them; all learn from their
    // previous decisions; all choose; then each acts in turn, finding the world as those before it left
    // it. What herd-mates learn from watching is learnt last, all together.
    void think_all(const std::vector<Bitzkey*>& agents);
    void learn_witnessed();  // herd-mates learn from what they saw this turn, each brain in the order it saw it
    void conceive();  // make the brains of the young conceived since it last did, all at once
    // Bring the living's brains back from the GPUs into their CPU copies, before anything reads their
    // weights: an export, the catalog, the brain report. Nothing to do on the CPU.
    void sync_brains();
    // Only the brains whose handles fall, counted round `parts`, in the `count` from `from` on: a share at a time.
    void sync_brains(int from, int count, int parts);
    bool on_gpu() const { return gpu_ != nullptr; }
    // Milliseconds spent on each part of the turns so far, for finding where the time goes.
    struct Timings {
        double upkeep = 0, sense = 0, think = 0, learn = 0, choose = 0, act = 0, after = 0;
        // Within acting: each action's share, how often it was taken, and the lessons of what herds saw.
        std::array<double, ACTION_COUNT> actions{};
        std::array<long long, ACTION_COUNT> taken{};
        double witnessed = 0;
    };
    Timings timings;
    const std::string& gpu_status() const { return gpu_status_; }  // which GPUs think, or why none do
    // The GPUs failed (out of memory, most likely): every brain comes back from them, as far as it still can, and the
    // world thinks on the CPU from here on, rather than stop.
    void leave_gpu(const std::exception& error);
    // Think from here on with `engine`, or on the CPU without one: every living brain is brought back from the engine
    // it is on, as it now is, and put on the new one.
    void use_engine(std::unique_ptr<gpu::BrainEngine> engine, std::string status);
    // The brain whose handle on the engine this is, as it was last brought back from it; none if no living brain's.
    const Brain* engine_brain(int handle) const;
    void act(Bitzkey& agent, int action, Situation& situation);  // and remembers where it fed, and tires (see ACTIVE_SHARE)
    // How clearly it remembers a place: 1 the turn it happened, fading to nothing over MEMORY_TURNS times its
    // memory; 0 when it has nothing to remember.
    double remembered(const Bitzkey& agent, const std::optional<Place>& memory) const;
    double comfort(const Bitzkey& agent, const Situation& situation) const;
    double wellbeing(const Bitzkey& agent) const;
    // How much the future counts against the present, a turn: more for the patient. The discounts and trace decays are
    // set for a turn of a FORESIGHT_DAY-turn day, and kept to the same span of days whatever the day's length.
    static double discount(const Bitzkey& agent) {
        return per_turn(IMPATIENT_DISCOUNT + (PATIENT_DISCOUNT - IMPATIENT_DISCOUNT) * agent.feeling(Patience));
    }
    // How much of a choice's credit carries on to the next, a turn, beside the discount: more for the clever.
    static double trace_decay(const Bitzkey& agent) {
        return per_turn(TRACE_DECAY + (SMART_TRACE_DECAY - TRACE_DECAY) * agent.intelligence());
    }
    static double per_turn(double rate) { return std::pow(rate, static_cast<double>(FORESIGHT_DAY) / DAY_LENGTH); }
    // How much warmer than home a row feels, as a sense: 0.5 a step, from -1 (two or more steps too
    // cold) to 1 (two or more too hot).
    double warmth(const Bitzkey& agent, int y) const;
    // Record a death, with what killed it and what the killer was doing.
    void die(Bitzkey& agent, std::string_view cause, const Bitzkey* killer = nullptr, std::string_view activity = {});
    void attack(Bitzkey& agent, Bitzkey& victim, std::string_view activity = "attacking");  // a blow, hunting or attacking
    void defend(Bitzkey& agent, Bitzkey& assailant, bool for_herd = false);
    void struck(Bitzkey& victim, const Bitzkey& striker);
    void exhausted(Bitzkey& agent);
    void breed(Bitzkey& first, Bitzkey& second);
    // Whether an animal is in primal fear this turn. With no hunter in sight it is calm again. Facing one
    // stronger than itself, it may be overcome, more likely the more lopsided the fight; a predator a third
    // as often. Once overcome, it stays so while a hunter is in sight.
    bool primal_fear(Bitzkey& agent, const Bitzkey* hunter);
    // How much stronger an opponent is, 0 (no stronger) to 1 (overwhelmingly): weighing the share of each
    // other's health one blow takes, with every trait that counts in a fight.
    static double disparity(const Bitzkey& agent, const Bitzkey& opponent);
    // One blow, as damage(), and an animal's health, with each side's size and strength as they count in sizing
    // up a fight: a predator's PREDATOR_LOOMS steps more of each (past the full range, for the largest).
    static double looming_damage(const Bitzkey& attacker, const Bitzkey& defender);
    static double looming_health(const Bitzkey& animal);
    // The young it carries are born on the free cells nearest it, as many as there are cells and the
    // population cap allows; the rest are lost. Each starts with its share of the energy its parents gave.
    void give_birth(Bitzkey& mother);
    void browse(Bitzkey& agent, Bitzkey& plant);
    // A starving grazer, with food scarce about it, tears into a plant beside it, recovering from a
    // bite or not: it eats UPROOT_MEALS times a bite's meal, and the plant loses UPROOT_BITE health,
    // often all it has. An armed plant still strikes back.
    void uproot(Bitzkey& agent, Bitzkey& plant);
    // The climate zones from top to bottom, each the rows it covers outside the winters.
    std::vector<std::vector<int>> zones() const;
    void alert(Bitzkey& agent, const Bitzkey& about);
    void feed_young(Bitzkey& parent, const std::vector<Bitzkey*>& young);  // its infants about it fed full, at no cost to it  // alert its herd to a hunter, or a fresh kill
    void scavenge(Bitzkey& agent, Bitzkey& corpse);
    void feast(Bitzkey& agent, Bitzkey& corpse);
    void eat_portion(Bitzkey& corpse);  // a portion of a body eaten: once the last is, the body is gone at the turn's end
    bool fresh(const Bitzkey& corpse) const;  // still a fresh kill, not yet carrion
    Bitzkey* nearest_kill(const Bitzkey& agent, int sight) const;  // of something it lives on
    void seed_corpse(const Bitzkey& corpse);  // a body rotted away leaves the seed of a new plant where it lay
    void wither_grass();
    void sow(int tufts);
    void experience(Bitzkey& agent, int event);
    std::vector<Bitzkey*> nearby(const Bitzkey& agent, int radius) const;
    // The Bitzkeys nearby() lists, in its order, walked one by one without making the list: for a loop that only
    // reads them, or changes nothing that decides who is nearby or alive while it walks.
    class Around {
    public:
        class iterator {
        public:
            Bitzkey* operator*() const { return (*list_)[inner_]; }
            iterator& operator++() {
                ++inner_;
                settle();
                return *this;
            }
            bool operator!=(const iterator& other) const { return offset_ != other.offset_ || inner_ != other.inner_; }

        private:
            friend class Around;
            iterator(const Around& around, std::size_t offset) : around_(&around), offset_(offset) {}
            void settle();  // on to the next one living, and not the one left out, or to the end
            const Around* around_;
            std::size_t offset_;
            std::size_t inner_ = 0;
            const std::vector<Bitzkey*>* list_ = nullptr;
        };
        iterator begin() const;
        iterator end() const;

    private:
        friend class Simulation;
        Around(const Simulation& simulation, Cell cell, int radius, const Bitzkey* except)
            : simulation_(simulation), cell_(cell), radius_(radius), except_(except) {}
        const Simulation& simulation_;
        Cell cell_;
        int radius_;
        const Bitzkey* except_;
    };
    Around around(const Bitzkey& agent, int radius) const { return Around(*this, {agent.x, agent.y}, radius, &agent); }
    // The Bitzkey of this identifier, living or dead, while the world still holds it; agents_ stays in the order
    // they were made, so it is found by halving.
    Bitzkey* find_agent(int identifier) const;
    // Whether an animal is in heat now, where it stands (see HEAT_CLOSEST).
    bool heat(const Bitzkey& agent) const;
    // Its brain as it now is, brought back from the GPUs if it thinks there: to read it.
    void sync_brain(Bitzkey& agent) {
        Bitzkey* one[] = {&agent};
        sync(one);
    }
    std::vector<Bitzkey*> nearby_cell(Cell cell, int radius, const Bitzkey* except) const;
    std::vector<Cell> free_cells_near(const Bitzkey& agent) const;  // squares near it, its own first, with any room
    std::vector<Cell> squares_near(const Bitzkey& agent) const;     // its own square, then those around, nearest first
    int room_in(Cell cell, const Bitzkey* leaving = nullptr) const;  // a square's free spaces, as though `leaving` were gone
    // A square with room for a Bitzkey taking `space`, of a terrain it may be in: this one, or else the nearest within
    // BIRTH_REACH.
    std::optional<Cell> spot_near(Cell cell, int space, Terrain terrain = Terrain::Terrestrial) const;
    bool step(Bitzkey& agent, int x, int y);  // a step into a square, if it has room; whether it took it
    std::pair<double, double> territory(const Bitzkey& hunter) const;
    struct HerdView {
        bool any = false;
        int close = 0;
        std::optional<Cell> middle;
    };
    HerdView herd(const Bitzkey& agent) const;
    double room() const;
    // Other animals, besides its partner, within its sight; with `own_kind`, only those of its own kind.
    int crowding(const Bitzkey& agent, const Bitzkey& partner, bool own_kind = false) const;

    // One blow; struck in defence, a grazer's or omnivore's weapons count for less.
    static double damage(const Bitzkey& attacker, const Bitzkey& defender, bool defending = false);
    static double defence_share(const Bitzkey& animal);  // the share of its weapons an animal brings to defence
    // Its natural weapons as a share of their full range: a predator's count PREDATOR_WEAPONS times over, an
    // omnivore's three quarters and a grazer's half as many times.
    static double armed(const Bitzkey& animal);
    static bool compatible(const Bitzkey& first, const Bitzkey& second);
    static bool willing(const Bitzkey& agent, const Bitzkey& other);
    static bool same_kind(const Bitzkey& first, const Bitzkey& second);
    static double cannibal_reluctance(const Bitzkey& animal);
    static double rival_weight(const Bitzkey& hunter, const Bitzkey& other);
    // Whether an animal's blows would bring the opponent down as fast as the opponent's would bring it down,
    // each as it stands now.
    static bool would_win(const Bitzkey& agent, const Bitzkey& opponent);
    // Whether a hunter could hunt or attack another, wherever they stand: it may go where the other is, sea or land,
    // and the other is no flyer out of its reach, avian and not resting, while it is terrestrial or aquatic.
    static bool within_reach(const Bitzkey& hunter, const Bitzkey& other);
    static bool is_prey(const Bitzkey& hunter, const Bitzkey& other);
    static bool feeds_on(const Bitzkey& eater, const Bitzkey& other);
    static double diet_share(const Bitzkey& eater, const Bitzkey& other);
    static double food_needed(const Bitzkey& agent);  // the meals a mouth it needs about it to breed; 0, none
    double food_supply(const Bitzkey& agent) const;
    double home_plants(const Bitzkey& grazer) const;  // the plants in its home band per grazer at home there, itself among them
    double home_prey(const Bitzkey& predator) const;  // the prey in its home band per predator at home there, itself among them
    // The food in its home band per mouth at home there: plants for a grazer, prey for a predator; 0 for an omnivore.
    // Whether an animal may breed with a partner, all but being close enough and finding a nursery: both ready, room
    // in the world, not crowded by its own kind, food enough about it where that limits breeding, and, for a grazer,
    // plants enough in its home band. A predator's prey at home shrinks its litters instead.
    bool may_breed(const Bitzkey& agent, const Bitzkey& partner) const;
    double home_food(const Bitzkey& agent) const;
    static double home_food_needed(const Bitzkey& agent);  // PLANTS_PER_GRAZER, PREY_PER_PREDATOR, or none
    // How well fed its home band is, as a sense: 1 at twice the food a mouth needs to breed, or more; 1 where no home
    // food limits it, and 0 for an omnivore.
    double home_food_sense(const Bitzkey& agent) const;  // the share of a full litter it would feed
    // The share of a full litter an animal's home band feeds: 1 at twice the food a mouth needs to breed, or more,
    // falling to 0 with none, and 1 where no home food limits it. A grazer's is its plants, a predator's its prey, and
    // an omnivore's both, half each: half the share its plants would give a grazer, and half the share its prey
    // (grazers and predators) would give a predator, each per omnivore at home there.
    double home_fed(const Bitzkey& agent) const;
    // The share of a full litter `food` a mouth feeds where `needed` is the least to breed: full at twice it.
    // The share of a full litter `food` a mouth feeds where `needed` is the least to breed: full at `full` times it.
    static double fed_share(double food, double needed, double full = 0.0) {
        return needed ? std::min(1.0, food / ((full ? full : FULL_LITTER_FOOD) * needed)) : 1.0;
    }
    // How many times the home food it needs to breed a full litter needs: PREDATOR_FULL_LITTER_FOOD for a predator,
    // FULL_LITTER_FOOD for any other.
    static double full_litter(const Bitzkey& agent) {
        return agent.species == Species::Predator ? PREDATOR_FULL_LITTER_FOOD : FULL_LITTER_FOOD;
    }
    // Whether its home band holds the food a mouth needs to breed: plants for a grazer, prey for a predator, both
    // together, half each, for an omnivore.
    bool well_fed(const Bitzkey& agent) const { return home_fed(agent) >= 1 / full_litter(agent); }
    // Whether a hunter's prey species, all together, have grown so few (under RARE_PREY percent of its own species' numbers) that it spares them
    // by not breeding.
    bool prey_rare(const Bitzkey& agent) const;
    // A grown animal feeds the hungriest infant of its kind within NURSING_REACH: a turn's burn of food, so the infant
    // grows no hungrier, paid from its own stomach, while it stays below NURSING_LIMIT hunger itself.
    void nurse(Bitzkey& agent);
    double plant_yield(const Bitzkey& plant) const;  // the share of its food a plant gives: less out of its temperature
    // The nearest cell straight up or down, within sight, where it would feel at home: north before south.
    std::optional<Cell> comfort_row(const Bitzkey& agent, int sight) const;
    std::pair<bool, bool> prey_switching(const Bitzkey& hunter) const;
    int other_food(const Bitzkey& omnivore) const;  // its food besides predators within FORAGE_RADIUS: grazers, plants, grassy cells
    static double flee_range(const Bitzkey& agent, int herd_near);
    bool herd_mate(const Bitzkey& agent, const Bitzkey& other) const;
    // The herd-mates within sight of an animal's success (an outcome above 0) or failure learn from it
    // as if they had made its last choice themselves, feel it, and sense it for a while; how much
    // they take it in is their empathy times their intellect.
    void witness(const Bitzkey& actor, double outcome);
    // Whether an animal counts another, alive or not, as one of its herd (or pack).
    bool society(const Bitzkey& observer, const Bitzkey& actor) const;

private:
    friend struct WorldFile;  // which saves the world whole, and loads it back
    std::vector<int> group_sizes(int population);
    // Lay out the sea over `ocean` percent of the map, from its own stream of `seed`: smooth noise, the lowest of it sea.
    void make_sea(double ocean, std::uint64_t seed);
    // BIOME_GROUPS starting groups at home in every biome.
    void spawn_biomes();
    // A square for a group's centre of this terrain, in one of `rows` if given, drawn until one suits it; none if none does.
    std::optional<Cell> group_centre(Terrain terrain, const std::vector<int>& rows);
    // `native` groups have random starting genomes: each carries a tolerance flag only with
    // STARTING_TOLERANCE, and plants take the usual temperature of their centre's zone as their home.
    // So that every zone starts with plants, the starting groups' plant groups take a zone each, top
    // to bottom, until every zone has one; if there are too few, the last animal groups become plants,
    // though one animal group is always kept. Returns the rows each such group's centre may fall in,
    // by group; the rest fall anywhere. `species` is changed in place.
    std::vector<std::vector<int>> plant_homes(std::vector<int>& species) const;
    // A group's centre falls in one of `rows` if given (and not empty).
    // Or, given a biome, centred on one of its squares, and each member that would fall outside it at the centre.
    void spawn_group(Dna dna, int size, const std::shared_ptr<const Brain>& brain = nullptr,
                     const std::optional<PartialMind>& mind = std::nullopt, bool native = false,
                     const std::vector<int>& rows = {}, const std::vector<Cell>* biome = nullptr);
    void perform(Bitzkey& agent, int action, Situation& situation);  // the action itself
    void take_census();
    // Forget the deaths before the graph's start, and the genomes of the Bitzkeys neither on the graph nor in the world.
    void forget_before(int start);
    // Forget the deaths kept whole from before the last RECENT_DEATH_TURNS turns, and now and then the killers gone.
    void forget_old_deaths();
    // Forget a death, unless another has since been recorded under its identifier.
    void forget_death(int turn, int identifier);
    // The deaths' order, oldest first, from the records alone, as a loaded world has them.
    void order_deaths();
    bool dies_of_age(Bitzkey& agent);
    bool room_for_seedling(Cell cell) const;  // the cell is free, and plants don't crowd it
    void sprout(Dna dna, Cell cell, int generation);
    void settle(Bitzkey& agent);
    // Learn from its own last choice; its `outcome` (what came of it, before the future's worth) sets how much it counts.
    void learn(Bitzkey& agent, double target, double outcome, bool replay = false);
    // Going further costs: GO_EFFORT of a turn's burn for each cell past the first, however it moves.
    static void effort(Bitzkey& agent, int cells) { agent.hunger += agent.burn * GO_EFFORT * std::max(0, cells - 1); }
    // The nearest grass in sight, but none in a row further from its home temperature than here.
    std::optional<Cell> nearest_grass(const Bitzkey& agent, int sight) const;
    Bitzkey* nearest_carrion(const Bitzkey& agent, int sight) const;
    struct Seen {
        Bitzkey* quarry = nullptr;
        Bitzkey* hunter = nullptr;
        Bitzkey* suitor = nullptr;
        Bitzkey* flora = nullptr;
    };
    // Prey as far as `prey_sight`, when it sees prey further; else as far as `sight`.
    Seen look_around(const Bitzkey& agent, int sight, int prey_sight = -1) const;
    void move(Bitzkey& agent, int x, int y);
    void move_toward(Bitzkey& agent, int x, int y, int steps = -1);  // its own steps, unless given
    void move_away(Bitzkey& agent, const Bitzkey& threat);
    bool in_bounds(int x, int y) const { return 0 <= x && x < width_ && 0 <= y && y < height_; }
    // A row kept on the map: the top and bottom edges are barriers, though east and west wrap round.
    int within(int y) const { return std::max(0, std::min(height_ - 1, y)); }
    std::vector<Bitzkey*>& cell_at(int x, int y) { return cells_[static_cast<std::size_t>(y) * width_ + x]; }
    const std::vector<Bitzkey*>* cell_if(int x, int y) const {
        return in_bounds(x, y) ? &cells_[static_cast<std::size_t>(y) * width_ + x] : nullptr;
    }

    int width_, height_;
    PyRandom random_;
    Insights insights_;
    Census census_;
    std::deque<Dna> lineage_;  // from identifier lineage_first_ on
    int lineage_first_ = 0;
    std::unordered_map<int, DeathRecord> death_records_;
    std::deque<std::pair<int, int>> death_order_;  // (turn, identifier) of each death, oldest first
    DeathTally death_tally_;
    int turn_ = 0;
    int deaths_ = 0;
    DeathCauses death_causes_;
    int next_identifier_ = 0;
    std::vector<std::unique_ptr<Bitzkey>> agents_;
    // Their identifiers as the cells were last indexed, in the same order, so one is found without reading each
    // Bitzkey on the way; those born since lie beyond them. Emptied whenever any is let go, until indexed again.
    std::vector<int> identifiers_;
    // What the dead leave to be freed, their brains and bodies, freed on a thread of its own while the next turn
    // runs: handing a crowded world's memory back takes as long as some of the thinking.
    std::vector<std::shared_ptr<Brain>> dead_brains_;
    std::vector<std::unique_ptr<Bitzkey>> dead_bodies_;
    std::future<void> freeing_;
    void free_the_dead();
    std::vector<std::unique_ptr<Bitzkey>> departed_;  // removed corpses, kept until the cells are re-indexed
    std::vector<std::vector<Bitzkey*>> cells_;
    std::vector<std::vector<Bitzkey*>> carrion_;
    std::size_t carrion_count_ = 0;
    std::vector<std::vector<Bitzkey*>> kills_;  // fresh kills, by cell
    std::size_t kill_count_ = 0;
    int population_ = 0;
    std::optional<int> population_cap_;
    double mind_mutation_, body_mutation_;
    int plants_ = 0;
    std::vector<int> grass_;
    std::vector<std::uint8_t> sea_;  // 1 for each square under the sea, row by row; empty without sea
    double ocean_ = 0.0;
    double grass_regrowth_;
    int initial_grass_ = 0;  // what grows back each turn is a share of it
    std::vector<Seed> seeds_;
    std::vector<double> climate_;      // each row's temperature this turn
    std::vector<double> growth_;       // each row's share of medium, where grass grows
    std::vector<int> band_of_row_;     // the climate band each row lies in, top to bottom
    std::vector<std::vector<int>> home_band_;  // by home temperature and row: the home band nearest
    std::vector<int> band_plants_;     // the plants in each band, and the grazers at home there,
    std::vector<int> band_grazers_;    // as the cells were last indexed
    std::vector<int> band_prey_;       // the prey in each band, and the predators at home there, likewise
    std::vector<int> band_predators_;
    std::vector<int> band_omnivores_;       // the omnivores at home in each band, and what they hunt there:
    std::vector<int> band_omnivore_prey_;   // grazers and predators
    std::array<int, 4> living_{};           // the living of each species, as the cells were last indexed
    void index_bands();
    std::vector<int> grass_rows_;      // the rows where grass grows at all
    std::vector<double> grass_reach_;  // their growth added up, row by row, for sowing
    struct Witnessed {  // what a herd-mate saw this turn, to learn from at its end
        Bitzkey* observer;
        Senses senses;
        int choice;
        double outcome, rate;
    };
    std::vector<Witnessed> witnessed_;
    // The brains of young conceived this turn, each made from its parents' and its own seed once the turn's
    // matings are done, on every core at once, into the brain its womb already holds.
    struct Conception {
        std::shared_ptr<Brain> into, first, second;
        std::uint64_t seed;
        int depth, width;
        bool born_hunter;  // a predator of other parents, bringing eating to hunting and uprooting to feasting
    };
    std::vector<Conception> conceptions_;
    // A tile of the map, its animals acting on a core of its own (see act_in_tiles): its own stream of chance, and what
    // they do that reaches beyond it, kept to be done once every tile has acted.
    struct ActingTile {
        PyRandom rng;
        std::vector<std::size_t> members;  // their places in the turn's order, in that order
        struct DeathNote {
            Bitzkey* agent;
            std::string cause;
            int killer;
            Species killer_species;
            std::string activity;
        };
        std::vector<DeathNote> deaths;
        std::vector<std::pair<const Bitzkey*, double>> witnessed;  // (actor, outcome) for its herd to see
        std::vector<std::pair<Bitzkey*, const Bitzkey*>> alerts;   // (alerting, what about)
        std::vector<std::pair<Bitzkey*, std::vector<Bitzkey*>>> feeding;  // (parent, its young) fed
        std::vector<Conception> conceptions;
        std::vector<std::shared_ptr<Brain>> dead_brains;
        int kills = 0, kills_gone = 0, carrion_gone = 0;
        std::array<double, ACTION_COUNT> took{};
        std::array<long long, ACTION_COUNT> taken{};
        double acting = 0;
    };
    static thread_local ActingTile* acting_;  // the tile this core is acting for, if any
    // The stream of chance: the acting tile's, else the world's own.
    PyRandom& rng() { return acting_ ? acting_->rng : random_; }
    // The turn's choices acted out tile by tile, on every core: returns false, having done nothing, when the world is
    // too small to part, or thinks on the GPUs, to be acted out one by one instead.
    bool act_in_tiles(const std::vector<Bitzkey*>& agents, const std::vector<int>& choices, std::vector<Sensed>& plans);
    // A death written down: its record and tally, the counts, and the parents' grief.
    void record_death(Bitzkey& agent, std::string_view cause, int killer, Species killer_species, std::string_view activity);
    std::unique_ptr<gpu::BrainEngine> gpu_;  // the GPUs every brain lives on, when the world thinks there
    std::string gpu_status_;
    std::deque<Senses> lesson_senses_;  // what each queued lesson sensed, kept still while it waits
    std::vector<gpu::BrainEngine::Lesson> lessons_;  // lessons for the GPUs, learnt in one batch
    void queue_lesson(const Bitzkey& agent, const Senses& senses, int action, double target, double rate, bool traced,
                      double decay);
    void learn_queued();
    void sync(std::span<Bitzkey* const> agents);  // these brains back from the GPUs, each once a turn
    // Put a brain on the GPUs, unless they have failed.
    void attach(Bitzkey& agent);
    // A brain of its own to learn on: its own if nothing else holds it (a group deployed together shares one), else a copy.
    Brain& learning_brain(Bitzkey& agent);
};

}  // namespace bitzkey
