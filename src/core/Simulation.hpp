#pragma once
// Turn-based Bitzkey world and its deterministic history.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
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
#include <set>
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
constexpr int DEFAULT_WIDTH = 200;
constexpr int DEFAULT_HEIGHT = 100;
constexpr int INITIAL_POPULATION = 300;
constexpr double GRASS_DENSITY = 50;      // percent of cells that start with grass
inline double GRASS_REGROWTH = 0.032;  // share of the starting grass that grows back each turn
constexpr int MAX_POPULATION = 20000;     // the most Bitzkeys spawned at the start
inline int CROWDING_LIMIT = 10;  // 0: no limit
constexpr std::array<int, 2> SPAWN_GROUP = {3, 5};
inline int GRAZER_SPAWN_GROUP = 30;
// A new world's grazers start at least this many squares (across and down, east and west wrapping) from its hunters'
// groups, and its hunters from its grazers', where there is room: the farthest of APART_TRIES places, else; 0: anywhere.
inline int HUNTERS_APART = 15;
inline int PREGROWTH_TURNS = 2000;  // a new world's plants grow alone this many turns, uncounted, before its animals spawn
constexpr int APART_TRIES = 20;  // a new world's grazers spawn in groups this large, so the hunters' prey outnumber them
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
// Stamina, 0 to 100: every action but rest tires an animal, and resting restores it. Its pace follows the day: an
// animal is active ACTIVE_SHARE of each day and sleeps the rest, spending its stamina in the one and getting it all back
// in the other, however many turns a day is (see stamina_pace and rest_recovery). Before
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
inline double GRAZER_INFANCY = 0.5;    // and a grazer this share as long
inline double OMNIVORE_INFANCY = 0.5;  // and an omnivore this share as long
inline double GRAZER_GESTATION = 0.5;  // a grazer carries its young this share as long as its size would have it
inline double FEEBLE_SHARE = 0.5;
constexpr int DEPLOY_GROUP = 5;  // Bitzkeys deployed from the catalog onto a running world, of each chosen, a click
inline int SPAWN_AT_HOME = 1;  // 1: a group appears in rows whose usual temperature is its home (the nearest, if none); 0: anywhere
// Biomes: each stretch of land or sea within one climate zone, joined square to square (east and west wrapping round),
// of BIOME_SIZE squares or more. A world started with any Bitzkeys has, on top of its population, BIOME_GROUPS groups of
// each species in every biome, each native there: the zone's usual temperature its home, and the surface's own
// terrain (terrestrial on land, aquatic at sea).
inline int BIOME_GROUPS = 1;
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
inline constexpr int FIRE_STONES = 2;  // carried stones that strike a fire, and are kept; else a club and a wood rubbed together, the wood spent
inline int WATERED_REACH = 7;  // steps from water within which hot land grows grass and can be tilled
// An animal awake out of its own hours, in the dark if active by day or in the light if active by night (at its dusk and
// dawn), sees OFF_HOURS_SIGHT as far as in its own, at least a step. Calls carry as far either way.
inline double OFF_HOURS_SIGHT = 0.5;
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
inline double CURIOUS_EXPLORATION = 0.1;  // the chance, times its curiosity, that an animal tries any of its options at
                                           // random rather than as its brain values them, so no option goes untried for good
// Boredom, 0..1: rising as an animal keeps to what it did lately, easing as it does what it hasn't for BOREDOM_HORIZON turns.
// It is felt, unpleasant, and may turn any choice into another, the longest undone the likeliest.
constexpr int BOREDOM_HORIZON = 200;
inline double BOREDOM_RISE = 0.02, BOREDOM_RELIEF = 0.25, BOREDOM_DRIVE = 0.2, BOREDOM_UNEASE = 0.2;
inline double UNTRIED_HOPE = 1.0;  // see hope_for_untried
// Foresight: a brain learns what its next turn will bring at this share of its learning rate, one foreseen thing a turn
// in rotation. How far what came differs from what it foresaw is its surprise, felt as a small pleasure.
inline double FORESIGHT_LEARNING = 0.5, SURPRISE_JOY = 0.2;
// Imagining: an animal awake and in no peril asks its brain what would come of its IMAGINED_OPTIONS best-valued options,
// and those it foresees leaving it better off gain on the others by this much for each whole unit.
inline double IMAGINATION = 0.5;
constexpr int IMAGINED_OPTIONS = 3;
constexpr double FORESIGHT_SCALE = 10;  // hunger, health and stamina change by hundredths a turn: foreseen at this many times that
inline double RESTLESS_CURIOSITY = 0.5;  // the chance, times a half and its curiosity, that an animal whose brain chose to rest
                                         // (not made to) does any of its other options instead; in peril, neither, but as its brain chooses
// Moving costs food. A walk, a single cell, saves this share of the turn's burn, as resting does; going as
// far as its speed allows costs this share of a turn's burn more for each cell beyond the first.
inline double WALK_REST = 0.5;
inline double GO_EFFORT = 0.25;
inline double FEAR_DREAD = 0.3;
inline double UNEASE = 0.1;
inline double CLIMATE_UNEASE = 2.0;  // discomfort two or more steps from home, half that one step; less for the hardy
inline double RESTING_UNEASE = 1.0;  // unease, a turn, of having chosen to rest out of its temperature, two or more steps from home (half one step; less if hardy)
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
// Lessons of restraint, felt, not laws. A hunter that kills where few others of its prey are left in its sight is
// uneasy (HUNTER_RESTRAINT, in full with none left, none with PREY_PLENTY or more). One that eats grass or a plant and
// leaves none of either in its sight is uneasy (LAST_BITE). One whose sown seed sprouts within its sight is proud
// (SOWER_PRIDE). A herd-mate's starving teaches those who see it FAMINE_LESSON of what a sudden death does.
inline constexpr int PREY_PLENTY = 4;
inline double HUNTER_RESTRAINT = 1.0, LAST_BITE = 0.5, SOWER_PRIDE = 1.0, FAMINE_LESSON = 1.0;
// A sense of plenty: the food within FORAGE_RADIUS (prey for a hunter; plants and grassy squares for a grazer, and
// both for an omnivore) against its memory of it there, which follows the food over about a day: 0.5 as remembered,
// toward 1 for more and 0 for less.
// Society: an animal sees what happens to its herd-mates (its pack, for a predator) within its sight. A
// herd-mate's kill is a success, its death (but of old age) a failure; the witness learns from it as
// if it had made the herd-mate's last choice itself, and feels it, by its empathy times its intellect.
inline double SOCIAL_LEARNING = 1.0;  // how fast it learns from what it sees, beside learning from its own choices
inline double FATAL_LESSON = 5.0;  // how many times as hard a herd-mate's death teaches those who see it
inline double SOCIAL_MOOD = 0.3;      // how much what it sees moves its mood
inline double SOCIAL_MEMORY = 0.93;   // the share of a seen success or failure it still senses a turn later (0.8 every three hours)
inline double OTHER_KIND = 0.4;
// The social background's traits. Loyalty: whenever it can herd, an animal values keeping to its herd this much
// more, times its loyalty. Dominance: a ready animal breeds with the most dominant willing partner in reach, and
// a territory's crowding troubles the dominant less and the submissive more, by up to this share.
inline double LOYALTY_PULL = 0.25;
// Loyalty to a leader: a follower turns from the leader it holds its standout to one held in more esteem only by
// chance. Not at all unless the new one is esteemed more by its loyalty times LOYALTY_MARGIN; past that, with the
// share of the way it is from there to the most there can be, raised to its loyalty. A loyal one needs a far better
// one even to waver; one without loyalty turns to any better on a whim.
inline double LOYALTY_MARGIN = 0.5;
// Convictions: a trait at CONVICTION_LEVEL of its full range or more is one it won't break, even starving. Empathy: a
// grazer won't uproot a plant, and a predator won't hunt, strike or feast on an infant or one carrying young.
// Tolerance: it won't strike a foe nor drive one from its land, only defend itself and its own; it still hunts to eat.
inline double CONVICTION_LEVEL = 1.0;
// Scruples, short of conviction: the kinder value uprooting a plant, or felling one on worked land (a wild one is fair
// game), EMPATHY_SCRUPLE less, times their empathy, and a
// predator likewise hunting, striking or feasting on an infant or one carrying young; the more tolerant value striking
// a foe or driving one out TOLERANCE_SCRUPLE less, times their tolerance, though not hunting to eat.
inline double EMPATHY_SCRUPLE = 1.0, TOLERANCE_SCRUPLE = 1.0;
// Sociability: an animal values making an offer this much more, times its sociability.
inline double SOCIABLE_TRADE = 0.25;
// Its generosity: the strongest of its patience, sociability and empathy, less the strongest of its dominance,
// aggression and appetite, from -1 to 1. Food brought to its herd's store gladdens it by SHARE_JOY times that, and
// eating fruit off a plant, a kill where it lies or what it carries, with a herd to share with, by SELFISH_JOY times
// less: the generous glad to share and uneasy at keeping, the greedy the other way about.
inline double SHARE_JOY = 0.5, SELFISH_JOY = 0.1;
// And with a friend in sight there, KIN_SHARE_JOY times its kinship besides, however greedy it is.
inline double KIN_SHARE_JOY = 1.0;
inline bool swayed(double loyalty, double margin, double chance) {
    const double need = LOYALTY_MARGIN * loyalty;
    if (margin <= need || need >= 1.0) return false;
    return chance < std::pow((margin - need) / (1.0 - need), loyalty);
}
// A chance of its own this turn, 0 to 1, the same on whichever core reads its senses.
inline double whim(int identifier, int turn) {
    std::uint64_t z = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(identifier)) << 32 | static_cast<std::uint32_t>(turn)) + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return static_cast<double>((z ^ (z >> 31)) >> 11) * 0x1.0p-53;
}
inline double PARENTAL_INSTINCT = 2.0;  // how much of the gap between feeding its young and its best other option a parent
                                        // closes, times the hungriest's hunger and a half and its empathy: at 2, half hungry, level
inline double INFANT_HERDING = 1.0;  // how much more an infant values keeping to its kind, and it herds once a cell from their middle
inline int NURSING_REACH = 2;        // each turn, a grown animal feeds the hungriest infant of its kind this close, a turn's burn of food,
inline int NURSING_LIMIT = 80;       // while it stays below this hunger itself
inline double DOMINANCE_EASE = 0.5;
// Trust: how much more, or less, the most and least trusting take in what they see of their herd-mates, by
// this share; and an alert reaches it from a herd-mate this many times its trust times its sight away.
inline double TRUST_HEED = 0.5;
inline double TRUST_REACH = 2.0;
inline int STUDENT_INTELLECT = 4;  // the least inborn intellect an animal needs to be taught
inline double STUDENT_AGE = 0.5;  // the oldest a student may be, as a share of its elder's age
inline double STUDENT_TRUST = 0.25;  // how trusting an animal must be to be willing to be taught
inline double TEACHING_JOY = 1.0;  // how glad an elder is of teaching a student; more if empathetic
// Tutoring: anyone may give one of its kind beside it, not a foe and not tutored within a day, one lesson in a craft it
// has mastered or a skill it has worth teaching, for the hunger of a call: a recipe the pupil lacks (now known), else
// archery if it shoots the better, else a recipe the pupil has not mastered. A lesson is worth TUTOR_PRACTICE of real
// practice: that chance of a try nearer mastery, that share of a shot's lesson. The tutor is glad of it (TUTOR_JOY,
// more if empathetic) and the pupil thinks the better of it, by half a teaching.
inline double TUTOR_JOY = 0.5, TUTOR_PRACTICE = 0.5;
inline constexpr double ARCHERY_WORTH_TEACHING = 0.05;  // how much the better a shot a tutor must be to teach archery
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
inline double LEARNING_FLOOR = 0.25;  // the share of LEARNING_RATE even the dullest learns at, intellect adding to it
inline int REPLAY_PER_INTELLECT = 2;
// What an animal learns toward is held between -VALUE_LIMIT and VALUE_LIMIT, so that a brain's overestimates, fed back
// into what it learns, can't run away with its values; 0: no limit.
inline double VALUE_LIMIT = 50.0;
inline double limited_value(double target) { return VALUE_LIMIT > 0 ? std::clamp(target, -VALUE_LIMIT, VALUE_LIMIT) : target; }
inline int MATE_REACH = 2;            // a partner within this many steps is close enough to breed with
inline double MATING_CALL = 0.5;      // how much more a predator ready to breed values breeding, or else courting a mate it sees
inline double MATING_CALL_RANGE = 2.0;    // a predator ready to breed that sees no mate seeks one this many times its sight off
constexpr double MAX_MATING_CALL_RANGE = 3.0;
inline double OMNIVORE_MATING_CALL = 0.25;  // an omnivore's milder mating call
inline double OMNIVORE_MATING_CALL_RANGE = 2.0;  // and how far, times its sight, one ready to breed that sees no mate seeks one
inline double FIRST_BREEDING_INSTINCT = 3.0;  // how much more one of a world's first, ready to breed, values breeding, or else courting
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
// Calls (Brain.hpp): one carries this many steps times a half and its caller's size (a share of its range), at least
// one, and costs its caller this much hunger, as any call does, the alert among them.
inline int CALL_REACH = 16;
inline double CALL_COST = 0.05;  // for each sound of a call: two sounds cost twice as much as one
// The most sounds a call may run to, 1 to 3: past the first, each place may stop or add one of the five sounds.
inline int CALL_LENGTH = 3;
// What a caller feels of what its call did to those of its kind who heard it: whether their next turn went better or worse
// than each expected (its reward against what its brain valued its choice at), each weighed by the caller's care for it
// (its kin wholly, a friend by its regard, any other a little), the mean of them, times its empathy and this, is added to
// what the call itself teaches it, a turn late, as the listeners' turn comes after. Its action's lesson is untouched. 0 for none.
inline double CALL_EMPATHY = 1.0;
// A call that helps pays its caller. Each animal remembers the loudest caller of its own kind it heard; if, by the
// next turn, it got away (a hunter was in sight and it was not struck) or, hungry (half or more), it ate, the caller
// takes joy in it (CALL_PAYOFF, times a half and its empathy or sociability, the greater; for three helped a turn at
// most), and the one helped thinks the better of it (HEEDED_REGARD of a deed's weight). Nothing says what a call means.
inline double CALL_PAYOFF = 0.0, HEEDED_REGARD = 0.0;  // none by default: what a call earns is left to what it does
inline double COMMUNICATION_JOY = 0.5;  // a call answered within a day by a deed good for both: each takes this share of its own gain
constexpr int CALL_PAYOFFS_A_TURN = 3;
// Memory (Brain.hpp): how many choices back a lesson's error is carried to the memory gates that wrote what the brain
// read, 0 for none (what it keeps is then only what it was born to), up to MAX_MEMORY_STEPS; and how fast they learn,
// times the brain's learning rate.
inline int MEMORY_STEPS = 1;
inline double MEMORY_LEARNING = 1.0;
// The mental map: an animal remembers up to Map places of the MAP_PATCH-square patches it has been in (none at 0), the
// one it was last in longest ago forgotten for a new one; each visit moves what it remembers of a place in that season
// toward what it finds there by Map learning (the first sets it). Among places as good, it heeds the nearer, by
// MAP_NEARNESS a patch away.
constexpr int MAP_PATCH = 8;
constexpr int MAX_MAP_PLACES = 32;
inline int MAP_PLACES = 16;
inline double MAP_LEARNING = 0.2;
constexpr double MAP_NEARNESS = 0.01;
// Those it knows: an animal remembers up to Acquaintances others (none at 0) by what they did to it, the one it dealt
// with least (the regard nearest nothing, then the one longest ago) forgotten for a new one; each deed moves its regard for the doer toward the deed's worth by Regard
// learning (the first sets it): a blow -1, being fed or nursed as an infant 1, an alert it heeded or a mating a half.
// A deed seen done to another, within both's sight, counts Witness share as much: a blow only if the victim is of the
// witness's species, and feeding, teaching, a craft or a lit fire whoever it was for.
constexpr int MAX_ACQUAINTANCES = 16;
inline int ACQUAINTANCES = 8;
inline double REGARD_LEARNING = 0.3;
// A child is held, by one that has not yet judged it by its own deeds, at INHERITED_REGARD of how that one holds its
// parents (those of the two it knows, averaged): their regard, and their reputation for danger and skill.
inline double INHERITED_REGARD = 0.5;
inline double EAVESDROP_SHARE = 0.1;  // see deed_seen: a friend's treatment of another, seen, colours the watcher's view of that other
inline double WITNESS_SHARE = 0.25;
// A deed of skill seen counts for more toward how skilled the doer is held than toward liking it: SKILL_WITNESS of
// its weight on skill (and on danger, which it lowers), however little Witness share gives regard.
inline double SKILL_WITNESS = 1.0;
// Reputation: beside regard, how dangerous and how skilled it holds another, 0 to 1, at Regard learning. A blow raises
// danger, whoever it was struck at; any other deed lowers it. Crafting, teaching and lighting a fire raise skill.
enum DeedKind { PlainDeed, HarmDeed, SkillDeed, GarbDeed, LieDeed, VictoryDeed, CowardDeed, TheftDeed, CultureDeed, MarkDeed, TitleDeed, TrespassDeed };  // CultureDeed: seen for its culture alone
// Cultures: what each species esteems, seen done by one of its own kind, over and above the deed's own worth, by
// CULTURE_REGARD, twice it from family (parent, child or sibling) and, for a predator, from its pack. Predators esteem
// victories; omnivores, crafting and building (shelters and roads); grazers, sowing and fruit picked from a plant
// without uprooting it.
enum Culture { NoCulture, VictoryCulture, CraftCulture, SowingCulture };
inline double CULTURE_REGARD = 0.3;  // garb worn in sight, and fleeing in it
// Factions: up to Factions body colours (none at 0), each remembered by the first of it met. Every deed noted of an
// animal also moves the regard for its colour, by Faction share as much, and one it doesn't know is taken at its
// colour's worth, anything within Faction width on the body ring. The weakest is forgotten for a new one.
constexpr int MAX_FACTIONS = 16;
inline int FACTIONS = 8;
inline double FACTION_SHARE = 0.25, FACTION_WIDTH = 0.05;
// Regard of Regard strong or more either way makes a friend or an enemy. The nearest it knows, so sensed, eases or
// troubles it (Regard comfort); harming one gladdens or pains it (Grudge joy); helping a friend pleases it more (Friend
// joy); and it heeds a friend's alert from further, an enemy's not at all. Each is half plus the trait, so none is lost
// to a trait bred low.
constexpr double REGARD_STRONG = 0.5;
inline double REGARD_COMFORT = 0.5, GRUDGE_JOY = 1.0, FRIEND_JOY = 1.0;
constexpr double BLOW_DEED = -1.0, FEEDING_DEED = 1.0, ALERT_DEED = 0.5, MATING_DEED = 0.5, TEACHING_DEED = 1.0, CRAFT_DEED = 0.5, FIRE_DEED = 0.5;
// Shared moments: a fire enjoyed together, and a herd-mate defended. Each moves regard as a deed of these worths, at
// SHARED_BOND of a deed's weight between those who share it; the one defended takes its defender's deed at full weight.
constexpr double HEARTH_DEED = 1.0, DEFENCE_DEED = 1.0;
constexpr int HEARTH_REACH = 2;    // steps from a fire to share it
constexpr int HEARTH_COMPANY = 6;  // the most at one fire who bond, the first in the turn's order
inline double SHARED_BOND = 0.2;
// The diary: an animal keeps up to Diary entries (none at 0) of its most vivid experiences, by how much came of them,
// the vividness fading by Diary fading each turn since, the faintest forgotten for a more vivid one; and each turn it
// re-learns from one of them, in turn, as from a replay.
inline int DIARY_ENTRIES = 8;
inline double DIARY_FADING = 0.999;
constexpr int MAX_CALL_REACH = 45;  // the farthest a call may carry: the largest caller's at the most Call reach
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
inline double INFANT_THORNS = 0.25;  // an infant pricked by a plant's thorns takes this share of what a grown animal would
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
inline double PACK_BONUS = 0.5;  // for each herd-mate alongside the prey, up to PACK_MATES
constexpr int PACK_MATES = 3;
// Pack tactics: an animal senses how many of its pack are already beside the one it would strike, prey it would strike
// or is after, else its opponent in a fight, else a rival it would take from (prey flanked), and any blow struck
// alongside one is a pleasure (JOINED_HUNT_JOY, times a half and its herd instinct), as it is harder (PACK_BONUS).
inline double JOINED_HUNT_JOY = 0.5;
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
// A grazer may attack any animal within reach it holds a foe, at GRAZER_FORCE of the base force of a blow (before
// weapons, gear and other bonuses), and gains no meal by it.
inline double GRAZER_FORCE = 0.5;
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
// An animal may carry food off rather than eat it, up to its size + 1 portions, and drop it in a pile any may take
// from or eat from. Eaten from a pile, a portion sates this much hunger, by the eater's gut: a middle-sized feast of
// meat (times its digestion) and a bite of a plant's fruit (times its share of the food in plants).
inline double MEAT_PORTION = 855;
inline double FRUIT_PORTION = 70;
enum PileKind : int { MeatPile, FruitPile, StonePile, BonePile, WoodPile, BoneAxePile, ClubPile, StoneAxePile, BoneArmourPile, WoodArmourPile, CookedMeatPile, CookedFruitPile, BowPile, PILE_KINDS };
inline int FIRE_TURNS = 24;  // a day, and another for each wood fed to it; a club lights one, a fire cooks what is carried, and cooked food never spoils
inline double FIRE_PANIC = 0.25;  // the chance an animal panics, seeing a fire not its own or its herd's
inline double FIRE_COMFORT = 0.3;  // the comfort of seeing a fire that is its own or its herd's, as an unfamiliar one frightens
inline double COOKED_FOOD = 2.0;  // times as nourishing as raw
inline double CARRIED_SPOIL = 0.02;  // the chance each turn a raw portion carried goes off; cooked food keeps
// The taste for cooked food: each cooked portion eaten adds a third to its taste for it (cooked_known, 0 to 1), and is a
// delight (COOKED_JOY, times a half and the taste). Once it knows that taste, a raw portion, from a pile or carried, is
// a letdown (RAW_DISTASTE times the taste), and cooking a pleasure (COOK_JOY, times a half and the taste). Cooked food,
// and a fire cooked on within the day, are smelt SMELL_REACH times as far as it sees; cooked food smelt is food sought,
// and in sight it is taken before raw.
inline constexpr double COOKED_TASTE = 1.0 / 3;
inline double COOKED_JOY = 1.0, RAW_DISTASTE = 0.5, COOK_JOY = 1.0, SMELL_REACH = 3.0;
// A sense of taste, inborn: a cooked portion is tasty (TASTE_JOY, times its appetite), a raw portion, from a pile or
// carried, as much the opposite, over and above the taste it learns.
inline double TASTE_JOY = 1.0;
// Gluttony, 0..1: a half, plus half its appetite, less half its kinship. A meal eaten while less than GLUTTONY_HUNGER
// hungry is an unease of GLUTTONY_UNEASE times what it lacks of gluttony, fading from the whole of it when it is not
// hungry at all to none at GLUTTONY_HUNGER: none to the wholly gluttonous.
inline double GLUTTONY_UNEASE = 1.0;
inline constexpr double GLUTTONY_HUNGER = 50.0;
inline int SITE_TURNS = 100;  // an unfinished shelter left this long falls in, leaving its stone and wood in piles
inline int SHELTER_UNITS = 7;  // of stone or wood, any mix, to finish a shelter; until then each takes a space of the square
inline constexpr int FIRE_REACH = 2;  // squares off, a fire shelters an animal from the cold
// A craft must first be discovered, the more likely each turn the cleverer the animal holding its materials, and then
// made to work: the chance rises with each try to certainty at CRAFT_TRIES, the sooner the cleverer. A failure spends
// nothing, and is a small thrill of discovery to the curious.
inline double DISCOVERY_CHANCE = 0.0005;  // rare: a find is news, and a taught recipe spreads it
inline double DISCOVERY_JOY = 5.0;  // the joy of first discovering a craft (half plus curiosity), so its finder comes to prize the materials
inline int CRAFT_TRIES = 10;
inline double CRAFT_THRILL = 1.0;
inline double FIRE_JOY = 2.0;  // the sense of accomplishment in lighting a fire
inline double FIRE_FEED_JOY = 1.0;  // and in feeding one, so it burns on
inline double CRAFT_JOY = 1.0;  // a craft that works: a weapon's in full, armour's in half (half plus curiosity)
inline double TOOL_JOY = 2.0;  // the harm a weapon added to a blow, as a share of its victim's health
inline double ARMOUR_JOY = 2.0;  // the harm its armour took off a blow, as a share of its health
inline double GEAR_EASE = 0.15;  // the ease, each turn, of a weapon or bow in hand, and as much again of armour worn
inline double GEAR_COURAGE = 0.25;  // the share of fear's effects a weapon or bow in hand takes off, and as much again armour worn
inline double DON_JOY = 0.25;  // gaining a weapon or bow is TOOL_JOY times this a joy, and armour ARMOUR_JOY times this; dropping one as much a loss
inline double GEAR_DESIRE = 0.05;  // desire for a kind of gear stirred by seeing it pay off, up to 1; envy counts that gear 1 + desire times
inline double DESIRE_FADE = 0.999;  // the share of a desire for gear kept each turn, so it fades without fresh sights
enum GearDesire { WeaponDesire, ArmourDesire, BowDesire };
// A wagon: made of WAGON_STONE stones and WAGON_WOOD wood with an axe, kept; hitched, it follows its hitcher, holds
// SHELTER_UNITS more of its load, shelters it as a shelter does, and makes each step WAGON_DRAG more tiring.
constexpr int WAGON_STONE = 2, WAGON_WOOD = 3;
inline double WAGON_DRAG = 0.5;
// Crafting at hand: a craft takes its materials from the pack first, then the work begun on its square, then piles on
// its square and those beside it, then the packs of friends within a step, each of whom shares in the making (a try
// at the craft, if it knows it, and the sharing's joy or unease, SHARE_JOY times its generosity by its share). One who
// knows a bow or a wagon but lacks a part or the tool puts what it carries of one into work on its square (BUILD_THRILL
// by its share of the parts, times curiosity), for any who knows the craft to finish; work left SITE_TURNS untouched
// falls apart, its stone, bone and wood in piles there, its hide lost.
struct Parts {
    int stone = 0, bone = 0, wood = 0, hide = 0;
};
struct Work {
    int craft = -1, stone = 0, bone = 0, wood = 0, hide = 0, turn = 0;  // craft -1: none begun
};
inline double READY_JOY = 0.3;  // for each part of a set it holds for what it knows how to make (half plus mostly patience)
inline double BUILD_THRILL = 1.0;  // each stage of a shelter, k of SHELTER_UNITS, thrills BUILD_THRILL * k / SHELTER_UNITS; finishing it, twice BUILD_THRILL (times curiosity, plus SHELTER_LONGING times its shelter memory)
// A finished shelter keeps the weather off whoever stands in it, hot or cold. Resting inside is a comfort
// (SHELTER_COMFORT, half standing in one), and each turn of it is remembered, to a full memory after a day, so that
// one who has known a shelter longs to build one (SHELTER_LONGING). Whatever keeps the weather off, a shelter, a
// fire, or an infant's parent close by, is felt as relief, by as much unease as it spares (PROTECTION_JOY).
inline double SHELTER_COMFORT = 0.3;
inline double SHELTER_LONGING = 1.0;
// Roads: one with a stone axe lays a section of road on its square in ROAD_TURNS turns of hard work (ROAD_FATIGUE
// each, more than a blow), clearing it of grass, plants and worked land. Each turn of it thrills as building does
// (ROAD_THRILL, growing to twice it for the finished section), and a section that joins two shelters by road, more
// (ROAD_LINK_JOY). Going from a road square costs ROAD_EASE of a step's stamina; each such step is remembered, to a
// full memory after a day of them, and is a pleasure that grows with it (ROAD_JOY), as does the longing to lay more
// (ROAD_LONGING).
inline constexpr int ROAD_TURNS = 3;
inline constexpr int ROAD_STONES = 2;  // carried stones a section needs to be worked on, laid into it when it is finished
inline double ROAD_FATIGUE = 60;
inline double ROAD_THRILL = 1.0;
inline double ROAD_LINK_JOY = 3.0;
inline double ROAD_EASE = 0.5;
inline double ROAD_JOY = 0.3;
inline double ROAD_LONGING = 1.0;
inline double PROTECTION_JOY = 1.0;
// Leadership. One it holds skilled close by is a comfort (SKILL_COMFORT, more if curious), and a craft seen done by one
// it holds skilled may be learnt from the watching (WATCH_LEARNING). A student within ELDER_REACH steps of the elder
// who taught it is glad of it (ELDER_COMFORT, more if trusting). With danger in sight, a friend or one it holds skilled
// within FOLLOW_REACH steps spares it FOLLOW_SAFETY of its dread, felt as relief as any protection is.
constexpr double SKILL_HELD = 0.5;  // the skill at which another is held skilled
inline double SKILL_COMFORT = 0.5;
inline double WATCH_LEARNING = 0.25;
// A child that hears its parent call learns, at this share of its own learning, to make that call where the parent
// made it, and, hearing it, what the parent then did; 0, not at all.
inline double CHILD_CALL_LEARNING = 1.0;
// Body language: one that sees the caller of its kind it hears may read what it meant, a chance of BODY_LANGUAGE for
// each point of its intellect (none at 0): it learns, as a child its parent's call, to make that call where the caller
// made it and, hearing it, what the caller then did. Unseen, or not read, a call is only what its own turns teach it.
inline double BODY_LANGUAGE = 0.1;
inline double ELDER_COMFORT = 0.5;
inline int ELDER_REACH = 3;
inline double FOLLOW_SAFETY = 0.5;
inline int FOLLOW_REACH = 2;
inline double DIG_CHANCE = 0.1;  // that a dig finds a stone
// What an animal may wield and wear: a tool of bone or wood breaks after GEAR_USES, a stone axe never; a stone carried
// and nothing else wielded strikes STONE_FORCE times as hard, an axe or club WEAPON_FORCE. A bone axe or club can't go
// with armour. Armour counts as at least ARMOUR_LEAST of the trait's points, doubled.
enum Weapon : int { NoWeapon, BoneAxe, Club, StoneAxe };
enum Armouring : int { NoArmour, BoneArmour, WoodArmour };
inline int GEAR_USES = 7;
inline double WEAPON_FORCE = 2.0, STONE_FORCE = 1.5, ARMOUR_FORCE = 2.0;
// Archery. A bow is made of BOW_WOOD carried wood and the hide it wears, both spent, with a stone axe in hand, kept;
// it is carried apart from what it wields, and shoots BOW_USES times (arrows assumed) before it is spent. It shoots
// as far as the archer sees: at the prey it would hunt or, else, the nearest it holds a foe, striking as hard as a
// carried stone; or at a plant, for practice, harming nothing. The chance of a hit rises with steadiness (patience,
// intellect, fearlessness) and with skill, and falls with distance; every shot adds ARCHERY_LEARNING of what skill is
// left to learn. A hit, in practice too, is a joy (ARCHERY_JOY, the more for the skilled), and all who see it hold the
// archer the more skilled, and think the better of it, by ARCHERY_REGARD of a deed.
inline constexpr int BOW_WOOD = 2;
inline int BOW_USES = 20;
// Arrows: one who knows the bow makes a bow's worth of arrows from a wood at hand (see at_hand), for its own bow not yet
// full, else for the bow of one beside it it holds no foe: the bow shoots BOW_USES times again; a bow whose arrows
// are spent is kept, empty, for more. It is a craft (CRAFT_JOY)
// and a step toward mastering the bow; one given arrows thinks the better of their maker, as of a gift.
inline double ARCHERY_JOY = 1.0, ARCHERY_REGARD = 0.1, ARCHERY_LEARNING = 0.05;
inline constexpr double ARCHERY_LEAST = 0.2;  // the chance of a hit at the archer's feet, unsteady and unskilled
constexpr int CRAFT_COUNT = 7;  // the crafts, in the order of their actions from MakeBoneAxe
inline int ARMOUR_LEAST = 2;  // stone never spoils
// Agriculture: raw fruit eaten leaves a seed, which feeds SEED_FOOD of a fruit's portion or may be planted in worked
// land. Digging with any tool works the land, only in the medium zone; it stays worked.
inline double SEED_FOOD = 0.1, PLANTING_JOY = 1.0;
// Harvest: a fruit picked from a plant without uprooting it is a small gladness (HARVEST_JOY), and its picker, seeing
// the plant bear fruit again, is glad of it (REGROWTH_JOY for each fruit it bears), once for the last picking of it.
inline double HARVEST_JOY = 0.1, REGROWTH_JOY = 0.5;
// Farming: a plant on worked land bears ARABLE_FRUIT times as much fruit each morning, and, the land fed bone,
// FERTILIZED_FRUIT times that again, the bone spent on it.
inline int ARABLE_FRUIT = 2, FERTILIZED_FRUIT = 2;
// Worked land no one tends (digs, sows, feeds bone or picks fruit on) for FALLOW_YEARS goes wild, its bone with it; 0, never.
inline double FALLOW_YEARS = 1.0;
// Bone crushed with a stone carried, a club or a stone axe, a bone it carries or one from a pile on its square: a
// scavenger eats its marrow, a portion of meat as from a pile; or, on worked land, it is fed to the soil, which then
// sprouts the next seed there in half PLANT_GESTATION, a pleasure as planting is (FERTILIZING_JOY, times curiosity).
// Each marrow meal edges a grazer MARROW_LEAN toward meat; at the full of it, it gives in: an omnivore, in its genes
// too, so its young are born omnivores.
inline double FERTILIZING_JOY = 1.0, MARROW_LEAN = 0.05;
// Clothing: with a bone or stone axe, one skins a body not yet skinned, getting the better of its own hide and any it wore, if
// better than what it wears now; a pleasure (SKINNING_JOY, times curiosity, the more for the finer hide). A hide keeps
// out HIDE_WARMTH steps of cold and adds HIDE_ARMOUR_SHARE of the armour of the body it came from to the wearer's own.
// An omnivore's hide is a warrior's garb (WARRIOR_GARB), a predator's a mark of prestige (PRESTIGE_GARB): each turn,
// all who see it move their regard toward that, at GARB_REGARD of a deed's weight. A wearer seen fleeing a fight is
// a liar to them: the regard its garb won is lost, and LIE_DISREGARD times as much again. The moodiest or most
// dominant of its kind (above ENVY_LEVEL of them, ranked each turn) envy better possessions they see, hide, tools and
// armour: uneasy (ENVY_UNEASE) by how much better, glad (ENVY_JOY) of anything better they gain, and drawn to the
// best-provided mate.
inline double SKINNING_JOY = 1.0, HIDE_WARMTH = 1.0, HIDE_ARMOUR_SHARE = 0.5;
inline double WARRIOR_GARB = 0.5, PRESTIGE_GARB = 1.0, GARB_REGARD = 0.05, LIE_DISREGARD = 1.0;
inline double ENVY_LEVEL = 0.75, ENVY_UNEASE = 0.5, ENVY_JOY = 1.0;
// Foes, victory, cowardice, gifts and theft. A foe is one regarded at or below minus FOE_LEVEL times (0.5 + half its
// empathy and sociability together), so the kind and sociable are slower to call any a foe; a foe counts as a rival.
// A victory seen (a kill, or a fight over a possession won) moves regard for the winner by VICTORY_REGARD: up for a
// friend, down for a foe, and up by FOE_DEFEAT_REGARD more if the loser is a foe. Fleeing a fight, or one of its own
// kind's blows, is cowardice: COWARD_DISREGARD from those who hold it a friend or foe. The kindest or most sociable of
// their kind (above GIVE_LEVEL of them) may give a friend beside them a possession it lacks (GIVE_JOY); the most
// aggressive or dominant (above TAKE_LEVEL of them) may demand one, or steal it. Asked, the owner fights at a chance of its own aggression or dominance, else
// yields it. A theft seen costs the thief THEFT_DISREGARD (twice that with its victim), but wins it as much regard
// from any who hold the victim a foe, and the victim may fight to get it back. A fight lasts until one flees or falls
// to half its health: the loser gives up the possession. While it lasts, those who see it hold the taker a foe, or a
// friend if they hold its victim a foe.
inline double FOE_LEVEL = 0.5, VICTORY_REGARD = 0.3, FOE_DEFEAT_REGARD = 0.3, COWARD_DISREGARD = 0.3;
inline double GIVE_LEVEL = 0.75, GIVE_JOY = 0.5, TAKE_LEVEL = 0.75, THEFT_DISREGARD = 0.5;
// Offers: any animal may offer one beside it, not a foe, a good of its own for one of the other's: a hide, weapon or
// armour, or a portion of carried meat or fruit. A good's worth to an animal is its quality, as worn as it is, times
// its need: food the hungrier, by the hunger it would ease; a hide the colder than home, and the more for the
// prestige of the body it came from; a weapon the more aggressive; armour alike to all. It offers what it would miss
// least that the other would gain from, for what it would gain most from. Each judges the deal, misjudging each
// good's worth by up to TRADE_MISJUDGE times one less its intelligence, either way: the one offering may think better
// of it, and the other accepts only if it seems a gain. An accepted deal is a kindness both ways (TRADE_REGARD of a
// deed's weight); each side that truly gained takes joy in it (TRADE_JOY), and one that truly lost thinks the worse
// of the other instead, as much. A refusal is nothing. Choosing what to offer, an animal sees each kind of good
// through a misjudgement of its own, the same each time (Simulation::trade_bias), as large as at the deal; and it
// judges an offer from one it thinks ill of the worse, by TRADE_WARINESS times that ill regard.
inline double TRADE_MISJUDGE = 0.2, TRADE_REGARD = 0.5, TRADE_JOY = 0.5, TRADE_WARINESS = 0.5;
// Marks of the victor: one that defeats an omnivore, killing it or winning a fight over a possession, bears the mark of
// the warrior (WARRIOR_MARK), and one that defeats a predator the mark of prestige (PRESTIGE_MARK), the better; it keeps
// the better it has earned for life, whatever it does after. Each turn, all who see it and don't hold it a foe move their
// regard toward the mark's worth, at MARK_REGARD of a deed's weight; a foe who sees it holds it the more dangerous, as
// much, and dreads it (MARK_DREAD, times the mark's worth and a half plus its fear).
enum Mark : int { NoMark, WarriorMark, PrestigeMark };
inline double WARRIOR_MARK = 0.5, PRESTIGE_MARK = 1.0, MARK_REGARD = 0.05, MARK_DREAD = 0.3;
// Lands: the map in squares of LAND_PATCH cells a side. Each animal belongs to a colour group of its species, those
// whose body colours lie within Faction width of the group's (the mean of its members'), counted every LAND_TURNS
// turns. At each count a group of LAND_SIZE or more claims a square where LAND_HOLD or more of it stand, while it has
// fewer than two claimants, and keeps it while any of it is there; with none there (or, contesting it, while clearly
// losing the war) its claim fades over LAND_MEMORY counts and is given up. No lands at a Land size of 0.
// In its own land an animal heals 1 + LAND_HEALING times as fast, takes LAND_DEFENCE less of a blow from an outsider,
// and its herd counts 1 + LAND_HERD times as much (herd safety and the pack's blows); it is at ease there (LAND_COMFORT),
// and uneasy as much away while its group holds land. An outsider in its land is a trespasser: each turn there, those
// of the land who see it from their land think worse of it, at TRESPASS_REGARD of a deed's weight, growing by
// TRESPASS_GROWTH of itself each turn it stays (to a whole deed); a trespasser held a foe is prey to a hunter of the land
// standing in it. Defending its land, an animal rallies: LAND_RALLY of the chance it panics or breaks, and of its
// primal fear, is overcome, a born coward's too.
// A square claimed by two groups is contested: they are at war, and each count every member thinks worse of the other's
// colour by WAR_ENMITY of a deed's weight. Once WAR_KILLS or more of each other have died in it, a group with two thirds
// of the kills or more is clearly winning (glad, WAR_MORALE, and WAR_NERVE less likely to panic), with a third or less
// clearly losing (as much the other way). A group clearly losing gives up the land it contests and may not claim the
// winner's again while the war is remembered; each count every war's kills fade by WAR_FADE, so recent deaths decide
// it, and a war over, with no land contested, is forgotten once its kills have faded away.
// Standing: no one is made a leader. Each turn an animal is glad of how many about it hold it a friend (STANDING_JOY,
// times a half and its dominance, in full with three or more), and at ease near its standout, the member of its own
// colour group in sight it esteems most (FOLLOW_COMFORT, times that esteem and closeness, and a half plus its trust and
// sociability together); it senses that standout, and its brain learns whether keeping near it pays.
inline double STANDING_JOY = 0.2, FOLLOW_COMFORT = 0.2;
// Leaders: no one is made one, but one may be recognised. At each count of the lands (every LAND_TURNS turns), an
// animal that LEADER_FOLLOWERS or more of its colour group hold as their standout is recognised as a leader; it stays
// one until fewer than half as many do. Those whose standout it was at that count are its followers. A follower
// yields to its leader's demand without a fight, and judges its offers the kinder by LEADER_DEFERENCE; in sight of its
// leader it is LEADER_RALLY the less likely to panic or break, and it comes to its leader's defence when it is struck
// close by. A leader takes joy in its followers in sight (LEADER_JOY, in full with LEADER_FOLLOWERS of them), is
// courted first by any that may breed with it, and its partner takes LEADER_ALLURE more joy in breeding with it.
// Each senses whether it leads, and whether its standout does. LEADER_FOLLOWERS 0 for no leaders.
inline int LEADER_FOLLOWERS = 5;
// Ranks: a leader no leader follows is a chief; one a leader of rank r follows is of rank r + 1, by the longest chain
// beneath it, up to king; leaders who follow one another round a loop hold no rank over each other. The first king of a
// colour group with no emperor living is crowned its emperor, for life, and follows none. Each rank above chief makes its
// leader's bonuses RANK_BONUS the stronger, and each who sees a titled leader and holds it no foe esteems it TITLE_REGARD
// times its rank the more, each turn.
constexpr int KING_RANK = 6, EMPEROR_RANK = 7, MAX_RANK = EMPEROR_RANK;
constexpr std::array<std::string_view, MAX_RANK + 1> TITLES = {"none", "chief", "lord", "baron", "count", "duke", "king", "emperor"};
inline double RANK_BONUS = 0.25, TITLE_REGARD = 0.01;
inline double LEADER_DEFERENCE = 0.5, LEADER_RALLY = 0.5, LEADER_JOY = 0.3, LEADER_ALLURE = 0.5;
inline int LAND_PATCH = 8, LAND_SIZE = 12, LAND_HOLD = 3, LAND_MEMORY = 5, LAND_TURNS = 24, WAR_KILLS = 3;
inline double LAND_HEALING = 1.0, LAND_DEFENCE = 0.25, LAND_HERD = 0.5, LAND_COMFORT = 0.1, TRESPASS_REGARD = 0.02,
              TRESPASS_GROWTH = 0.25, LAND_RALLY = 0.75, WAR_ENMITY = 0.1, WAR_MORALE = 0.1, WAR_NERVE = 0.25,
              WAR_FADE = 0.1;
// Ambition and welcome. Standing in another group's land, an animal is drawn to stay the more it dislikes that group,
// and uneasy the more it likes it (LAND_AMBITION, times its regard for the group's colour, -1 to 1), unless welcome
// there. At each count, a group whose members think well enough of a colour group, on average, of those holding a
// view (WELCOME_LEVEL or more), welcomes it: those of it in its land are guests, not trespassers, neither thought the
// worse of nor driven out, at no unease; and they claim none of its land, nor keep a claim on it, while welcome.
// By temperament: the aggressive, dominant and fearless are drawn the more to a foe's land, the empathetic, sociable
// and tolerant the less (even uneasy there), and these are at ease as welcome guests of a friend (Simulation::ambition).
inline double LAND_AMBITION = 0.1, WELCOME_LEVEL = 0.5;
enum Possession : int { HidePossession, WeaponPossession, ArmourPossession, BowPossession };
constexpr int MeatGood = 4, FruitGood = 5, WagonGood = 6, GOODS = 7;  // what may be offered: the possessions, then food, then a wagon
inline double WOOD_LASTS = 10.0;  // times as long as a pile of food a pile of wood or bone lasts
inline int STONE_SCATTER_PER = 100;  // one stone to this many squares, scattered once in a world's life
inline double PILE_LASTS = 3.0;  // times as long as a corpse a pile lasts, since food was last dropped on it
inline double PLANT_BITE = 15;
inline double PLANT_REGROWTH = 0.02;
inline int PLANT_RECOVERY = 24;  // a day, after it is uprooted from
// Fruit: a plant holds a fruit for each PLANT_BITE of its health pool (at least one). Picking or browsing one leaves the
// plant unhurt; it may be picked while it has any, and from the morning after it is no longer full it grows one back
// each morning, until full.
// A desperate grazer, starving with food scarce about it, may uproot a plant beside it: it eats this
// many times a bite's meal, and tears this much of the plant's health away, recovering or not.
inline int UPROOT_HUNGER = 80;  // starving: hunger at least this
inline double UPROOT_MEALS = 0.5;  // share of a feast on a body of the plant's size an uprooting grazer eats
inline int UPROOT_BITE = 45;
inline double UPROOT_SORROW = 0.5;  // a grazer's sorrow at tearing into a plant, times a half plus its empathy
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
// at the equator's far end from the winter stays hot through it: the south end in the north's winter, the north end in the
// south's. As the winter washes in, the hot equator narrows from its winter side, its far edge unmoved. Every Bitzkey has a home temperature,
// and may tolerate heat or cold, suffering HARDINESS of its penalties, and FRAILTY of the other's; plants tolerate nothing, and a plant
// spawned at the start is at home in its zone's usual temperature.
// Days: every DAY_LENGTH turns is a day, and a new world's year is 360 of them. Night covers half the map, its
// columns from the top to the bottom, and moves west round it once a day. With SLEEP, an animal rests through the middle
// of its night, the dark for the diurnal and the light for the nocturnal (see the activity gene), 1 - ACTIVE_SHARE of
// the day, awake at its dusk and dawn: it may only rest, but for
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
inline double RESTING_EXPOSURE = 2.0;  // times the weather's harm to an animal that chose to rest out of its temperature
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
int month(int turn);   // the month of its year a turn falls in, 1 to 12
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

// A patch of the mental map: which (its column and row of patches), the turn it was last there, and, for each season,
// whether it has been there then and what it found: food (grass and plants for a grazer, prey, kills and carrion for a
// predator, either for an omnivore, by how close), danger (a hunter close, or a blow), and comfort (the weather's), 0..1.
struct MapPlace {
    int column = 0, row = 0, last = 0;
    std::array<bool, 4> seen{};
    std::array<double, 4> food{}, danger{}, comfort{};
    bool operator==(const MapPlace&) const = default;
};

// A colour it has a view of: the body word, how well it regards that kind, -1 to 1, and the turn of the last deed.
struct Faction {
    std::uint32_t ring = 0;
    double regard = 0.0;
    int last = 0;
    double danger = 0.0, skill = 0.0;  // as an acquaintance's
};

// One it knows: who, how well it regards them, -1 to 1, and the turn they last dealt with it.
struct Acquaintance {
    int identifier = 0;
    double regard = 0.0;
    int last = 0;
    bool operator==(const Acquaintance&) const = default;
    double danger = 0.0, skill = 0.0;  // its reputation with this one
    double garb = 0.0;  // the regard its garb has won from this one
    bool liar = false;  // seen fleeing a fight in a warrior's or prestige garb, so its garb wins nothing more
};

// A colour group (see LAND_SIZE): its species, colour (the mean of its members' body words), members as of the last
// count, and the land squares it claims.
struct Realm {
    int identifier = 0;
    Species species = Species::Grazer;
    std::uint32_t ring = 0;
    int members = 0;
    std::vector<int> patches;  // kept from the lands, not saved
};
// A claim on a land square: the group's, and how firmly, 1 fresh, fading toward giving it up at 0.
struct Claim {
    int realm = -1;
    double hold = 0.0;
    bool operator==(const Claim&) const = default;
};
using LandClaims = std::array<Claim, 2>;  // two claimants: contested
// Two groups at war, the lower identifier first, and how many of each the other has killed since it began.
struct War {
    int first = 0, second = 0;
    int first_kills = 0, second_kills = 0;  // of the second by the first, and of the first by the second
    int since = 0;
    bool operator==(const War&) const = default;
};

struct Bitzkey {
    Bitzkey(int identifier, Dna dna, int x, int y);
    // Work out again what its genome makes of it under the rules as they now stand: its health pool,
    // metabolism, stomach, burn, might, toughness, steps, diet, sight, lifespan, breeding cost and digestion.
    void derive();

    int identifier;
    Dna dna;
    int x, y;
    int was_x = 0, was_y = 0;  // where it stood when the turn's acting began (see Simulation::in_reach)
    bool was_alive = true;     // and whether it lived then
    int crowd_seen = 0;        // its own kind in its sight then, and the food about it per mouth: what one about to
    double food_seen = 0.0;    // court or breed goes by, while the animals act in tiles
    bool wet = false;  // in the sea, as the world keeps it while it moves
    double hunger = 0.0;
    double health = 100.0;
    double stamina = FULL_STAMINA;  // spent by acting, restored by resting (see ACTIVE_SHARE)
    int age = 0;
    int cooldown = 0;
    int fruit = -1;  // a plant's fruit left to pick; below 0 while it is full (see fruit_left)
    int picked_by = -1;  // a plant's last picker, glad to see it bear again (REGROWTH_JOY); -1 for none
    std::string_view last_action = "new";
    std::shared_ptr<Brain> brain;  // shared with history snapshots; copied before it learns
    int generation = 0;
    int children = 0;
    bool infant_taught = false;  // whether a parent has given it its brain, its first chance
    int taught_by = -1;  // the elder whose brain it was given, once in its life; -1 while it has none
    int carried_meat = 0, carried_fruit = 0;  // the portions it carries
    int cooked_meat = 0, cooked_fruit = 0;  // the cooked portions, which never spoil
    int carried_stone = 0, carried_bone = 0, carried_wood = 0;  // and the materials
    double boredom = 0.0;  // see BOREDOM_RISE
    double shelter_known = 0.0;  // its memory of resting in a shelter, 0 to 1 after a day of it (see SHELTER_LONGING)
    double road_known = 0.0;  // its memory of going by road, 0 to 1 after a day of it (see ROAD_LONGING)
    int hide = 0;  // the hide it wears, by the species it came from: 0 none, else Grazer, Omnivore or Predator; a body keeps it
    double hide_armour = 0.0;  // the armour the hide adds
    struct SpareTool { int kind = 0; int uses = 0; };      // a tool carried beside the one in hand, and its uses
    struct SpareHide { int kind = 0; double armour = 0.0; };  // a hide carried beside the one worn, and its armour
    std::vector<SpareTool> spare_tools;  // each a unit of its load (see Simulation::arrange)
    std::vector<SpareHide> spare_hides;
    int wagon = -1;  // the wagon it drags, by its place among the world's, or -1
    bool skinned = false;  // a body whose hide has been taken
    double regarded = 0.0;  // how well those who know it regard it, on average, as of this turn
    int thefts = 0;  // possessions it has stolen
    int mark = NoMark;  // the best mark of the victor it has earned
    int realm = -1;  // its colour group (see Realm), as of the last count; -1 for none yet
    int standout = -1;  // the member of its colour group in sight it esteems most, as last sensed; -1 for none
    int follows = -1;  // its standout as of the last count, if that one was then recognised a leader; -1 for none
    int left_leader = -1;  // why it last stopped following a leader, to be counted (see Simulation::followers_lost)
    int followers = 0;  // those whose standout it was at the last count
    bool leader = false;  // recognised a leader at the last count
    int rank = 0;  // and its rank then, by those under it: 0 none, 1 a chief, up to MAX_RANK (see TITLES)
    bool emperor = false;  // crowned its colour group's emperor, for life: a leader that follows none
    double rank_factor() const { return 1 + RANK_BONUS * std::max(0, rank - 1); }  // its leader's bonuses, by its rank
    int trespass = 0;  // turns it has stood in another group's land without a break
    double plenty = 0.0;  // the food about it as of its last survey (see FORAGE_RADIUS)
    double plenty_known = -1.0;  // its memory of that food, following it over about a day; below 0 before its first survey
    double cooked_known = 0.0;  // its taste for cooked food, a third for each cooked portion eaten, to 1 (see COOKED_JOY)
    double meat_lean = 0.0;  // a grazer's lean toward meat from marrow eaten, 0 to 1; at 1 it turns omnivore
    bool expects = false;  // whether it imagined what it last chose, so `expected` holds what it foresaw
    Foresight expected{};
    double surprise = 0.0;  // how far its last turn differed from what it foresaw, about 0..1
    double change_size = 0.0;  // and how far that turn changed things, the surprise had it foreseen no change
    int surprised_turn = -1;  // the turn they were last measured, for the probe; not saved
    std::array<int, ACTION_COUNT> last_tried{};  // the turn after it last chose each action; 0 for never
    int carried_seed = 0;  // kept from raw fruit it ate, up to its capacity, and no weight to it
    int weapon = NoWeapon, armour = NoArmour;  // what it wields and wears
    int crafts = 0;  // a bit for each craft it has discovered
    std::array<int, CRAFT_COUNT> craft_tries{};  // how often it has tried each
    std::array<int, CRAFT_COUNT> craft_source{};  // how it came to know each: 0 not recorded, 1 discovered, 2 taught by a parent, 3 by an elder, 4 watched, 5 tutored
    std::array<int, CRAFT_COUNT> craft_chain{};  // how many teachers in a line each came down through, the last its own; 0 for one it found alone
    bool bow = false;      // whether it carries a bow, kept when its arrows are spent, awaiting more
    int bow_uses = 0;      // shots left in it
    double archery = 0.0;  // its skill at hitting with a bow, 0..1, from its shots
    std::array<double, 3> desire{};  // its desire for a weapon, armour and a bow (GearDesire), 0..1, from seeing each pay off
    int tutored_turn = -1;  // the turn it last had a lesson from a tutor; -1 never
    int weapon_uses = 0, armour_uses = 0;      // how often each has been used
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
    int call = Silent;                  // what it called alongside its last action (Brain.hpp's Call), heard the turn after
    int call_turn = -1;                 // the turn it called it
    int heard_from = -1;                // the loudest caller of its kind it heard when it last chose, -1 none
    int heard_call = -1;                // and its call
    int heard_turn = -1;                // that turn
    double heard_hunger = 0.0;          // its hunger then
    bool heard_danger = false;          // and whether a hunter was in sight
    int answer_to = -1, answer_call = -1, answer_turn = -1;  // the last caller of any kind it heard, its call and when, till answered or a day passes
    int struck_turn = -1;               // the last turn it was struck
    int last_call = -1;                 // its last choice of call, to learn from with its action; -1 for none of its own
    std::array<int, MAX_SOUNDS - 1> sounds{-1, -1};       // the sounds its call ran on to after the first (0..4), -1 where it stopped
    std::array<int, MAX_SOUNDS - 1> last_sounds{-1, -1};  // likewise of its last choice of call, to learn from
    double chosen_value = 0.0;          // what its brain valued its last choice at, to judge how the turn went against it
    int chosen_turn = -1;               // and the turn it chose it
    int call_heeded = -1;               // the caller of its kind it heard as it last chose, whose call shares in how that went
    // Its last call's lesson, held a turn for what it then did to those who heard it (Call empathy): the senses it called
    // by, the outputs of its sounds (and of where it stopped), the target, and whether one is held.
    Senses call_senses{};
    std::array<int, MAX_SOUNDS> call_outputs{-1, -1, -1};
    double held_call_target = 0.0;
    bool call_held = false;
    double call_credit = 0.0;  // its listeners' surprise this turn, each weighed by its care for it
    int call_listeners = 0;    // and how many they were
    Held held{};                        // what its memory neurons hold, read back the next time it senses (Brain.hpp)
    // Its senses at the choices before its last, most recent first, back to the first its memory wasn't written at, and
    // no more than Memory steps: what learning back in time goes over. Whether its memory was written at its last choice.
    std::vector<Senses> past;
    bool last_wrote = false;
    std::vector<MapPlace> places;  // its mental map, in the order it first came to each place
    std::vector<Acquaintance> known;  // those it knows, in the order it came to know them
    std::vector<Faction> factions;  // the colours it has a view of
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
    struct Entry {  // an experience of its diary
        Memory memory;
        double vividness = 0.0;  // how much came of it, when it happened
        int turn = 0;
    };
    std::vector<Entry> diary;  // its most vivid experiences
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
    bool last_panicked = false;  // whether that was a panicked flight, which teaches though it was no choice
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
    int capacity() const { return trait(Size) + 1 + (wagon >= 0 ? SHELTER_UNITS : 0); }  // the portions it can carry, the more with a wagon
    int load() const {
        return carried_meat + carried_fruit + cooked_meat + cooked_fruit + carried_stone + carried_bone + carried_wood +
               static_cast<int>(spare_tools.size() + spare_hides.size());
    }
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
    // How long its infancy lasts, in turns: FEEBLE_YOUTH of its lifespan (a predator's PREDATOR_INFANCY as long, a
    // grazer's GRAZER_INFANCY), the
    // smallest SMALL_MATURITY of that, the larger more of it.
    double infancy() const {
        const double maturity = SMALL_MATURITY + (1 - SMALL_MATURITY) * share(body[Size]);
        const double share = species == Species::Predator  ? PREDATOR_INFANCY
                             : species == Species::Grazer   ? GRAZER_INFANCY
                             : species == Species::Omnivore ? OMNIVORE_INFANCY
                                                            : 1.0;
        return FEEBLE_YOUTH * share * maturity * lifespan;
    }
    bool infant() const { return species != Species::Plant && age < infancy(); }
    bool feeble_at_age() const { return species != Species::Plant && (age < infancy() || age > AGEING * lifespan); }
    // A temperament trait as it is now: the feeble are drawn out of themselves, the old thrice as patient and twice as
    // curious and infants twice as curious, each of a half plus the trait, so that none is left at nothing, and up to 1.
    double feeling(int trait) const {
        const double value = share(mind[trait]);
        if (trait == Curiosity && feeble_at_age()) return std::min(1.0, 2.0 * (0.5 + value));
        if (trait == Patience && feeble_at_age() && !infant()) return std::min(1.0, 3.0 * (0.5 + value));
        return value;
    }
    // Its intellect now, as a share of the full range, as it counts in all it does: a predator's
    // PREDATOR_INTELLIGENCE times over, up to the full range.
    double intelligence() const { return std::min(1.0, feeling(Intellect) * (species == Species::Predator ? PREDATOR_INTELLIGENCE : 1.0)); }
    // Its fear, as its effects feel it: a predator's PREDATOR_FEAR as much, and less the hungrier it is; and GEAR_COURAGE
    // the less for each of a weapon or bow in hand and armour worn.
    double fearfulness() const {
        return feeling(Fear) * (species == Species::Predator ? PREDATOR_FEAR * unafraid() : 1.0) * (1 - GEAR_COURAGE * gear_held());
    }
    bool wielding() const { return weapon != NoWeapon || bow_uses > 0; }
    bool wearing() const { return armour != NoArmour; }
    int gear_held() const { return wielding() + wearing(); }  // a weapon or bow in hand, and armour worn
    bool has_tool(int kind) const {  // in hand or carried: either serves for crafting
        if (weapon == kind) return true;
        for (const SpareTool& tool : spare_tools) {
            if (tool.kind == kind) return true;
        }
        return false;
    }
    bool any_tool() const { return weapon != NoWeapon || !spare_tools.empty(); }
    bool any_hide() const { return hide > 0 || !spare_hides.empty(); }
    // The share of its fear's effects hunger leaves a predator: all when fed, falling with its hunger to none
    // as it starves; any other animal feels them all.
    double unafraid() const { return species == Species::Predator ? std::max(0.0, 1 - hunger / 100) : 1.0; }
    double herd_instinct() const { return std::min(feeling(Empathy), feeling(Patience)); }
    // How much a carrion-eater it is: 0 up to the middle of its carrion trait's range, rising to 1 at the top.
    double scavenger() const { return std::max(0.0, 2 * physique(Carrion) - 1); }
    Profile profile() const;
    // Ready, not frail (neither an infant nor old: feeble), not carrying young, and able to pay for breeding with energy to spare.
    bool can_breed() const {
        return in_heat && !feeble && cooldown == 0 && womb.empty() && hunger + breeding_price() <= 100 - BREED_RESERVE;
    }
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
    double climate_steps(double here) const {  // a hide keeps out HIDE_WARMTH steps of cold
        return hide && here > climate.temperature ? std::max(0.0, here - climate.temperature - HIDE_WARMTH) : std::abs(here - climate.temperature);
    }  // part steps while the weather turns
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
int call_reach(const Bitzkey& caller);  // how many steps its calls carry (Call reach)
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
// Its instincts, on top of what its brain values its `options` at: to hunt and feast, the mating call, a scavenger's
// craving, loyalty to its herd, an infant's to its kind, and a parent's to feed its young (the hungriest that hungry).
void add_instincts(const Bitzkey& agent, Values& valued, std::span<const int> options, double hunger, double hungriest_young);
// Hope for what it has never done: an option it has never taken in its life valued at least at UNTRIED_HOPE of the average of
// those it has, since a brain learns an action's worth only by taking it, and one never taken stays valued at next to nothing.
void hope_for_untried(const Bitzkey& agent, Values& valued, std::span<const int> options);
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
        // Turns the plants grow alone before the animals spawn, which aren't counted: the world starts at turn 0 after
        // them (a new world's setup offers PREGROWTH_TURNS).
        int pregrowth = 0;
    };

    explicit Simulation(Settings settings);
    Simulation(int width, int height, int population, std::optional<std::uint64_t> seed);

    int width() const { return width_; }
    int height() const { return height_; }
    int turn() const { return turn_; }
    // The world's diary: the first time each thing was done, kept in the world file. It only watches, and draws on no
    // chance, so a world unfolds the same with it. Each event is written once, for each species and craft.
    struct Milestone {
        int turn;
        std::string event, species, craft;
        int who;  // the Bitzkey who did it, or -1
    };
    const std::vector<Milestone>& diary() const { return diary_; }
    int diary_since() const { return diary_since_; }  // what came before this turn went unrecorded, in a world older than the diary
    void note_first(std::string_view event, Species species, int who, std::string_view craft = {});
    int deaths() const { return deaths_; }
    // Every death so far, by species and then cause.
    using DeathCauses = std::array<std::map<std::string, int, std::less<>>, 4>;
    const DeathCauses& death_causes() const { return death_causes_; }
    int next_identifier() const { return next_identifier_; }
    std::optional<int> population_cap() const { return population_cap_; }
    void set_population_cap(std::optional<int> cap) { population_cap_ = cap; }
    double mind_mutation() const { return mind_mutation_; }
    double body_mutation() const { return body_mutation_; }
    void set_mind_mutation(double percent) { mind_mutation_ = percent; }
    void set_body_mutation(double percent) { body_mutation_ = percent; }
    void set_mutation(double mind, double body) { mind_mutation_ = mind; body_mutation_ = body; }

    const std::vector<std::unique_ptr<Bitzkey>>& agents() const { return agents_; }
    std::vector<Bitzkey*> living() const;
    int grass_at(int x, int y) const;
    int pile_at(int x, int y, PileKind kind) const { return in_bounds(x, y) ? piles_[kind][static_cast<std::size_t>(y) * width_ + x] : 0; }
    int meat_pile_at(int x, int y) const { return pile_at(x, y, MeatPile); }  // portions lying there
    int fruit_pile_at(int x, int y) const { return pile_at(x, y, FruitPile); }
    void add_to_pile(int x, int y, PileKind kind, int portions = 1) {
        claim(x, y);
        const std::size_t cell = static_cast<std::size_t>(y) * width_ + x;
        piles_[kind][cell] += portions;
        if (portions > 0 && kind < BoneAxePile) pile_turns_[kind][cell] = turn_;  // a gear pile keeps its wear there instead
    }
    void add_to_pile(int x, int y, bool meat, int portions = 1) { add_to_pile(x, y, meat ? MeatPile : FruitPile, portions); }
    void spoil_piles();  // those left PILE_LASTS corpses' lifetimes since their last drop are gone
    // What has become of food carried, since the world was loaded or begun, for the probe (counted as acting tiles run).
    enum Carrying { MeatFromKill, MeatFromPile, FruitFromPlant, FruitTorn, FruitFromPile, MeatDropped, FruitDropped,
                    PileEaten, CarriedEaten, Spoiled, CARRYING_COUNT };
    struct Tally {  // atomic, yet a world can still be copied
        std::atomic<long long> count{0};
        Tally() = default;
        Tally(const Tally& other) : count(other.load()) {}
        Tally& operator=(const Tally& other) { count = other.load(); return *this; }
        void operator++() { ++count; }
        void operator+=(long long more) { count += more; }
        long long load() const { return count.load(); }
    };
    std::array<Tally, CARRYING_COUNT> carrying{};
    // Since this run began: blows at its own kind, how many fell on enemies and friends, and how many of each stood in
    // sight at those moments, to compare with chance; all blows at any enemy or friend; alerts heeded and refused.
    enum RegardCount { KinBlows, KinBlowsEnemy, KinBlowsFriend, KinInSight, EnemiesInSight, FriendsInSight, BlowsEnemy, BlowsFriend,
                       AlertsFromFriends, AlertsFromOthers, AlertsRefused, REGARD_COUNTS };
    std::array<Tally, REGARD_COUNTS> regard_counts{};
    // The sea: whether a square lies under it (none off the map, nor in a world without sea).
    // Whether a column lies in the night now: half the map, moving west a whole turn round each day.
    bool night(int x) const;
    bool dawn(int x) const;  // whether night ended at this column this turn
    static int fruit_capacity(const Bitzkey& plant);
    static int fruit_left(const Bitzkey& plant) { return plant.fruit < 0 ? fruit_capacity(plant) : plant.fruit; }
    int night_start() const;  // the westernmost column of the night, which runs east half the map's width from it
    // Whether an animal sleeps where it stands: in the middle of its night (the dark for the diurnal, the light for the
    // nocturnal), the 1 - ACTIVE_SHARE of the day there, awake at its dusk and dawn.
    bool resting_time(const Bitzkey& agent) const;
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
    int& grass_cell(int x, int y) { claim(x, y); return grass_[static_cast<std::size_t>(y) * width_ + x]; }
    void clear_grass() { std::fill(grass_.begin(), grass_.end(), 0); }
    std::size_t grass_tufts() const;
    const Insights& insights() const { return insights_; }
    const Census& census() const { return census_; }
    void restart_census();  // the colour graph begun afresh: those alive now its first snapshot
    void go_fallow();  // worked land untended FALLOW_YEARS gone wild
    // A child hearing its parent call: the call, in the parent's situation, valued as its own best call, to copy;
    // and what the parent then did, in its own senses holding the call, valued as its own turn, to take in.
    void learn_from_parent(Bitzkey& child, double target, double call_target, double rate);
    void read_body_language(Bitzkey& listener, double target, double call_target, double rate);  // see BODY_LANGUAGE
    void teach_call(Bitzkey& agent, const Senses& senses, const std::array<int, MAX_SOUNDS>& outputs, double target, double rate);
    void settle_call(Bitzkey& agent);  // its held call lesson, with what it did to its listeners (Call empathy)
    void credit_callers(const std::vector<Bitzkey*>& agents, const std::vector<double>& targets);  // see CALL_EMPATHY
    int tended_at(int x, int y) const { return in_bounds(x, y) ? tended_[static_cast<std::size_t>(y) * width_ + x] : 0; }
    int morning_fruit(Bitzkey& plant);  // the fruit a plant bears this morning, any bone fed its land spent
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
    // A gift handed over, for the probe to look for trade: the last GIFT_LOG of them, oldest first, not saved.
    struct Gift {
        int turn, giver, receiver, possession, worth;  // worth: how good the gift was (value_of), to the giver
        Species species;
        bool kin;  // parent and child, or of a parent
        int giver_realm, receiver_realm;
    };
    static constexpr std::size_t GIFT_LOG = 20000;
    std::vector<Gift> gifts_so_far() const;
    const std::array<std::array<std::array<long long, 3>, CALL_COUNT>, 4>& call_payoffs() const { return call_payoffs_; }
    // Followers who stopped holding a recognised leader as their standout, by species and why: it died, one of them left
    // the other's colour group, its esteem fell to nothing, or one in sight outshone it; since the world began or was loaded.
    const std::array<std::array<long long, 4>, 4>& followers_lost() const { return followers_lost_; }
    // An offer made, accepted or not, for the probe: the last GIFT_LOG of them, oldest first, not saved.
    struct Trade {
        int turn, offerer, owner, wanted, offered;
        Species species;
        bool withdrawn, accepted;
        double offerer_gain, owner_gain;  // what each would truly gain by the deal, worth for worth
        double offerer_wits, owner_wits;  // their intelligence
    };
    std::vector<Trade> trades_so_far() const;
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
    // Hot land near enough water: it grows grass as if medium, and can be tilled.
    bool watered(int x, int y) const { return in_bounds(x, y) && !watered_.empty() && watered_[static_cast<std::size_t>(y) * width_ + x]; }
    int watered_in_row(int y) const { return watered_rows_.empty() ? 0 : watered_rows_[static_cast<std::size_t>(y)]; }
    double growth_at(int x, int y) const { return watered(x, y) ? 1.0 : growth(y); }
    // A seed waiting where it fell for the temperature it can grow in.
    struct Seed {
        int x, y;
        Dna dna;
        int generation, sown;
        int sower = -1;  // the animal that sowed it, if one did
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
    void rein_in(Bitzkey& agent, const Senses& senses, const Values& valued);  // what it values beyond the Value limit, taught back
    void foresee(Bitzkey& agent, const Foresight& change);  // one thing it foresaw, taught what came
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
        Bitzkey* shoot_at = nullptr;     // with a bow: the prey it would hunt in sight, else the nearest foe
        Bitzkey* practise_on = nullptr;  // and the nearest plant in sight, to practise on
        Bitzkey* pupil = nullptr;        // one beside it worth tutoring
        int lesson = -1;                 // in which craft, or CRAFT_COUNT for archery
        std::optional<Cell> grass;
        std::optional<Cell> meat_pile, fruit_pile;  // the nearest of each in sight
        std::optional<Cell> stone_pile, bone_pile, wood_pile, gear_pile;
        std::optional<Cell> eat_meat, eat_fruit, fire;  // the nearest food to eat, cooked or not, and fire
        PileKind gear_kind = BoneAxePile;  // of the gear it may take
        int wagon_near = -1;              // a wagon no one drags within a step
        Bitzkey* dressable = nullptr;  // the body it would dress: a fresh kill, or else carrion
        bool grass_here = false;
        Bitzkey* carrion = nullptr;
        Bitzkey* kill = nullptr;
        Bitzkey* flora = nullptr;
        Bitzkey* alert_to = nullptr;  // what it may alert its herd to
        std::optional<Cell> revisit;  // where it last fed, to go back to
        std::vector<Bitzkey*> young;  // its own infants within its sight, to feed
        Bitzkey* student = nullptr;  // a willing student beside an elder
        Bitzkey* assailant = nullptr;
        double rivalry = 0.0;
        double regard_near = 0.0;  // for the nearest it knows
        double skill_near = 0.0;  // the nearest it knows: how skilled it holds them
        int known_steps = -1;  // and how many steps off; -1 for none in sight
        int teacher_steps = -1;  // steps to the elder who taught it, if in sight
        int kin = 0;
        int others = 0;
        Bitzkey* defend_target = nullptr;
        Bitzkey* uprootable = nullptr;  // a plant beside a starving grazer, with food scarce
        Bitzkey* timber = nullptr;  // a plant to fell with an axe: one beside it, bare or bearing, else the bearing one it sees
        bool tears = false;  // it may tear into that plant, so it may tear its fruit from it too
        bool for_herd = false;
        bool herd = true;
        int herd_near = 0;
        std::optional<Cell> herd_middle;
        std::optional<Cell> store;  // where it would bring the food it carries for its herd
        Bitzkey* fletch_for = nullptr;  // whose bow it would make arrows for: its own, else one beside it
        double exposure = 0.0;  // how far from home it is, 0.5 a step to 1, times the share of exposure it suffers
        double spared = 0.0;  // the exposure kept off it by a shelter, a fire or a parent, which leaves it none
        int inside = 0;  // 2 resting in a shelter, 1 standing in one
        double home_distance = 0.0;  // how far the nearest row it is at home in lies, as sensed, times the same share
        int young_near = 0;  // its own young, still infants, within its sight
        double envy = 0.0;  // how much better the best possessions it sees are than its own
        Bitzkey* claim_from = nullptr;  // one beside it with a possession better than its own, to demand or steal
        int claim = -1;  // which possession
        Bitzkey* gift_to = nullptr;  // a friend beside it whose possession is worse than its own
        int gift = -1;
        Bitzkey* trade_with = nullptr;  // one beside it, not a foe, it may offer a good for one of its own
        int wanted = -1, offered = -1;  // the good it wants, and the one it would offer
        double foe_closeness = 0.0;
        double cooking_smell = 0.0;
        double prey_flanked = 0.0;  // its pack beside its prey, opponent or rival: a half each, to 1  // how close the nearest smell of cooking is, cooked food or a fire cooked on, 1 beside it
        double land = 0.0;  // 1 in its own land, -1 away from the land its group holds, 0 with none
        double war = 0.0;  // its group's wars: 1 clearly winning, -1 clearly losing, 0 neither or at peace
        double admired = 0.0;  // those in sight who hold it a friend, a third each, to 1
        double standout = 0.0;  // its esteem for its standout, times how close it is
        double followed = 0.0;  // a leader's followers in sight, a LEADER_FOLLOWERS-th each, to 1
        double ambition = 0.0;  // its dislike of the group whose land it stands in, uninvited: -1 a friend's, 1 a foe's
        int heard_from = -1, heard_call = -1;  // the loudest caller of its kind it heard, and its call
        int loudest_from = -1, loudest_call = -1;  // the loudest caller of any kind it heard, and its call
        bool panic = false;
        bool frozen = false;  // in primal fear, frozen: it does nothing this turn
    };
    struct Sensed {
        Senses senses{};
        std::vector<int> options;
        std::vector<int> calls;  // the calls open to it, silence first (Brain.hpp's Call)
        Situation situation;
    };

    // A hunter's view of its territory: how crowded with rivals, and whether its prey are common enough to hunt.
    void survey(Bitzkey& agent);
    Sensed sense(Bitzkey& agent, bool surveyed = false);  // surveying first, unless it already has this turn
    // What an animal finds where it stands, as it senses it, noted on its mental map.
    void note_place(Bitzkey& agent, const Senses& senses);
    // What `other` did to `agent`, worth `deed`, -1 to 1, noted in its regard for them.
    // Only its reputation moves, not its regard, unless `regarded`.
    void note_deed(Bitzkey& agent, const Bitzkey& other, double deed, double weight = 1.0, DeedKind kind = PlainDeed, bool regarded = true);
    void share_hearths(const std::vector<Bitzkey*>& agents, const std::vector<Sensed>& plans);  // those at a fire together bond
    void bond_in_defence(Bitzkey& defender, const Bitzkey& assailant);  // and those who stood together against an attacker
    // A deed of `doer`'s, done to `target` if to anyone, noted by those who see it.
    void deed_seen(const Bitzkey& doer, double deed, const Bitzkey* target = nullptr, DeedKind kind = PlainDeed, int craft = -1, Culture culture = NoCulture);
    static bool family(const Bitzkey& one, const Bitzkey& other);  // parent, child or sibling
    // The outputs its last call is learnt by: its first sound's (or silence's, or the alert's), then where it went on to,
    // or stopped, at each later place offered; -1 for places it never reached.
    static std::array<int, MAX_SOUNDS> utterance_outputs(const Bitzkey& agent);
    static int sounds_made(const Bitzkey& agent);  // how many sounds its current call has, 0 silent or alerting
    void culture_seen(Bitzkey& seer, const Bitzkey& doer, Culture culture);  // the esteem its kind's culture gives the deed
    double regard_for(const Bitzkey& agent, const Bitzkey& other) const;  // of it, or else of its colour; 0 for neither
    double skill_held(const Bitzkey& agent, const Bitzkey& other) const;  // how skilled it holds it, or else its colour
    void note_faction(Bitzkey& agent, const Bitzkey& other, double deed, double weight, DeedKind kind, bool regarded);
    static const Faction* faction_of(const Bitzkey& agent, const Bitzkey& other);
    double harm_felt(const Bitzkey& agent, double regard) const;    // Grudge joy
    double help_felt(const Bitzkey& agent, double regard) const;    // Friend joy
    void tally_attack(const Bitzkey& agent, const Bitzkey& victim);  // who it struck of those of its kind it could
    void think(Bitzkey& agent);  // one animal senses, learns, chooses and acts
    // Animals think at once, in phases, so that their brains can be worked as one batch: all sense the
    // world as it stands; all their brains value the actions open to them; all learn from their
    // previous decisions; all choose; then each acts in turn, finding the world as those before it left
    // it. What herd-mates learn from watching is learnt last, all together.
    void think_all(const std::vector<Bitzkey*>& agents);
    void teach(Bitzkey& elder, Bitzkey& student);  // agreed now, done at the turn's end
    void teach_all();  // each student given its elder's brain, once the turn's lessons are learnt
    bool first_chance(const Bitzkey& elder, const Bitzkey& student) const;  // an infant untaught, and this its parent
    bool willing_student(const Bitzkey& elder, const Bitzkey& student) const;
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
        // Finer parts: indexing the cells, births, the deeds witnessed after acting, conceiving, learning what was
        // witnessed, teaching, and letting go of the turn's working memory.
        double index = 0, births = 0, witnessing = 0, conceiving = 0, witnessed_learning = 0, teaching = 0, cleanup = 0;
        // The tiles (see in_tiles), for upkeep [0] and for acting [1]: laying them out, working them, and what reached beyond them.
        std::array<std::array<double, 3>, 2> tiles{};
        double ahead = 0, upkeep_seen = 0;  // upkeep: what is worked out beforehand, and its deeds witnessed
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
    void take_meat(Bitzkey& agent, Situation& s);  // a portion from the nearer of a pile and a fresh kill, carried off
    void take_fruit(Bitzkey& agent, Situation& s);  // from the nearer of a pile and a plant, or torn from one it may uproot
    void take_material(Bitzkey& agent, Cell pile, PileKind kind, int& carried);  // one from a pile, carried off
    void dress(Bitzkey& agent, Bitzkey& corpse);  // all that is left of a body made into bone, a pile where it lay
    void take_gear(Bitzkey& agent, Situation& s);  // wielded or worn at once
    void discover(Bitzkey& agent);  // perhaps, a craft whose materials it holds
    void make_gear(Bitzkey& agent, int action);    // from what it has at hand, wielded or worn at once; else work on a bow or wagon
    Parts at_hand(const Bitzkey& agent, int craft, const std::vector<Bitzkey*>& near) const;  // its pack, the work, piles about, friends' packs
    static bool enough(const Parts& parts, int craft);  // whether these parts make the craft
    bool has_set(const Bitzkey& agent, int craft, const std::vector<Bitzkey*>& near) const;  // all the craft needs, at hand
    bool can_finish(const Bitzkey& agent, int action) const;  // a bow or wagon, with the tool and the set at hand
    bool can_add(const Bitzkey& agent, int craft) const;  // carries a part the work on its square still lacks
    void add_to_work(Bitzkey& agent, int action);
    void use_parts(Bitzkey& agent, int craft, const Parts* exactly = nullptr);  // spent in that order, friends who gave sharing in it
    void craft_arrows(Bitzkey& agent, Bitzkey& archer);  // a wood at hand made into arrows for the archer's bow
    void rot_works();  // work left SITE_TURNS falls apart
    const Work& work_at(int x, int y) const { return works_[cell_index(x, y)]; }
    void drop_gear(int x, int y, PileKind kind, int uses);  // its wear stays with the pile, shared out as it is taken
    static void wear_weapon(Bitzkey& agent);       // a use of what it wields, which may break it
    static void wield_best(Bitzkey& agent);        // in hand the best tool it carries that it may wield
    static void wear_best(Bitzkey& agent);         // on it the best hide it carries
    void arrange(Bitzkey& agent);                  // both, and what it can no longer carry falls
    void spend_tool(Bitzkey& agent, int kind);     // one of that kind, in hand or carried, used up
    static void wear_armour(Bitzkey& agent);
    static double gear_toughness(const Bitzkey& agent);  // with what it wears
    static double gear_force(const Bitzkey& agent);      // how many times as hard what it wields strikes
    void cut(Bitzkey& agent, Bitzkey& plant);  // an axe fells a plant, which leaves its wood
    bool fire_friendly(const Bitzkey& agent, Cell fire) const;  // lit by it or a herd-mate
    void scatter_stones();  // one world's only stones, on the land, by a draw of its own
    void eat_pile(Bitzkey& agent, Situation& s);  // a portion of whichever pile in sight would fill it more
    void eat_carried(Bitzkey& agent);  // a portion of whichever it carries would fill it more
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
    Around around(const Bitzkey& agent, int radius) const {
        if (guard_claims && acting_) {  // the whole of what it looks over, by its corners
            claim(agent.x - radius, agent.y);
            claim(agent.x + radius, agent.y);
            claim(agent.x, agent.y - radius);
            claim(agent.x, agent.y + radius);
        }
        return Around(*this, {agent.x, agent.y}, radius, &agent);
    }
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
    bool warmed_by_fire(const Bitzkey& agent, int here) const;
    int fire_at(int x, int y) const { return in_bounds(x, y) ? fires_[static_cast<std::size_t>(y) * width_ + x] : 0; }  // the turn it goes out
    void light_fire(Bitzkey& agent);  // struck from two stones it carries, which it keeps, or of its club, spent
    void feed_fire(Bitzkey& agent, Cell fire);
    void cook(Bitzkey& agent, Cell fire);  // what it carries, raw, carried on cooked
    // Raw food for its own gut to cook: what it carries, else, with room for it, a fresh kill, a meat pile, a bearing
    // plant or a fruit pile beside it.
    enum class HandFood { None, Carried, Kill, MeatPile, Plant, FruitPile };
    HandFood food_to_cook(const Bitzkey& agent, const Situation& s) const;
    void cook(Bitzkey& agent, Situation& s);  // that, taken up raw and cooked on the fire beside it
    void taste(Bitzkey& agent, bool cooked);  // the delight of a cooked portion, or the letdown of a raw one once it knows better
    double cooking_smell(const Bitzkey& agent, int sight) const;
    double food_about(const Bitzkey& agent) const;
    int pack_beside(const Bitzkey& agent, const Bitzkey& prey) const;  // its pack-mates beside the prey, but itself  // its food within FORAGE_RADIUS, for its sense of plenty
    bool food_in_sight(const Bitzkey& agent) const;  // any grass or living plant within its sight
    void last_bite(Bitzkey& agent);  // uneasy if what it ate left no grass or plant in its sight  // closeness of the nearest cooking smelt, 0 none
    bool take_cooked(Bitzkey& agent, Cell pile, bool meat);  // a cooked portion, if that is what lies there
    bool arable_at(int x, int y) const { return in_bounds(x, y) && arable_[static_cast<std::size_t>(y) * width_ + x]; }
    void keep_seed(Bitzkey& agent);
    void eat_seed(Bitzkey& agent);
    void plant_seed(Bitzkey& agent);
    bool shelter_at(int x, int y) const { return in_bounds(x, y) && shelters_[static_cast<std::size_t>(y) * width_ + x]; }
    bool safe_inside(const Bitzkey& animal) const { return animal.last_choice == Rest && under_cover(animal); }
    bool under_cover(const Bitzkey& animal) const { return shelter_at(animal.x, animal.y) || animal.wagon >= 0 || parked_.contains(cell_index(animal.x, animal.y)); }
    std::size_t cell_index(int x, int y) const { return static_cast<std::size_t>(y) * width_ + x; }
    struct Wagon {
        int x = 0, y = 0;
        int hitched_to = -1;                // who drags it, or -1
        std::array<int, 7> goods{};         // what it holds beyond its hitcher's own load: stone, wood, bone, meat, fruit, cooked meat and fruit
    };
    const std::vector<Wagon>& wagons() const { return wagons_; }
    void hitch(Bitzkey& agent, int wagon);  // it drags it, and what it holds comes to its load as far as it has room
    void unhitch(Bitzkey& agent);  // it leaves it where it stands, with what it can no longer carry
    int site_at(int x, int y) const { return in_bounds(x, y) ? sites_[static_cast<std::size_t>(y) * width_ + x] : 0; }  // units in an unfinished shelter
    void construct(Bitzkey& agent);  // a unit of what it carries, stone first, into the shelter on its square
    bool road_at(int x, int y) const { return in_bounds(x, y) && roads_[static_cast<std::size_t>(y) * width_ + x]; }
    int road_work_at(int x, int y) const { return in_bounds(x, y) ? road_work_[static_cast<std::size_t>(y) * width_ + x] : 0; }  // turns laid on an unfinished road
    void build_road(Bitzkey& agent);  // a turn's work on the road on its square
    bool fertilized_at(int x, int y) const { return in_bounds(x, y) && fertilized_[static_cast<std::size_t>(y) * width_ + x]; }
    bool can_crush(const Bitzkey& agent) const {  // a bone to hand and something to crush it with
        return (agent.has_tool(Club) || agent.has_tool(StoneAxe) || agent.carried_stone > 0) && (agent.carried_bone > 0 || pile_at(agent.x, agent.y, BonePile) > 0);
    }
    bool crush_bone(Bitzkey& agent);  // spends the bone it carries, or else one from the pile on its square; false if none is left
    void eat_marrow(Bitzkey& agent);
    void fertilize(Bitzkey& agent);
    void skin(Bitzkey& agent, Bitzkey& corpse);
    // A fight over a possession: the one taking, the one it is taken from, which, and whether it is already taken.
    struct Fight {
        int thief, victim, possession;
        bool taken;
        bool operator==(const Fight&) const = default;
    };
    const std::vector<Fight>& fights() const { return fights_; }
    const Fight* fight_of(int identifier) const;  // the fight it is in, if any
    // Lands (see LAND_SIZE): the colour groups, the wars between them, and the claims on the square holding x, y
    // (nullptr off the map or with no lands counted yet).
    const std::map<int, Realm>& realms() const { return realms_; }
    const std::vector<War>& wars() const { return wars_; }
    int land_patch() const { return land_patch_; }
    const LandClaims* claims_at(int x, int y) const;
    const LandClaims* square_claims(int patch) const {
        return patch >= 0 && static_cast<std::size_t>(patch) < lands_.size() ? &lands_[static_cast<std::size_t>(patch)] : nullptr;
    }
    int land_columns() const { return land_columns_; }  // land squares to a row
    static bool claims(const LandClaims& land, int realm) { return realm >= 0 && (land[0].realm == realm || land[1].realm == realm); }
    bool in_own_land(const Bitzkey& agent) const;
    bool trespassing(const Bitzkey& agent) const;  // in a land with a claimant other than its own group, that does not welcome it
    bool welcomed(int owner, int guest) const { return owner >= 0 && guest >= 0 && welcome_.contains({owner, guest}); }
    // Its regard for a colour group, by its colour, if it holds a view.
    std::optional<double> view_of_realm(const Bitzkey& agent, int realm) const;
    void count_welcomes();  // which groups welcome which, from what their members think of them
    // How its group's war with `enemy` goes, or, with none named, all its wars together: 1 clearly winning, -1 clearly
    // losing, 0 neither, too few killed to tell, or at peace.
    int war_standing(int realm, int enemy = -1) const;
    // Whether a hunter standing in its own land may strike a trespasser there that it holds a foe.
    bool drives_out(const Bitzkey& hunter, const Bitzkey& other) const;
    static bool convinced(const Bitzkey& agent, int trait) { return agent.feeling(trait) >= CONVICTION_LEVEL; }
    // An empathetic predator spares the young and those carrying young, alive or dead.
    static bool spares(const Bitzkey& hunter, const Bitzkey& other) {
        return hunter.species == Species::Predator && (other.infant() || !other.womb.empty()) && convinced(hunter, Empathy);
    }
    void add_scruples(const Bitzkey& agent, Values& valued, const Situation& s) const;
    int sight_now(const Bitzkey& agent) const;  // its sight, less out of its own hours (OFF_HOURS_SIGHT)
    static double gluttony(const Bitzkey& agent);  // a half, plus half its appetite, less half its kinship, 0..1
    static double gluttony_unease(const Bitzkey& agent, double hunger_before);  // for a meal begun that hungry
    static double generosity(const Bitzkey& agent);  // the strongest of patience, sociability and empathy, less that of dominance, aggression and appetite
    // The food piles, fires and shelters it sees, the one nearest its herd's middle; else that middle, to begin one.
    std::optional<Cell> herd_store(const Bitzkey& agent, Cell middle, int sight) const;
    void provision(Bitzkey& agent, Cell store);  // its food brought there and put down
    bool preys_on(const Bitzkey& hunter, const Bitzkey& other) const {
        if (spares(hunter, other)) return false;
        return is_prey(hunter, other) || (!convinced(hunter, Tolerance) && (drives_out(hunter, other) || grazer_assails(hunter, other)));
    }
    // A grazer has no prey, but may attack a foe within reach.
    bool grazer_assails(const Bitzkey& grazer, const Bitzkey& other) const {
        return grazer.species == Species::Grazer && other.species != Species::Plant && !other.guarded && within_reach(grazer, other) &&
               is_foe(grazer, other);
    }
    // The share of a blow from an outsider that lands on one in its own land, and how much more its herd counts there.
    double land_guard(const Bitzkey& struck, const Bitzkey& striker) const {
        return striker.realm != struck.realm && in_own_land(struck) ? 1 - LAND_DEFENCE : 1.0;
    }
    double land_herd(const Bitzkey& agent) const { return in_own_land(agent) ? 1 + LAND_HERD : 1.0; }
    // How much of its panic, or primal fear, is left it in a fight, by its land and its wars: LAND_RALLY of it overcome
    // defending its own land from an outsider, and WAR_NERVE less, or more, clearly winning or losing.
    double nerve(const Bitzkey& agent, const Bitzkey* foe) const {
        const bool rallied = in_own_land(agent) && (!foe || foe->realm != agent.realm);
        return (rallied ? 1 - LAND_RALLY : 1.0) * std::max(0.0, 1 - WAR_NERVE * war_standing(agent.realm)) *
               (leader_in_sight(agent) ? 1 - std::min(1.0, LEADER_RALLY * find_agent(agent.follows)->rank_factor()) : 1.0);  // and its leader in sight
    }
    // Whether it follows that one, a leader, as of the last count; and whether its leader is in its sight.
    static bool follows(const Bitzkey& follower, const Bitzkey& leader) { return leader.leader && follower.follows == leader.identifier; }
    bool leader_in_sight(const Bitzkey& agent) const;
    void pay_callers(Bitzkey& listener, const Situation& now, std::map<int, int>& paid);  // see CALL_PAYOFF
    void shoot(Bitzkey& archer, Bitzkey& target, bool practice);  // see BOW_USES
    static double hit_chance(const Bitzkey& archer, int away);
    static int lesson_for(const Bitzkey& tutor, const Bitzkey& pupil, int turn);  // what it has worth teaching it, -1 for nothing
    void tutor(Bitzkey& tutor, Bitzkey& pupil, int lesson);  // see TUTOR_JOY
    static double ambition(const Bitzkey& agent, double regard, bool welcome);  // see LAND_AMBITION
    void recognise_leaders();
    static double mark_worth(int mark) { return mark == PrestigeMark ? PRESTIGE_MARK : mark == WarriorMark ? WARRIOR_MARK : 0.0; }
    bool is_friend(const Bitzkey& agent, const Bitzkey& other) const { return regard_for(agent, other) >= REGARD_STRONG; }
    bool is_foe(const Bitzkey& agent, const Bitzkey& other) const { return regard_for(agent, other) <= -foe_level(agent); }
    static double foe_level(const Bitzkey& agent) { return FOE_LEVEL * (0.5 + 0.5 * (agent.feeling(Empathy) + agent.feeling(Sociability))); }
    static int value_of(const Bitzkey& agent, int possession);  // how good its possession of that kind is, 0 none
    static int better_possession(const Bitzkey& taker, const Bitzkey& owner);  // the kind owner has most better, or -1
    void hand_over(Bitzkey& from, Bitzkey& to, int possession);  // what `to` had of it falls where it stands
    void demand(Bitzkey& agent, Bitzkey& owner, int possession);
    void steal(Bitzkey& agent, Bitzkey& owner, int possession);
    void give(Bitzkey& agent, Bitzkey& friend_, int possession);
    void offer(Bitzkey& agent, Bitzkey& owner, int wanted, int offered);
    double good_worth(const Bitzkey& judge, const Bitzkey& holder, int good) const;  // holder's good, to judge
    double trade_gain(const Bitzkey& judge, const Bitzkey& from, int good) const;  // judge taking from's good
    static bool holds(const Bitzkey& holder, int good);
    // An animal's own misjudgement of a kind of good, -1 to 1, the same every time: a hash of the two.
    static double trade_bias(int identifier, int good) {
        std::uint32_t hash = static_cast<std::uint32_t>(identifier) * 2654435761u ^ static_cast<std::uint32_t>(good + 1) * 2246822519u;
        hash ^= hash >> 15;
        hash *= 2654435761u;
        hash ^= hash >> 13;
        return hash / 4294967295.0 * 2.0 - 1.0;
    }
    void pass_good(Bitzkey& from, Bitzkey& to, int good);
    void begin_fight(const Fight& fight);
    void brawl(Bitzkey& agent, Bitzkey& opponent);  // a turn of the fight: closer, or a blow
    void settle_fights();  // after the turn: new fights begun, and those won, fled or ended, settled  // takes the better of a fresh body's hide and the one it wore
    static int hide_from(const Bitzkey& corpse) { return std::max(static_cast<int>(corpse.species), corpse.hide); }
    static double garb_of(int hide) { return hide == static_cast<int>(Species::Predator) ? PRESTIGE_GARB : hide == static_cast<int>(Species::Omnivore) ? WARRIOR_GARB : 0.0; }
    static double possessions(const Bitzkey& agent);  // its possessions, for envy: its hide, tool and armour
    static double possessions(const Bitzkey& agent, const Bitzkey& judge);  // as the judge, by its desires, weighs them
    void gear_seen(const Bitzkey& user, int kind);  // those who see gear of that kind pay off desire it the more
    void gear_witnessed(const Bitzkey& striker, const Bitzkey& struck);  // a blow seen: the weapon that added to it, the armour that saved
    void answered(Bitzkey& listener, Bitzkey& caller, double mine, double theirs);  // a deed of a listener that touched the caller it heard
    static int ready_sets(const Bitzkey& agent);  // a bit for each set of materials it holds for what it knows how to make
    static double ready_worth(const Bitzkey& agent, int sets);  // the joy of those sets, by their parts
    static void donned(Bitzkey& agent, bool had_weapon, bool had_armour);  // glad of a weapon or armour it lacked
    double envy_of(const Bitzkey& agent) const;  // how envious it is: 0 at or below its kind's envy cut, 1 at the most
    // The drives that envy, taking and giving go by, each its larger of two feelings.
    static double envy_drive(const Bitzkey& agent) { return std::max(agent.feeling(Moodiness), agent.feeling(Dominance)); }
    static double take_drive(const Bitzkey& agent) { return std::max(agent.feeling(Aggression), agent.feeling(Dominance)); }
    static double give_drive(const Bitzkey& agent) { return std::max(agent.feeling(Empathy), agent.feeling(Sociability)); }
    // What one sees of another's garb: its regard won (or, fleeing, lost as a liar's)
    void garb_seen(Bitzkey& seer, const Bitzkey& wearer, double garb, bool lie);
    void mark_seen(Bitzkey& seer, const Bitzkey& bearer, double worth);  // regard toward its worth, or a foe's dread
    void earn_mark(Bitzkey& victor, const Bitzkey& defeated);  // the mark a defeat of it earns, if better than its own
    // Every LAND_TURNS turns: each animal's colour group, the claims on each land square, the wars, and their enmity.
    void count_lands();
    void index_realm_patches();  // each group's squares, from the claims
    void trespass(Bitzkey& agent);  // a turn in another's land: thought the worse of by those of it who see it
    void tally_war(const Bitzkey& killer, const Bitzkey& victim);  // a death in a war
    // The shelters beside the road joined to the one at x, y (squares beside it, diagonals too), each road square
    // reached marked in seen.
    std::vector<std::size_t> road_shelters(int x, int y, std::vector<char>& seen) const;
    void dig(Bitzkey& agent);  // an axe or club finds a stone in the ground, carried or left here
    static double damage(const Bitzkey& attacker, const Bitzkey& defender, bool defending = false);
    // The worth of a blow's gear: what the striker's weapon added is a joy to it, and what the struck one's armour took off a joy to that, each by its share of the defender's health.
    static void gear_pays(Bitzkey& striker, Bitzkey& struck, double blow);
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
    // A band's plants, and its grass as plants: the share of the standing grass a grazer counts on (STANDING_GRASS), as
    // well as this species digests it (GRAZING), in a plant's meals (PLANT_MEALS).
    double band_plant_food(std::size_t band, Species species) const;
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
    // Those of its herd who see it, each with how much it heeds what it sees; and what they take from it.
    std::vector<std::pair<Bitzkey*, double>> witnesses_of(const Bitzkey& actor) const;
    void witnessed_by(const Bitzkey& actor, double outcome, const std::vector<std::pair<Bitzkey*, double>>& seen);
    // Whether an animal counts another, alive or not, as one of its herd (or pack).
    bool society(const Bitzkey& observer, const Bitzkey& actor) const;

private:
    friend struct WorldFile;  // which saves the world whole, and loads it back
    std::pair<std::vector<int>, std::vector<int>> starting_groups(int population);
    std::vector<Cell> grazer_centres_, hunter_centres_;  // where a new world's grazer and hunter groups were centred  // each group's species, and its size
    // Lay out the sea over `ocean` percent of the map, from its own stream of `seed`: smooth noise, the lowest of it sea.
    void make_sea(double ocean, std::uint64_t seed);
    // BIOME_GROUPS starting groups at home in every biome.
    void spawn_biomes(bool plants);  // the plants' groups in every biome, or the animals'
    void pregrow(int turns);         // the plants alone, these turns, then the clock and the records started afresh
    // A square for a group's centre of this terrain, in one of `rows` if given, drawn until one suits it; none if none does.
    std::optional<Cell> group_centre(Terrain terrain, const std::vector<int>& rows);
    // `native` groups have random starting genomes: each carries a tolerance flag only with
    // STARTING_TOLERANCE, and plants take the usual temperature of their centre's zone as their home.
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
    // `call_target`: what its last call (if it chose one) is worth now, which it learns beside its action.
    void learn(Bitzkey& agent, double target, double outcome, bool replay = false, double call_target = 0.0);
    void note_entry(Bitzkey& agent, const Bitzkey::Memory& memory, double vividness);
    // Going further costs: GO_EFFORT of a turn's burn for each cell past the first, however it moves.
    static void effort(Bitzkey& agent, int cells) { agent.hunger += agent.burn * GO_EFFORT * std::max(0, cells - 1); }
    // The nearest grass in sight, but none in a row further from its home temperature than here.
    std::optional<Cell> nearest_grass(const Bitzkey& agent, int sight) const;
    std::optional<Cell> nearest_pile(const Bitzkey& agent, int sight, PileKind kind) const;
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
    void move_away(Bitzkey& agent, Cell threat);
    bool in_bounds(int x, int y) const { return 0 <= x && x < width_ && 0 <= y && y < height_; }
    // A row kept on the map: the top and bottom edges are barriers, though east and west wrap round.
    int within(int y) const { return std::max(0, std::min(height_ - 1, y)); }
    std::vector<Bitzkey*>& cell_at(int x, int y) { claim(x, y); return cells_[static_cast<std::size_t>(y) * width_ + x]; }
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
    std::deque<Gift> gifts_;
    // Calls that helped, since the world began (not saved): by species, call, and what came of it, an escape or a meal.
    std::array<std::array<long long, 4>, 4> followers_lost_{};
    std::array<std::array<std::array<long long, 3>, CALL_COUNT>, 4> call_payoffs_{};  // escapes, meals, and calls understood
    int leaders_ = 0;  // recognised at the last count
    std::deque<Trade> trades_;
    std::unique_ptr<std::mutex> gifts_writing_ = std::make_unique<std::mutex>();  // animals act side by side; held apart so a world still moves
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
    // Last turn's callers, square by square (see index_callers), for the turn they were indexed in.
    struct Heard {
        const Bitzkey* who;
        int reach;  // how far its call carries
    };
    std::vector<Heard> callers_;
    std::vector<int> caller_start_;  // each square's first in callers_, and one past the last square's
    int callers_turn_ = -1;
    void index_callers();
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
    std::vector<double> band_grass_;   // the grass standing in each band, in tufts
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
    std::vector<std::uint8_t> watered_;  // hot land within WATERED_REACH steps of the sea
    std::vector<int> watered_rows_;      // how many such squares each row has
    int watered_reach_ = -1;             // the reach they were found for,
    std::size_t watered_sea_ = 0;        // and the sea's size then: none, until a world's sea is laid or loaded
    void find_watered();
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
    struct NotedFirst { std::size_t order; std::string event; Species species; int who; std::string craft; };
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
        std::vector<Seed> sown;  // seeds planted
        std::vector<std::tuple<const Bitzkey*, double, const Bitzkey*, DeedKind, int, Culture>> deeds_seen;
        std::vector<Fight> fights_begun;  // (doer, worth, done to, what kind, the craft or -1)
        std::vector<std::pair<Bitzkey*, std::vector<Bitzkey*>>> feeding;  // (parent, its young) fed
        std::vector<std::pair<Bitzkey*, Bitzkey*>> teaching;  // (elder, student)
        std::vector<Conception> conceptions;
        std::vector<std::shared_ptr<Brain>> dead_brains;
        int kills = 0, kills_gone = 0, carrion_gone = 0;
        std::array<double, ACTION_COUNT> took{};
        std::array<long long, ACTION_COUNT> taken{};
        double acting = 0;
        std::chrono::steady_clock::time_point clock;  // for timing what its animals do
        // Its ground: the columns from `left` (wrapping east) and the rows from `top`. Its animals touch nothing
        // further than `reach` squares beyond it, so tiles acting at once, twice that and more apart, never meet.
        int left = 0, top = 0, wide = 0, tall = 0, reach = 0, stride = 0, world_wide = 0;
        std::uint64_t seed = 0;  // of its stream of chance, drawn only if one of its animals needs it
        bool seeded = false;
        // How far a square lies beyond its ground, east or west the shorter way round, and north or south.
        int beyond(int x, int y) const {
            const int along = ((x - left) % world_wide + world_wide) % world_wide;
            const int dx = along < wide ? 0 : std::min(along - (wide - 1), world_wide - along);
            const int dy = y < top ? top - y : y >= top + tall ? y - (top + tall - 1) : 0;
            return std::max(dx, dy);
        }
        // What one of its animals did that reaches beyond its ground, to be done once every tile has acted, in the
        // tiles' order; and the call it was answering, if its caller stood too far off to be judged at once.
        struct Later {
            Bitzkey* actor;
            int answer;  // its place in `answers`, or -1
            std::function<void()> deed;
        };
        struct Answer {
            Bitzkey* listener;
            Bitzkey* caller;
            double mine;  // what the listener's act was worth to it, so far
            double theirs;
        };
        std::vector<Later> later;
        std::vector<Answer> answers;
        int answering = -1;  // the answer its acting animal's far deeds count toward
        Bitzkey* actor = nullptr;  // the animal now acting
        std::vector<NotedFirst> firsts;
        std::vector<gpu::BrainEngine::Handle> detached;  // the brains of its dead, to leave the GPUs
        std::deque<Senses> lesson_senses;  // lessons queued for the GPUs as its animals acted
        std::vector<gpu::BrainEngine::Lesson> lessons;
        bool fire_lit = false;
    };
    static thread_local ActingTile* acting_;  // the tile this core is acting for, if any
    // Whether another may be looked at and touched as it now is, by the animal acting: always, when the animals act one
    // by one; when they act in tiles, only one near enough that no other tile's animals can be touching it. One too far
    // is taken as it stood when acting began (was_x, was_y), and what is done to it kept for later (see later()).
    bool in_reach(const Bitzkey& other) const {
        const ActingTile* tile = acting_;
        if (!tile) return true;
        if (tile->beyond(other.was_x, other.was_y) > tile->reach + tile->stride) return false;  // it may be acting in another tile
        return tile->beyond(other.x, other.y) <= tile->reach;
    }
    Cell spot(const Bitzkey& other) const { return in_reach(other) ? Cell{other.x, other.y} : Cell{other.was_x, other.was_y}; }
    // A deed reaching beyond the acting animal's tile, kept to be done when all have acted: true if kept (the caller
    // then leaves it), false if the animals are acting one by one and it is to be done now.
    template <class Deed>
    bool later(Deed&& deed) {
        ActingTile* tile = acting_;
        if (!tile) return false;
        tile->later.push_back({tile->actor, tile->answering, std::forward<Deed>(deed)});
        return true;
    }
    // A check, for the tests: every square an acting animal touches lies within its tile's reach. Counted, not stopped.
    void claim(int x, int y) const {
        if (!guard_claims) return;
        const ActingTile* tile = acting_;
        if (tile && tile->beyond(x, y) > tile->reach) breach(x, y);
    }
    void breach(int x, int y) const;
public:
    static inline bool guard_claims = false;      // count touches beyond a tile's reach (and say where, the first few)
    static inline bool tiles_one_by_one = false;  // act the tiles one at a time, in their order: what side by side must equal
    static inline std::atomic<long long> claim_breaches{0};
    // A number that differs if anything about the world's animals, plants, grass or piles does: to compare two runs.
    std::uint64_t digest() const;
private:
    // Deeds seen while a stretch of the turn is batched, taken in by their witnesses together, each on its own core.
    struct SeenDeed { const Bitzkey* doer; double deed; const Bitzkey* target; DeedKind kind; int craft; Culture culture; };
    std::vector<SeenDeed> seen_deeds_;
    bool batching_deeds_ = false;
    static thread_local std::vector<NotedFirst>* noting_firsts_;  // firsts noted while witnessing, kept for their turn
    static thread_local std::size_t noting_order_;
    void witness_deeds();
    void witness_deed(Bitzkey& other, const SeenDeed& seen);
    // The stream of chance: the acting tile's, else the world's own.
    PyRandom& rng() {
        ActingTile* tile = acting_;
        if (!tile) return random_;
        if (!tile->seeded) {
            tile->rng.seed(tile->seed);
            tile->seeded = true;
        }
        return tile->rng;
    }
    // The turn's choices acted out tile by tile, on every core: returns false, having done nothing, when the world is
    // too small to part, to be acted out one by one instead.
    bool act_in_tiles(const std::vector<Bitzkey*>& agents, const std::vector<int>& choices, std::vector<Sensed>& plans);
    // `each` called for every one of these (by its place among them), tile by tile on every core, those of a tile in
    // their order; then what they did that reached beyond their tiles, in the tiles' order. `reach`: how far beyond its
    // tile one may touch; `stride`: how far one may go. False, having done nothing, when the world is too small to part.
    bool in_tiles(const std::vector<Bitzkey*>& agents, int reach, int stride, const std::function<void(std::size_t)>& each);
    // A death written down: its record and tally, the counts, and the parents' grief.
    void record_death(Bitzkey& agent, std::string_view cause, int killer, Species killer_species, std::string_view activity);
    std::array<std::vector<int>, PILE_KINDS> piles_;  // the portions lying on each square, of each kind
    std::array<std::vector<int>, PILE_KINDS> pile_turns_;  // the turn of each's last drop
    bool stones_scattered_ = false;
    std::vector<int> site_turns_;  // the turn each site was last built on
    std::vector<std::uint8_t> arable_;  // each square, whether it is worked land
    std::vector<int> tended_;  // the turn each worked square was last tended
    void tend(int x, int y) {
        if (arable_at(x, y)) tended_[static_cast<std::size_t>(y) * width_ + x] = turn_;
    }
    std::vector<int> sites_, shelters_;  // each square's units of an unfinished shelter, and whether one stands
    std::vector<Work> works_;  // each square's bow or wagon begun
    std::vector<Wagon> wagons_;  // every wagon made
    std::unordered_multimap<std::size_t, int> parked_;  // the wagons no one drags, by their squares
    std::vector<int> roads_, road_work_;  // whether each square is road, and turns laid on an unfinished one
    std::vector<int> fertilized_;  // worked land fed crushed bone, until a seed sprouts there
    std::map<int, Realm> realms_;  // the colour groups, by identifier
    std::vector<LandClaims> lands_;  // each land square's claims, row by row; empty with no lands
    int land_patch_ = 0, land_columns_ = 0;  // the square's size in cells, and how many squares to a row
    std::vector<War> wars_;
    std::set<std::pair<int, int>> welcome_;  // (owner, guest): groups an owner of land welcomes, as of the last count
    int next_realm_ = 0;
    std::vector<Fight> fights_;  // changed only between turns, so all read the same while they act
    // Each species' cut for envy, taking and giving: the drive at Envy, Take and Give level of its living, ranked each
    // turn; above it, one envies, may take or may give. None above 2, before the first ranking.
    std::array<double, 4> envy_cut_{2, 2, 2, 2}, take_cut_{2, 2, 2, 2}, give_cut_{2, 2, 2, 2};
    double raw_regard_for(const Bitzkey& agent, const Bitzkey& other) const;
    // How it holds another through its parents, at INHERITED_REGARD of how it holds them; none if it knows neither.
    std::optional<Acquaintance> inherited(const Bitzkey& agent, const Bitzkey& other) const;  // as known, no fight in it
    std::vector<int> site_stone_;  // how many of a site's units are stone, the rest wood: what it falls back to
    std::vector<int> fires_, fire_lighters_, cooked_on_;  // cooked_on_: the turn each square's fire was last cooked on  // each square's last fire: when it goes out, and who lit it
    bool any_fires_ = false;
    std::vector<Milestone> diary_;
    int diary_since_ = 0;
    std::array<bool, 3> kinds_seen_{};  // the animals seen alive at the last look, so that one can be seen to die out
    void look_for_firsts();  // those no single act shows: the first elder of a kind, and a kind dying out
    bool any_piles_ = false;  // as last indexed, so that an empty map isn't searched
    std::unique_ptr<gpu::BrainEngine> gpu_;  // the GPUs every brain lives on, when the world thinks there
    std::string gpu_status_;
    std::vector<std::pair<Bitzkey*, Bitzkey*>> teachings_;  // (elder, student) this turn
    std::deque<Senses> lesson_senses_;  // what each queued lesson sensed, kept still while it waits
    std::vector<gpu::BrainEngine::Lesson> lessons_;  // lessons for the GPUs, learnt in one batch
    // What each animal's lessons sensed, when the lessons are queued on every core at once (see think_all): each core
    // queues into the animal's own, and the queues are joined in the animals' order.
    std::vector<std::deque<Senses>> agent_lesson_senses_;
    std::deque<std::deque<Senses>> tile_lesson_senses_;  // and those queued as the tiles acted
    static thread_local std::deque<Senses>* queue_senses_;
    static thread_local std::vector<gpu::BrainEngine::Lesson>* queue_lessons_;
    void queue_lesson(const Bitzkey& agent, const Senses& senses, int action, double target, double rate, bool traced,
                      double decay, int steps = 0);
    void learn_queued();
    void sync(std::span<Bitzkey* const> agents);  // these brains back from the GPUs, each once a turn
    // Put a brain on the GPUs, unless they have failed.
    void attach(Bitzkey& agent);
    // A brain of its own to learn on: its own if nothing else holds it (a group deployed together shares one), else a copy.
    Brain& learning_brain(Bitzkey& agent);
};

}  // namespace bitzkey
