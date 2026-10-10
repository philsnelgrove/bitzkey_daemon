#include "core/Rules.hpp"

#include "core/GpuBrains.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <type_traits>
#include <utility>

#include "core/Brain.hpp"
#include "core/PyFormat.hpp"
#include "core/Simulation.hpp"

namespace bitzkey::rules {

double Rule::clamp(double value) const {
    value = std::max(minimum, std::min(maximum, value));
    return integer ? py_round(value) : value;
}

double Rule::get() const {
    return std::visit([](auto* place) { return static_cast<double>(*place); }, target);
}

void Rule::set(double value) const {
    std::visit([value](auto* place) { *place = static_cast<std::remove_reference_t<decltype(*place)>>(value); }, target);
}

namespace {

std::vector<Rule> make_rules() {
    // Generated from the Python table, to keep the two identical.
    std::vector<Rule> rules = {
        {"GRASS_REGROWTH", "World", "Grass regrowth", "share of the starting grass that grows back each turn", 0.0, 0.1, 0.001, false, &GRASS_REGROWTH},
        {"CORPSE_FERTILITY", "World", "Corpse fertility", "grass grows this many times as fast on and around a corpse", 1, 10, 1, true, &CORPSE_FERTILITY},
        {"CORPSE_TURNS", "World", "Corpse turns", "turns a corpse lasts while its meat rots away", 1, 500, 5, true, &CORPSE_TURNS},
        {"SPAWN_SPREAD", "World", "Spawn spread", "a starting group appears within this many cells of its centre", 0, 4, 1, true, &SPAWN_SPREAD},
        {"USE_GPU", "World", "Think on the GPU", "C++ only: 1 thinks and learns on every GPU found, in floats, with no rewinding; 0 on the CPU, exactly", 0, 1, 1, true, &USE_GPU},
        {"SPAWN_AT_HOME", "World", "Spawn at home", "1: starting and imported groups appear where their home temperature is usual; 0: anywhere", 0, 1, 1, true, &SPAWN_AT_HOME},
        {"HISTORY_TURNS", "World", "History turns", "turns of the world kept whole, to rewind the map to; at the top, all", 1, HISTORY_ALL, 10, true, &HISTORY_TURNS},
        {"HISTORY_MEMORY", "World", "History memory", "most memory, in GB, the rewind history may take", 0.1, 64.0, 0.1, false, &HISTORY_MEMORY},
        {"LIFESPAN_YEARS.0", "Bodies", "Lifespan, shortest", "years of life expected for the smallest bodies, plant or animal", 0.5, 30.0, 0.5, false, &LIFESPAN_YEARS[0]},
        {"LIFESPAN_YEARS.1", "Bodies", "Lifespan, middle", "years of life expected for middling bodies", 0.5, 30.0, 0.5, false, &LIFESPAN_YEARS[1]},
        {"LIFESPAN_YEARS.2", "Bodies", "Lifespan, longest", "years of life expected for the largest bodies", 0.5, 30.0, 0.5, false, &LIFESPAN_YEARS[2]},
        {"AGEING", "Bodies", "Ageing", "from this share of its lifespan, an animal may die of old age", 0.0, 0.95, 0.05, false, &AGEING},
        {"METABOLISM.0", "Bodies", "Metabolism, smallest", "metabolic rate of the smallest bodies", 0.1, 5.0, 0.1, false, &METABOLISM[0]},
        {"METABOLISM.1", "Bodies", "Metabolism, largest", "metabolic rate of the largest bodies", 0.1, 5.0, 0.1, false, &METABOLISM[1]},
        {"STOMACH.0", "Bodies", "Stomach, smallest", "food the smallest body holds, relative to the middle", 0.1, 5.0, 0.1, false, &STOMACH[0]},
        {"STOMACH.1", "Bodies", "Stomach, largest", "food the largest body holds, relative to the middle", 0.1, 5.0, 0.1, false, &STOMACH[1]},
        {"SIZE_IN_MIGHT", "Bodies", "Size in might", "share of strength and toughness that comes from body size", 0.0, 1.0, 0.05, false, &SIZE_IN_MIGHT},
        {"GRAZER_SPEED", "Bodies", "Grazer speed", "grazers move this many times as far a step, rounded half up", 0.5, 4.0, 0.25, false, &GRAZER_SPEED},
        {"WALK_REST", "Bodies", "Walk rest", "share of the turn's burn a one-cell walk saves, as resting does", 0.0, 1.0, 0.05, false, &WALK_REST},
        {"GO_EFFORT", "Bodies", "Go effort", "share of a turn's burn each cell a go_ moves beyond the first costs", 0.0, 2.0, 0.05, false, &GO_EFFORT},
        {"BREED_RESERVE", "Breeding", "Breeding reserve", "hunger a parent must still have spare after paying to breed", 0, 100, 5, true, &BREED_RESERVE},
        {"MATE_RING_DIFFERENCE", "Breeding", "Mate ring difference", "most a partner's ring colour may differ, at no tolerance", 0.0, 1.0, 0.01, false, &MATE_RING_DIFFERENCE},
        {"MATE_DOT_DIFFERENCE", "Breeding", "Mate dot difference", "most a partner's dot colour may differ, at no tolerance", 0.0, 1.0, 0.01, false, &MATE_DOT_DIFFERENCE},
        {"LITTER_RANGE.0", "Breeding", "Smallest litter", "fewest young per birth, while there is room", 1, 20, 1, true, &LITTER_RANGE[0]},
        {"LITTER_RANGE.1", "Breeding", "Largest litter", "most young per birth, while there is room", 1, 20, 1, true, &LITTER_RANGE[1]},
        {"LITTER_SIZE_WEIGHT", "Breeding", "Small-body litters", "share of litter size from small bodies; the rest from the trait", 0.0, 1.0, 0.05, false, &LITTER_SIZE_WEIGHT},
        {"SMALL_LITTERS", "Breeding", "Small-body litter boost", "the smallest bodies' litters are this many times the usual, falling to the usual for the largest", 1.0, 4.0, 0.25, false, &SMALL_LITTERS},
        {"GESTATION.0", "Breeding", "Shortest gestation", "turns the smallest animals carry their young", 1, 500, 1, true, &GESTATION[0]},
        {"GESTATION.1", "Breeding", "Longest gestation", "turns the largest animals carry their young", 1, 500, 1, true, &GESTATION[1]},
        {"BIRTH_REACH", "Breeding", "Birth reach", "young are born on free cells up to this many cells from their mother", 1, 5, 1, true, &BIRTH_REACH},
        {"PLANT_GESTATION", "Breeding", "Plant gestation", "turns a seed lies in the ground before it can sprout", 0, 500, 1, true, &PLANT_GESTATION},
        {"CROWDING_RADIUS", "Breeding", "Crowding radius", "animals within this many cells crowd a would-be parent", 1, 12, 1, true, &CROWDING_RADIUS},
        {"CROWDING_LIMIT", "Breeding", "Crowding limit", "litters shrink as crowding nears this; at it, no breeding", 1, 100, 1, true, &CROWDING_LIMIT},
        {"FORAGE_RADIUS", "Breeding", "Forage radius", "food and mouths are weighed within this many cells", 1, 12, 1, true, &FORAGE_RADIUS},
        {"FOOD_NEEDED", "Breeding", "Food needed", "meals each mouth needs nearby to breed; twice this for a full litter", 0.5, 20.0, 0.5, false, &FOOD_NEEDED},
        {"PLANT_MEALS", "Breeding", "Plant meals", "meals a plant is worth to a full-efficiency browser", 0.0, 20.0, 0.5, false, &PLANT_MEALS},
        {"PREY_MEALS", "Breeding", "Prey meals", "meals a prey animal is worth", 0.0, 20.0, 0.5, false, &PREY_MEALS},
        {"SHARED_DIET", "Breeding", "Shared diet", "how much another species eating the same food counts as a mouth", 0.0, 1.0, 0.05, false, &SHARED_DIET},
        {"GRASS_OUTLOOK", "Breeding", "Grass outlook", "grass is judged by what regrows in this many turns", 0, 1000, 10, true, &GRASS_OUTLOOK},
        {"STANDING_GRASS", "Breeding", "Standing grass", "plus this share of the grass standing now", 0.0, 1.0, 0.05, false, &STANDING_GRASS},
        {"DEATH_PENALTY", "Feelings", "Death penalty", "how sharply a brain learns from the choice that killed it", 0.0, 10.0, 0.1, false, &DEATH_PENALTY},
        {"BREED_JOY", "Feelings", "Breeding joy", "pleasure of breeding, scaled by libido", 0.0, 5.0, 0.05, false, &BREED_JOY},
        {"FIGHT_JOY", "Feelings", "Fighting joy", "pleasure of a blow, scaled by aggression", 0.0, 5.0, 0.05, false, &FIGHT_JOY},
        {"FEAST_JOY", "Feelings", "Feasting joy", "a predator's pleasure in feasting on a fresh kill", 0.0, 5.0, 0.05, false, &FEAST_JOY},
        {"ATTACK_JOY", "Feelings", "Attacking joy", "a predator's pleasure in a blow it lives through", 0.0, 5.0, 0.05, false, &ATTACK_JOY},
        {"COMPANY_JOY", "Feelings", "Company joy", "pleasure of company, scaled by sociability", 0.0, 5.0, 0.05, false, &COMPANY_JOY},
        {"CURIOSITY_JOY", "Feelings", "Curiosity joy", "pleasure of setting off in a direction, scaled by curiosity", 0.0, 5.0, 0.05, false, &CURIOSITY_JOY},
        {"FEAR_DREAD", "Feelings", "Fear dread", "dread of hunters nearby, scaled by fear", 0.0, 5.0, 0.05, false, &FEAR_DREAD},
        {"UNEASE", "Feelings", "Unease", "how much an intolerant animal minds other species close by", 0.0, 5.0, 0.05, false, &UNEASE},
        {"CLIMATE_UNEASE", "Feelings", "Climate unease", "discomfort two steps from home, half that one step; less if hardy", 0.0, 5.0, 0.05, false, &CLIMATE_UNEASE},
        {"EMPATHY_PAIN", "Feelings", "Empathy pain", "how much an empathetic animal feels another's wound", 0.0, 5.0, 0.05, false, &EMPATHY_PAIN},
        {"OTHER_KIND", "Feelings", "Other kind", "how much of that pain reaches it for another species", 0.0, 1.0, 0.05, false, &OTHER_KIND},
        {"SOCIAL_LEARNING", "Society", "Social learning", "how much a herd-mate's success or failure seen teaches, by empathy and intellect", 0.0, 3.0, 0.05, false, &SOCIAL_LEARNING},
        {"SOCIAL_MOOD", "Society", "Social mood", "how much a herd-mate's success or failure seen moves the mood", 0.0, 3.0, 0.05, false, &SOCIAL_MOOD},
        {"SOCIAL_MEMORY", "Society", "Social memory", "share of a seen success or failure still sensed a turn later", 0.0, 1.0, 0.05, false, &SOCIAL_MEMORY},
        {"LEARNING_RATE", "Mind", "Learning rate", "how fast a brain learns, at the highest intellect", 0.0, 0.5, 0.005, false, &LEARNING_RATE},
        {"TRACE_DECAY", "Mind", "Trace decay", "how far back a choice's outcome reaches earlier choices (lambda)", 0.0, 1.0, 0.05, false, &TRACE_DECAY},
        {"SMART_TRACE_DECAY", "Mind", "Trace decay, smartest", "trace decay at the highest intellect; the lowest keeps the one above", 0.0, 1.0, 0.01, false, &SMART_TRACE_DECAY},
        {"LEARNING_BOOST", "Mind", "Learning boost", "the highest intellect learns this much faster again; the lowest, no faster", 0.0, 5.0, 0.1, false, &LEARNING_BOOST},
        {"REPLAY_PER_INTELLECT", "Mind", "Replays per intellect", "recent experiences re-learnt each turn per point of intellect", 0, 4, 1, true, &REPLAY_PER_INTELLECT},
        {"PREDATOR_LEARNING", "Mind", "Predator learning", "times as fast a predator's brain learns from its own choices; an omnivore gets half the bonus", 1.0, 5.0, 0.1, false, &PREDATOR_LEARNING},
        {"TEMPERATURE", "Mind", "Choice temperature", "how loosely a brain picks among its options", 0.01, 5.0, 0.05, false, &TEMPERATURE},
        {"MUTATION", "Mind", "Brain mutation", "how much a newborn's brain varies from its parents'", 0.0, 1.0, 0.01, false, &MUTATION},
        {"HEADING_RELUCTANCE", "Mind", "Heading reluctance", "how much less a new brain values setting off in a direction", 0.0, 2.0, 0.05, false, &HEADING_RELUCTANCE},
        {"TEMPERAMENT_RELAPSE", "Mind", "Temperament relapse", "share of the gap back to the inborn value closed each turn", 0.0, 0.1, 0.001, false, &TEMPERAMENT_RELAPSE},
        {"PANIC.0", "Mind", "Panic, unhurt", "chance a fully fearful animal breaks when struck, unhurt", 0.0, 1.0, 0.05, false, &PANIC[0]},
        {"PANIC.1", "Mind", "Panic, near death", "chance a fully fearful animal breaks when struck, near death", 0.0, 1.0, 0.05, false, &PANIC[1]},
        {"HUNTING_INSTINCT", "Mind", "Hunting instinct", "how much more a predator values hunting and attacking, times its hunger", 0.0,
         5.0, 0.05, false, &HUNTING_INSTINCT},
        {"PRIMAL_FEAR", "Mind", "Primal fear", "chance a turn of being overcome by an overwhelmingly stronger hunter in sight", 0.0, 1.0,
         0.05, false, &PRIMAL_FEAR},
        {"PRIMAL_FEAR_PREDATORS", "Mind", "Primal fear, predators", "how susceptible predators are, as a share", 0.0, 1.0, 0.05, false,
         &PRIMAL_FEAR_PREDATORS},
        {"PREDATOR_LOOMS", "Mind", "Predators loom", "steps larger and stronger a predator counts as, sizing up a fight for primal fear", 0,
         7, 1, true, &PREDATOR_LOOMS},
        {"PREDATOR_INTELLIGENCE", "Mind", "Predator intelligence", "times over a predator's intellect counts, up to the full range", 1.0,
         5.0, 0.1, false, &PREDATOR_INTELLIGENCE},
        {"PREDATOR_FEAR", "Mind", "Predator fear", "share of fear's effects a predator feels: dread, panic, keeping to its herd", 0.0, 1.0,
         0.05, false, &PREDATOR_FEAR},
        {"PRIMAL_FREEZE", "Mind", "Primal freeze", "chance an overcome animal freezes rather than flees, at the lowest intellect", 0.0, 1.0,
         0.05, false, &PRIMAL_FREEZE},
        {"STRIKE_FORCE", "Fighting", "Strike force", "scales every blow", 0.0, 10.0, 0.1, false, &STRIKE_FORCE},
        {"PREDATOR_SIGHT", "Fighting", "Predator sight", "cells further a predator sees prey", 0, MAX_PREDATOR_SIGHT, 1, true, &PREDATOR_SIGHT},
        {"HUNT_STEPS", "Fighting", "Hunt steps", "steps further a predator chases prey a turn", 0, 5, 1, true, &HUNT_STEPS},
        {"PREDATOR_WEAPONS", "Fighting", "Predator weapons", "a predator's natural weapons count this many times over", 0.0, 10.0, 0.1, false,
         &PREDATOR_WEAPONS},
        {"GRAZER_DEFENCE_STRENGTH", "Fighting", "Grazer defence strength", "a grazer can strike back only with strength above this", 0, 7,
         1, true, &GRAZER_DEFENCE_STRENGTH},
        {"STRIKE_EFFORT", "Fighting", "Strike effort", "food burned for each point of damage a blow deals", 0.0, 2.0, 0.05, false, &STRIKE_EFFORT},
        {"THORN_FORCE", "Fighting", "Thorn force", "a plant's counterblow at full weapons and size", 0, 100, 1, true, &THORN_FORCE},
        {"THORN_SIZE_SHARE", "Fighting", "Thorn size share", "share of that from the plant's size; the rest its weapons", 0.0, 1.0, 0.05, false, &THORN_SIZE_SHARE},
        {"PACK_BONUS", "Fighting", "Pack bonus", "extra force at prey that a herd-mate is also alongside", 0.0, 5.0, 0.05, false, &PACK_BONUS},
        {"KILL_MEAL", "Fighting", "Kill meal", "hunger a kill sates", 0, 200, 5, true, &KILL_MEAL},
        {"KILL_MEAL_FOR_SIZE", "Fighting", "Kill meal for size", "more hunger sated, up to this for the largest prey", 0, 200, 5, true, &KILL_MEAL_FOR_SIZE},
        {"FRESH_KILL_TURNS", "Fighting", "Fresh kill turns", "turns after the one it died in that a body is a fresh kill", 0, 10, 1, true, &FRESH_KILL_TURNS},
        {"FRESH_KILL_MEALS", "Fighting", "Fresh kill meals", "times the kill meal a predator or omnivore gets eating a fresh kill", 0.0, 10.0, 0.5, false, &FRESH_KILL_MEALS},
        {"KIND_DIFFERENCE", "Fighting", "Kind difference", "predators whose rings differ no more are one kind, to hunt and breed with", 0.0,
         1.0, 0.01, false, &KIND_DIFFERENCE},
        {"FLEE_RANGE", "Fighting", "Flee range", "a hunter is only worth fleeing within this many steps, unless alarmed", 0, 12, 1, true, &FLEE_RANGE},
        {"HERD_VIGILANCE", "Fighting", "Herd vigilance", "how far a close herd shrinks that range", 0.0, 1.0, 0.05, false, &HERD_VIGILANCE},
        {"SWITCH_SHARE", "Fighting", "Prey switching", "a prey is hunted while it is at least this share of the food", 0.0, 1.0, 0.05, false, &SWITCH_SHARE},
        {"TERRITORY_RADIUS", "Territory", "Territory radius", "a hunter's territory: the cells within this many steps", 1, 12, 1, true, &TERRITORY_RADIUS},
        {"TERRITORY_RAMP", "Territory", "Territory ramp", "rivals past the limit that bring rivalry to its full strength", 1, 20, 1, true, &TERRITORY_RAMP},
        {"RIVAL_UNEASE", "Territory", "Rival unease", "discomfort of a fully overcrowded territory", 0.0, 5.0, 0.05, false, &RIVAL_UNEASE},
        {"RIVAL_KILL_JOY", "Territory", "Rival kill joy", "relief at killing a rival in an overcrowded territory", 0.0, 5.0, 0.05, false, &RIVAL_KILL_JOY},
        {"TERRITORY.predator.fewest", "Territory", "Predator rivals, cruel", "rivals the least empathetic predator tolerates", 0, 50, 1, true, &TERRITORY[0].fewest},
        {"TERRITORY.predator.most", "Territory", "Predator rivals, kind", "rivals the most empathetic predator tolerates", 0, 50, 1, true, &TERRITORY[0].most},
        {"TERRITORY.predator.strength", "Territory", "Predator rivalry", "how strongly overcrowding moves a predator", 0.0, 2.0, 0.05, false, &TERRITORY[0].strength},
        {"TERRITORY.omnivore.fewest", "Territory", "Omnivore rivals, cruel", "rivals the least empathetic omnivore tolerates", 0, 50, 1, true, &TERRITORY[1].fewest},
        {"TERRITORY.omnivore.most", "Territory", "Omnivore rivals, kind", "rivals the most empathetic omnivore tolerates", 0, 50, 1, true, &TERRITORY[1].most},
        {"TERRITORY.omnivore.strength", "Territory", "Omnivore rivalry", "how strongly overcrowding moves an omnivore", 0.0, 2.0, 0.05, false, &TERRITORY[1].strength},
        {"HERD_RADIUS", "Herding", "Herd radius", "herd-mates are sought within this many steps", 1, 12, 1, true, &HERD_RADIUS},
        {"HERD_SPREAD", "Herding", "Herd spread", "how far from the herd's middle a fearless animal is content", 0, 12, 1, true, &HERD_SPREAD},
        {"HERD_JOY", "Herding", "Herd joy", "pleasure of being among three or more herd-mates", 0.0, 5.0, 0.05, false, &HERD_JOY},
        {"HERD_LONELINESS", "Herding", "Herd loneliness", "unease at having no herd-mate in range", 0.0, 5.0, 0.05, false, &HERD_LONELINESS},
        {"HERD_SAFETY", "Herding", "Herd safety", "how much three close herd-mates calm fear", 0.0, 1.0, 0.05, false, &HERD_SAFETY},
        {"HERD_DEFEND_JOY", "Herding", "Herd defence joy", "pleasure of striking back at a herd-mate's attacker", 0.0, 5.0, 0.05, false, &HERD_DEFEND_JOY},
        {"GRASS_MEAL", "Food", "Grass meal", "hunger a tuft of grass sates", 0, 200, 5, true, &GRASS_MEAL},
        {"PLANT_MEAL", "Food", "Plant meal", "hunger a bite of a plant sates", 0, 200, 5, true, &PLANT_MEAL},
        {"PLANT_MEAL_FOR_SIZE", "Food", "Plant meal for size", "more hunger sated, up to this for the largest plants", 0, 200, 5, true, &PLANT_MEAL_FOR_SIZE},
        {"PLANT_BITE", "Food", "Plant bite", "health a plant loses to each bite", 0, 200, 1, true, &PLANT_BITE},
        {"PLANT_REGROWTH", "Food", "Plant regrowth", "share of its health a plant regrows each turn", 0.0, 1.0, 0.005, false, &PLANT_REGROWTH},
        {"PLANT_RECOVERY", "Food", "Plant recovery", "turns after a bite before a plant can be bitten again", 0, 100, 1, true, &PLANT_RECOVERY},
        {"UPROOT_HUNGER", "Food", "Uproot hunger", "a grazer this hungry, with food scarce, may uproot a plant", 0, 100, 1, true, &UPROOT_HUNGER},
        {"UPROOT_MEALS", "Food", "Uproot meals", "times a bite's meal an uprooting grazer eats", 0.0, 10.0, 0.1, false, &UPROOT_MEALS},
        {"UPROOT_BITE", "Food", "Uproot bite", "health a plant loses to being uprooted", 0, 200, 1, true, &UPROOT_BITE},
        {"CARRION_MEAT", "Food", "Carrion meat", "meat a well-fed corpse leaves", 0, 200, 5, true, &CARRION_MEAT},
        {"CARRION_MEAT_FOR_SIZE", "Food", "Carrion meat for size", "more meat, up to this for the largest", 0, 200, 5, true, &CARRION_MEAT_FOR_SIZE},
        {"GRAZING.grazer", "Food", "Grazer on grass", "share of the food in grass a grazer gets", 0.0, 3.0, 0.05, false, &GRAZING[1]},
        {"GRAZING.omnivore", "Food", "Omnivore on grass", "share of the food in grass an omnivore gets", 0.0, 3.0, 0.05, false, &GRAZING[2]},
        {"BROWSING.grazer", "Food", "Grazer on plants", "share of the food in plants a grazer gets", 0.0, 3.0, 0.05, false, &BROWSING[1]},
        {"BROWSING.omnivore", "Food", "Omnivore on plants", "share of the food in plants an omnivore gets", 0.0, 3.0, 0.05, false, &BROWSING[2]},
        {"SEED_CHANCE.0", "Plants", "Seeding, least fertile", "a well-fed plant's chance each turn of seeding", 0.0, 1.0, 0.01, false, &SEED_CHANCE[0]},
        {"SEED_CHANCE.1", "Plants", "Seeding, most fertile", "a well-fed plant's chance each turn of seeding", 0.0, 1.0, 0.01, false, &SEED_CHANCE[1]},
        {"SEED_COST", "Plants", "Seed cost", "hunger seeding costs a plant", 0, 100, 1, true, &SEED_COST},
        {"SEED_HUNGER", "Plants", "Seed hunger", "a plant hungrier than this doesn't seed", 0, 100, 1, true, &SEED_HUNGER},
        {"SEEDLING_HUNGER", "Plants", "Seedling hunger", "hunger a seedling starts with", 0, 100, 1, true, &SEEDLING_HUNGER},
        {"SEED_REACH", "Plants", "Seed reach", "how far, in cells, a seed may land from its plant", 1, 9, 1, true, &SEED_REACH},
        {"LIGHT_RADIUS", "Plants", "Light radius", "plants compete for light with plants within this many cells", 0, 12, 1, true, &LIGHT_RADIUS},
        {"LIGHT_LIMIT", "Plants", "Light limit", "this many such plants stop a plant seeding", 1, 50, 1, true, &LIGHT_LIMIT},
        {"PLANT_CROWDING", "Plants", "Plant crowding", "a seed doesn't take where more plants grow within two cells", 0, 50, 1, true, &PLANT_CROWDING},
        {"ARCTIC_SHARE", "Climate", "Arctic", "share of the map's height that is arctic, at the top and again at the bottom", 0.0, 0.5, 0.05, false, &ARCTIC_SHARE},
        {"EQUATOR_SHARE", "Climate", "Equator", "share of the map's height across its middle that is the equator", 0.0, 1.0, 0.05, false, &EQUATOR_SHARE},
        {"CLIMATE_HUNGER", "Climate", "Climate hunger", "times the food needed a step or more from home (a plant, only too hot)", 1.0, 5.0, 0.1, false, &CLIMATE_HUNGER},
        {"CLIMATE_DAMAGE", "Climate", "Climate damage", "share of full health lost a turn for each step from home (plants from two)", 0.0, 1.0, 0.01, false, &CLIMATE_DAMAGE},
        {"PLANT_CLIMATE_YIELD", "Climate", "Plant climate yield", "share of its food a plant gives out of its temperature", 0.0, 1.0, 0.05, false, &PLANT_CLIMATE_YIELD},
        {"SEASON_TURNING", "Climate", "Season turning", "share of each season its wave takes to wash across a hemisphere", 0.0, 1.0, 0.05, false, &SEASON_TURNING},
        {"WAVE_BAND", "Climate", "Wave band", "rows in each band of a season's wave", 1, 20, 1, true, &WAVE_BAND},
        {"WAVE_FRONT", "Climate", "Wave front", "bands at a wave's front, part way between the old weather and the new", 1, 30, 1, true, &WAVE_FRONT},
        {"GRASS_DIEBACK", "Climate", "Grass dieback", "where it isn't medium, turns for each tuft of a cell's grass to die", 1, 500, 1, true, &GRASS_DIEBACK},
        {"SEED_VIABILITY", "Climate", "Seed viability", "years a seed waits for its temperature before it dies", 0.0, 5.0, 0.05, false, &SEED_VIABILITY},
        {"STARTING_TOLERANCE", "Climate", "Starting tolerance", "chance a random starting genome carries each tolerance flag", 0.0, 1.0, 0.05, false, &STARTING_TOLERANCE},
        {"HARDINESS", "Climate", "Hardiness", "share of exposure's extra hunger and damage an animal tolerant of it suffers", 0.0, 1.0, 0.05, false, &HARDINESS},
    };
    for (std::size_t event = 0; event < EXPERIENCES.size(); ++event) {
        std::string name(EXPERIENCE_NAMES[event]);
        std::replace(name.begin(), name.end(), '_', ' ');
        std::string title = name;
        title[0] = static_cast<char>(title[0] - 'a' + 'A');
        for (Shift& shift : EXPERIENCES[event]) {
            const std::string trait(MIND_TRAITS[static_cast<std::size_t>(shift.trait)]);
            rules.push_back({"EXPERIENCES." + std::string(EXPERIENCE_NAMES[event]) + "." + trait, "Experiences",
                             title + ": " + trait, "how " + name + " shifts " + trait + ", as a share of its range", -0.5,
                             0.5, 0.0005, false, &shift.change});
        }
    }
    return rules;
}

// Pairs that must stay in order, smaller first, and pairs that must differ (lifespan divides by the gap).
constexpr std::pair<std::string_view, std::string_view> ORDERED[] = {
    {"LITTER_RANGE.0", "LITTER_RANGE.1"}, {"GESTATION.0", "GESTATION.1"}, {"LIFESPAN_YEARS.0", "LIFESPAN_YEARS.1"}, {"LIFESPAN_YEARS.1", "LIFESPAN_YEARS.2"}};
constexpr std::pair<std::string_view, std::string_view> DISTINCT[] = {{"METABOLISM.0", "METABOLISM.1"}};

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

}  // namespace

std::string locked(std::string_view key) {
    if (key == "USE_GPU" && !gpu::available()) return "No GPU with Vulkan was found on this computer, so the brains think on the CPU.";
    return {};
}

std::string_view warning(std::string_view key) {
    if (key == "HISTORY_TURNS") return "MEMORY: each turn kept holds the whole world, about 200 KB a brain (some 20 MB a turn with 100 animals), up to the history memory limit";
    if (key == "HISTORY_MEMORY") return "MEMORY: keep this well below your computer's free memory, or Bitzkey will slow to a crawl or crash";
    return {};
}

const std::vector<Rule>& all() {
    static const std::vector<Rule> rules = make_rules();
    return rules;
}

const Rule* find(std::string_view key) {
    const auto& rules = all();
    const auto found = std::find_if(rules.begin(), rules.end(), [key](const Rule& rule) { return rule.key == key; });
    return found == rules.end() ? nullptr : &*found;
}

const Values& defaults() {
    static const Values values = current();
    return values;
}

Values current() {
    Values values;
    for (const Rule& rule : all()) values[rule.key] = rule.get();
    return values;
}

std::vector<std::string> problems(const Values& values) {
    std::vector<std::string> found;
    for (const auto& [low, high] : ORDERED) {
        if (values.find(low)->second > values.find(high)->second) {
            found.push_back(find(low)->label + " must not exceed " + lower(find(high)->label));
        }
    }
    for (const auto& [first, second] : DISTINCT) {
        if (values.find(first)->second == values.find(second)->second) {
            found.push_back(find(first)->label + " must differ from " + lower(find(second)->label));
        }
    }
    if (2 * values.find("ARCTIC_SHARE")->second + values.find("EQUATOR_SHARE")->second > 1 + 1e-9) {
        found.push_back("The two arctic zones and the equator must fit in the map together");
    }
    return found;
}

void apply(const Values& values) {
    const Values& start = defaults();
    for (const Rule& rule : all()) {
        const auto given = values.find(rule.key);
        rule.set(rule.clamp(given != values.end() ? given->second : start.find(rule.key)->second));
    }
}

Values changed(const Values& values) {
    Values result;
    for (const auto& [key, value] : values) {
        if (value != defaults().find(key)->second) result[key] = value;
    }
    return result;
}

std::string shown(const Rule& rule, double value) {
    if (rule.key == "HISTORY_TURNS" && value >= rule.maximum) return "ALL";  // its top keeps every turn
    if (rule.integer) return std::to_string(static_cast<long long>(value));
    char text[32];
    std::snprintf(text, sizeof text, "%.6g", value);
    return text;
}

}  // namespace bitzkey::rules
