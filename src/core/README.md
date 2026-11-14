# Rules

Bitzkeys began as an arena of sorts.  It was a test to see who would be the last colour standing.  The problem we encountered was, that omnivores always won.
So, we started implementing rules... some of them are natural laws, and some of them are instinct.  The goal of these rules was to bring balance to the world.  The first such rules nerfed omnivores by buffing the food gathering of grazers and predators.
The coming years brought numerous tweaks, balances and special case considerations.  What follows below, is a complete list of every rule and it's definition.  These are all configurable in the sim client.

There are 472 rules in 13 sections. Each gives its default, the range it may be set within and the step it moves by. Rules declared together in the code, because they shape one behaviour, are listed as numbered subsections of the first of them; where such a rule sits in another section, the connection is noted beneath it.

## Contents

1. [World](#1-world) (18 rules)
2. [Bodies](#2-bodies) (36 rules)
3. [Breeding](#3-breeding) (33 rules)
4. [Feelings](#4-feelings) (25 rules)
5. [Society](#5-society) (17 rules)
6. [Mind](#6-mind) (156 rules)
7. [Food](#7-food) (83 rules)
8. [Fighting](#8-fighting) (30 rules)
9. [Territory](#9-territory) (11 rules)
10. [Herding](#10-herding) (9 rules)
11. [Plants](#11-plants) (10 rules)
12. [Climate](#12-climate) (17 rules)
13. [Experiences](#13-experiences) (27 rules)

## 1. World

### 1.1 Grass regrowth (`GRASS_REGROWTH`)

Share of the starting grass that grows back each turn.

Default **0.032**; from 0 to 0.1, in steps of 0.001.

### 1.2 Corpse fertility (`CORPSE_FERTILITY`)

Grass grows this many times as fast on and around a corpse.

Default **2**; from 1 to 10, in steps of 1.

#### 1.2.1 Watered land (`WATERED_REACH`)

Steps from water within which hot land grows grass and can be tilled; 0: none.

Default **7**; from 0 to 20, in steps of 1.

### 1.3 Off-hours sight (`OFF_HOURS_SIGHT`)

How far an animal awake out of its own hours sees, in the dark if active by day, in the light if active by night, as a share of its sight; calls carry as far as ever.

Default **0.5**; from 0.1 to 1, in steps of 0.05.

#### 1.3.1 Corpse turns (`CORPSE_TURNS`)

Turns a corpse lasts while its meat rots away.

Default **90**; from 1 to 1500, in steps of 5.

#### 1.3.2 Corpse plant (`CORPSE_PLANT`)

Chance an animal's body that rots away entirely leaves the seed of a new plant.

Default **0.1**; from 0 to 1, in steps of 0.05.

### 1.4 Square space (`CELL_SPACE`)

Spaces in a square; a Bitzkey takes 1 + its size, and none is born, moves or spawns into a square without room.

Default **10**; from 1 to 80, in steps of 1.

### 1.5 Spawn spread (`SPAWN_SPREAD`)

A starting group appears within this many cells of its centre.

Default **2**; from 0 to 4, in steps of 1.

### 1.6 Think on the GPU (`USE_GPU`)

C++ only: 1 thinks and learns on every GPU found, in floats; 0 on the CPU, exactly.

Default **0**; from 0 to 1, in steps of 1.

#### 1.6.1 Spawn at home (`SPAWN_AT_HOME`)

1: starting and imported groups appear where their home temperature is usual; 0: anywhere.

Default **1**; from 0 to 1, in steps of 1.

### 1.7 Day length (`DAY_LENGTH`)

Turns in a day; night covers half the map and moves round it once a day.

Default **24**; from 2 to 100, in steps of 1.

#### 1.7.1 Sleep (`SLEEP`)

1: an animal rests through its night (the dark if diurnal, the light if nocturnal) unless startled awake; 0: never forced to.

Default **1**; from 0 to 1, in steps of 1.

### 1.8 Biome groups (`BIOME_GROUPS`)

A world started with any Bitzkeys has this many groups of each species native to every biome, each stretch of land or sea in a climate zone, on top of its population; 0: none.

Default **1**; from 0 to 10, in steps of 1.

### 1.9 Biome size (`BIOME_SIZE`)

The fewest squares a stretch of land or sea in a climate zone needs to count as a biome.

Default **25**; from 1 to 1000, in steps of 5.

### 1.10 Graph turns (`GRAPH_TURNS`)

Turns of the past the graph and death report cover; deaths and genomes older than that are forgotten.

Default **250000**; from 1000 to 2000000, in steps of 1000.

### 1.11 Grazer group (`GRAZER_SPAWN_GROUP`)

Grazers in each group a new world starts with (other species start in groups of 3 to 5).

Default **30**; from 1 to 500, in steps of 5.

#### 1.11.1 Hunters apart (`HUNTERS_APART`)

Squares a new world's hunters start from its grazers, at the least, where there is room; 0: anywhere.

Default **15**; from 0 to 60, in steps of 1.

#### 1.11.2 Plant head start (`PREGROWTH_TURNS`)

Turns a new world's plants grow alone, uncounted, before its animals spawn; 0: none.

Default **2000**; from 0 to 20000, in steps of 100.

## 2. Bodies

### 2.1 Lifespan, shortest (`LIFESPAN_YEARS.0`)

Years of life expected for the smallest bodies, plant or animal.

Default **3**; from 0.5 to 30, in steps of 0.5.

#### 2.1.1 Lifespan, middle (`LIFESPAN_YEARS.1`)

Years of life expected for middling bodies.

Default **15**; from 0.5 to 30, in steps of 0.5.

#### 2.1.2 Lifespan, longest (`LIFESPAN_YEARS.2`)

Years of life expected for the largest bodies.

Default **30**; from 0.5 to 30, in steps of 0.5.

#### 2.1.3 Metabolism, smallest (`METABOLISM.0`)

Metabolic rate of the smallest bodies.

Default **1.6**; from 0.1 to 5, in steps of 0.1.

#### 2.1.4 Metabolism, largest (`METABOLISM.1`)

Metabolic rate of the largest bodies.

Default **0.6**; from 0.1 to 5, in steps of 0.1.

#### 2.1.5 Stomach, smallest (`STOMACH.0`)

Food the smallest body holds, relative to the middle.

Default **0.5**; from 0.1 to 5, in steps of 0.1.

#### 2.1.6 Stomach, largest (`STOMACH.1`)

Food the largest body holds, relative to the middle.

Default **2**; from 0.1 to 5, in steps of 0.1.

### 2.2 Starving damage (`STARVING_DAMAGE`)

Share of its full health a Bitzkey at full hunger loses each turn.

Default **0.1**; from 0 to 1, in steps of 0.01.

### 2.3 Meat digestion (`MEAT_DIGESTION`)

The share of its fullness that is meat empties at this share of the usual rate (1/7: meat lasts a week to plants' day).

Default **0.142857**; from 0.01 to 1, in steps of 0.001.

### 2.4 Ageing (`AGEING`)

From this share of its lifespan, an animal may die of old age, and is feeble.

Default **0.7**; from 0 to 0.95, in steps of 0.05.

#### 2.4.1 Size in might (`SIZE_IN_MIGHT`)

Share of strength and toughness that comes from body size.

Default **0.5**; from 0 to 1, in steps of 0.05.

### 2.5 Size hunger (`SIZE_HUNGER`)

A large body burns 1 + this times its size squared as much food (size a share of its range: twice as much at the largest).

Default **1**; from 0 to 5, in steps of 0.1.

#### 2.5.1 Size starving (`SIZE_STARVING`)

Starving, a body wastes away 1 + this times its size as fast (twice as fast at the largest).

Default **1**; from 0 to 5, in steps of 0.1.

#### 2.5.2 Small hiding (`SMALL_HIDING`)

A hunter's blow misses a small animal outright with this times its smallness squared as a chance (none at the largest).

Default **0.4**; from 0 to 1, in steps of 0.05.

#### 2.5.3 Small maturity (`SMALL_MATURITY`)

The smallest are infants this share as long as their lifespans would make them, the larger more, the largest the whole.

Default **0.1**; from 0.01 to 1, in steps of 0.01.

### 2.6 Feeble youth (`FEEBLE_YOUTH`)

An animal is feeble, an infant, for this share of its lifespan (1/30: 36 days for the smallest).

Default **0.0333333**; from 0 to 0.5, in steps of 0.001.

#### 2.6.1 Predator infancy (`PREDATOR_INFANCY`)

A predator is an infant this share as long as other animals.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 2.6.2 Grazer infancy (`GRAZER_INFANCY`)

A grazer is an infant this share as long as other animals.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 2.6.3 Omnivore infancy (`OMNIVORE_INFANCY`)

An omnivore is an infant this share as long as other animals.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 2.6.4 Grazer gestation (`GRAZER_GESTATION`)

A grazer carries its young this share as long as its size would have it.

Default **0.5**; from 0.05 to 1, in steps of 0.05.

#### 2.6.5 Feeble share (`FEEBLE_SHARE`)

A feeble animal's body traits and health are at this share of its own.

Default **0.5**; from 0.1 to 1, in steps of 0.05.

### 2.7 Grazer speed (`GRAZER_SPEED`)

Grazers move this many times as far a step, rounded half up.

Default **1.5**; from 0.5 to 4, in steps of 0.25.

### 2.8 Avian body (`AVIAN_BODY`)

An avian animal's size, weapons and armour count this share of its genes'.

Default **0.5**; from 0.1 to 1, in steps of 0.05.

#### 2.8.1 Flight hunger (`FLIGHT_HUNGER`)

An avian animal burns this share more food.

Default **0.25**; from 0 to 2, in steps of 0.05.

#### 2.8.2 Amphibious speed (`AMPHIBIOUS_SPEED`)

An amphibious animal goes this share of its usual steps on land, rounded half up, at least one.

Default **0.5**; from 0.1 to 1, in steps of 0.05.

#### 2.8.3 Amphibious cooldown (`AMPHIBIOUS_COOLDOWN`)

An amphibious animal rests this many times as long after breeding.

Default **2**; from 1 to 5, in steps of 0.25.

### 2.9 Active share (`ACTIVE_SHARE`)

Share of its waking half day the average animal's stamina lets it act, resting the rest; the fatigues below scale with the day's length to keep it.

Default **0.666667**; from 0.1 to 0.9, in steps of 0.05.

#### 2.9.1 Action fatigue (`ACTION_FATIGUE`)

Stamina any action but rest costs, relative to the others (an average turn's acting costs 20).

Default **10**; from 0 to 50, in steps of 0.5.

#### 2.9.2 Step fatigue (`STEP_FATIGUE`)

Stamina each square moved costs, more.

Default **5**; from 0 to 50, in steps of 0.5.

#### 2.9.3 Flight fatigue (`FLIGHT_FATIGUE`)

A flyer's squares tire it this many times as much.

Default **2**; from 1 to 5, in steps of 0.25.

#### 2.9.4 Strike fatigue (`STRIKE_FATIGUE`)

Stamina each blow struck costs, more.

Default **50**; from 0 to 100, in steps of 1.

#### 2.9.5 Breed fatigue (`BREED_FATIGUE`)

Stamina mating costs each parent.

Default **75**; from 0 to 100, in steps of 1.

#### 2.9.6 Hover fatigue (`HOVER_FATIGUE`)

Stamina a flyer over the sea, unable to rest, loses a turn more; exhausted there, it drowns.

Default **5**; from 0 to 50, in steps of 0.5.

#### 2.9.7 Amphibious fatigue (`AMPHIBIOUS_FATIGUE`)

An amphibious animal tires this many times as fast at everything.

Default **2**; from 1 to 5, in steps of 0.25.

### 2.10 Walk rest (`WALK_REST`)

Share of the turn's burn a one-cell walk saves, as resting does.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 2.10.1 Go effort (`GO_EFFORT`)

Share of a turn's burn each cell a go_ moves beyond the first costs.

Default **0.25**; from 0 to 2, in steps of 0.05.

## 3. Breeding

### 3.1 Breeding reserve (`BREED_RESERVE`)

Hunger a parent must still have spare after paying to breed.

Default **0**; from 0 to 100, in steps of 5.

### 3.2 Mate ring difference (`MATE_RING_DIFFERENCE`)

Most a partner's ring colour may differ, at no tolerance.

Default **0.05**; from 0 to 1, in steps of 0.01.

#### 3.2.1 Mate dot difference (`MATE_DOT_DIFFERENCE`)

Most a partner's dot colour may differ, at no tolerance.

Default **0.2**; from 0 to 1, in steps of 0.01.

#### 3.2.2 Smallest litter (`LITTER_RANGE.0`)

Fewest young per birth, while there is room.

Default **1**; from 1 to 20, in steps of 1.

#### 3.2.3 Largest litter (`LITTER_RANGE.1`)

Most young per birth, while there is room.

Default **6**; from 1 to 20, in steps of 1.

#### 3.2.4 Small-body litters (`LITTER_SIZE_WEIGHT`)

Share of litter size from small bodies; the rest from the trait.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 3.2.5 Small-body litter boost (`SMALL_LITTERS`)

The smallest bodies' litters are this many times the usual, falling to the usual for the largest.

Default **2**; from 1 to 4, in steps of 0.25.

### 3.3 Shortest gestation (`GESTATION.0`)

Turns the smallest animals carry their young.

Default **30**; from 1 to 1500, in steps of 1.

#### 3.3.1 Longest gestation (`GESTATION.1`)

Turns the largest animals carry their young.

Default **150**; from 1 to 1500, in steps of 1.

### 3.4 Birth reach (`BIRTH_REACH`)

Young are born on free cells up to this many cells from their mother.

Default **2**; from 1 to 5, in steps of 1.

#### 3.4.1 Heat ends, grazers (`HEAT_CLOSEST.grazer`)

Share of a year before its next winter a grazer's heat ends; more for the patient.

Default **0.1**; from 0 to 0.75, in steps of 0.01.

#### 3.4.2 Heat ends, omnivores (`HEAT_CLOSEST.omnivore`)

Share of a year before its next winter an omnivore's heat ends; more for the patient.

Default **0.1**; from 0 to 0.75, in steps of 0.01.

#### 3.4.3 Heat ends, predators (`HEAT_CLOSEST.predator`)

Share of a year before its next winter a predator's heat ends; more for the patient.

Default **0.1**; from 0 to 0.75, in steps of 0.01.

#### 3.4.4 Heat, grazers (`HEAT_LENGTH.grazer`)

Share of a year a grazer is in heat, before its heat ends; 0.75: always but in its winter.

Default **0.5**; from 0 to 0.75, in steps of 0.01.

#### 3.4.5 Heat, omnivores (`HEAT_LENGTH.omnivore`)

Share of a year an omnivore is in heat, before its heat ends.

Default **0.5**; from 0 to 0.75, in steps of 0.01.

#### 3.4.6 Heat, predators (`HEAT_LENGTH.predator`)

Share of a year a predator is in heat, before its heat ends.

Default **0.5**; from 0 to 0.75, in steps of 0.01.

#### 3.4.7 Heat foresight (`HEAT_FORESIGHT`)

Share of a year further from winter the most patient end their heat.

Default **0.15**; from 0 to 0.75, in steps of 0.01.

### 3.5 Plant gestation (`PLANT_GESTATION`)

Turns a seed lies in the ground before it can sprout.

Default **30**; from 0 to 1500, in steps of 1.

### 3.6 Crowding limit (`CROWDING_LIMIT`)

Litters shrink as crowding nears this; at it, no breeding; 0: no limit.

Default **10**; from 0 to 100, in steps of 1.

### 3.7 Forage radius (`FORAGE_RADIUS`)

Food and mouths are weighed within this many cells.

Default **6**; from 1 to 12, in steps of 1.

#### 3.7.1 Food needed (`FOOD_NEEDED`)

Meals each mouth needs nearby to breed; twice this for a full litter; 0: no limit.

Default **0**; from 0 to 20, in steps of 0.5.

#### 3.7.2 Uproot food (`UPROOT_FOOD`)

A starving grazer uproots a plant only with fewer meals a mouth than this about it.

Default **3**; from 0 to 20, in steps of 0.5.

#### 3.7.3 Breeding cooldown (`BREED_COOLDOWN`)

Grazers' and omnivores' share of the usual rest after breeding (15 to 60 turns by fertility); 0: none.

Default **1**; from 0 to 2, in steps of 0.05.

#### 3.7.4 Breeding cooldown, predators (`PREDATOR_BREED_COOLDOWN`)

Share of the usual rest after breeding for predators; 0: none.

Default **0.5**; from 0 to 2, in steps of 0.05.

#### 3.7.5 Food needed, predators (`PREDATOR_FOOD_NEEDED`)

Meals each mouth needs nearby for a predator to breed; 0: no limit.

Default **0**; from 0 to 20, in steps of 0.5.

### 3.8 Plants per grazer (`PLANTS_PER_GRAZER`)

Plants each grazer needs in its home climate band, shared among the grazers at home there, to breed; twice this for a full litter; 0: no limit.

Default **0.1**; from 0 to 10, in steps of 0.05.

### 3.9 Prey per predator (`PREY_PER_PREDATOR`)

Grazers and omnivores each predator needs in its home climate band, shared among the predators at home there, to breed; twice this for a full litter; 0: no limit.

Default **2**; from 0 to 20, in steps of 0.1.

#### 3.9.1 Plant meals (`PLANT_MEALS`)

Meals a plant is worth to a full-efficiency browser.

Default **2**; from 0 to 20, in steps of 0.5.

#### 3.9.2 Prey meals (`PREY_MEALS`)

Meals a prey animal is worth.

Default **10**; from 0 to 20, in steps of 0.5.

#### 3.9.3 Shared diet (`SHARED_DIET`)

How much another species eating the same food counts as a mouth.

Default **0.5**; from 0 to 1, in steps of 0.05.

### 3.10 Grass outlook (`GRASS_OUTLOOK`)

Grass is judged by what regrows in this many turns.

Default **450**; from 0 to 3000, in steps of 10.

#### 3.10.1 Standing grass (`STANDING_GRASS`)

Plus this share of the grass standing now.

Default **0.25**; from 0 to 1, in steps of 0.05.

### 3.11 Feeding hunger (`FEED_HUNGER`)

How hungry one of its infants within its sight must be for a parent to feed them all, at no cost to it.

Default **25**; from 0 to 100, in steps of 5.

## 4. Feelings

### 4.1 Death penalty (`DEATH_PENALTY`)

How sharply a brain learns from the choice that killed it.

Default **2**; from 0 to 10, in steps of 0.1.

### 4.2 Breeding joy (`BREED_JOY`)

Pleasure of breeding, for each parent: the greatest there is.

Default **4**; from 0 to 10, in steps of 0.05.

#### 4.2.1 Courting joy (`COURT_JOY`)

Pleasure of courting a ready, willing partner into reach to breed.

Default **2.5**; from 0 to 10, in steps of 0.05.

#### 4.2.2 Libido in joy (`LIBIDO_IN_JOY`)

Share of breeding's and courting's pleasure that libido sets; the rest every animal feels.

Default **0.25**; from 0 to 1, in steps of 0.05.

#### 4.2.3 Fighting joy (`FIGHT_JOY`)

Pleasure of a blow, scaled by aggression.

Default **0.5**; from 0 to 5, in steps of 0.05.

#### 4.2.4 Company joy (`COMPANY_JOY`)

Pleasure of company, scaled by sociability.

Default **0.1**; from 0 to 5, in steps of 0.05.

#### 4.2.5 Curiosity joy (`CURIOSITY_JOY`)

Pleasure of setting off in a direction, scaled by curiosity.

Default **0.15**; from 0 to 5, in steps of 0.05.

### 4.3 Feasting joy (`FEAST_JOY`)

A predator's pleasure in feasting on a fresh kill.

Default **1.5**; from 0 to 5, in steps of 0.05.

#### 4.3.1 Sated joy (`SATED_JOY`)

Pleasure in hunger relieved by eating, hunting, feasting or scavenging, for the full range.

Default **1**; from 0 to 5, in steps of 0.05.

#### 4.3.2 Attacking joy (`ATTACK_JOY`)

A predator's pleasure in a blow it lives through.

Default **0.1**; from 0 to 5, in steps of 0.05.

### 4.4 Fear dread (`FEAR_DREAD`)

Dread of hunters nearby, scaled by fear.

Default **0.3**; from 0 to 5, in steps of 0.05.

#### 4.4.1 Unease (`UNEASE`)

How much an intolerant animal minds other species close by.

Default **0.1**; from 0 to 5, in steps of 0.05.

#### 4.4.2 Climate unease (`CLIMATE_UNEASE`)

Discomfort two steps from home, half that one step; less if hardy.

Default **2**; from 0 to 5, in steps of 0.05.

#### 4.4.3 Resting unease (`RESTING_UNEASE`)

Unease, each turn, of having chosen to rest out of its temperature, two steps from home; half one step, less if hardy, none asleep or spent.

Default **1**; from 0 to 5, in steps of 0.05.

#### 4.4.4 Homeward unease (`HOMEWARD_UNEASE`)

More discomfort the further home lies, this much half the map away; each step home eases it.

Default **3**; from 0 to 5, in steps of 0.05.

#### 4.4.5 Empathy pain (`EMPATHY_PAIN`)

How much an empathetic animal feels another's wound.

Default **0.15**; from 0 to 5, in steps of 0.05.

### 4.5 Feeding joy (`FEEDING_JOY`)

How glad a parent is of feeding a starving infant of its own, in proportion for one less hungry; more if empathetic.

Default **1**; from 0 to 5, in steps of 0.05.

#### 4.5.1 Parent joy (`PARENT_JOY`)

How glad a parent is of three or more of its own infants within its sight; more if empathetic.

Default **0.3**; from 0 to 5, in steps of 0.05.

#### 4.5.2 Parent grief (`PARENT_GRIEF`)

How much a parent grieves when one of its infants dies, wherever; more if empathetic.

Default **2**; from 0 to 10, in steps of 0.1.

### 4.6 Pregnant remorse (`PREGNANT_REMORSE`)

How much a hunter regrets killing a mother carrying young; more if empathetic.

Default **1**; from 0 to 10, in steps of 0.1.

### 4.7 Hunter's restraint (`HUNTER_RESTRAINT`)

Unease of a kill where few others of its prey are left in its sight: in full with none, none with four or more.

Default **1**; from 0 to 10, in steps of 0.1.

#### 4.7.1 Last-bite unease (`LAST_BITE`)

Unease of eating grass or a plant and leaving none of either in its sight.

Default **0.5**; from 0 to 10, in steps of 0.1.

#### 4.7.2 Sower's pride (`SOWER_PRIDE`)

Gladness of seeing a seed it sowed sprout, within its sight.

Default **1**; from 0 to 10, in steps of 0.1.

#### 4.7.3 Famine lesson (`FAMINE_LESSON`)

How much of a sudden death's lesson a herd-mate's starving teaches those who see it; 0 for none.

Default **1**; from 0 to 1, in steps of 0.05.

### 4.8 Other kind (`OTHER_KIND`)

How much of that pain reaches it for another species.

Default **0.4**; from 0 to 1, in steps of 0.05.

## 5. Society

### 5.1 Social learning (`SOCIAL_LEARNING`)

How much a herd-mate's success or failure seen teaches, by empathy and intellect.

Default **1**; from 0 to 3, in steps of 0.05.

#### 5.1.1 Social mood (`SOCIAL_MOOD`)

How much a herd-mate's success or failure seen moves the mood.

Default **0.3**; from 0 to 3, in steps of 0.05.

#### 5.1.2 Social memory (`SOCIAL_MEMORY`)

Share of a seen success or failure still sensed a turn later.

Default **0.93**; from 0 to 1, in steps of 0.05.

### 5.2 Loyalty pull (`LOYALTY_PULL`)

How much more an animal values keeping to its herd, times its loyalty.

Default **0.25**; from 0 to 5, in steps of 0.05.

### 5.3 Loyalty margin (`LOYALTY_MARGIN`)

How much more esteem, times its loyalty, a follower must hold another in before it may leave its leader for it; past that, the likelier the further, and surer the less loyal.

Default **0.5**; from 0 to 1, in steps of 0.05.

### 5.4 Conviction level (`CONVICTION_LEVEL`)

Share of its full range at which empathy or tolerance becomes a conviction kept even starving: an empathetic grazer won't uproot, an empathetic predator spares the young and those carrying young, a tolerant animal strikes no foe and drives none out.

Default **1**; from 0 to 1, in steps of 0.05.

### 5.5 Sociable trade (`SOCIABLE_TRADE`)

How much more an animal values making an offer, times its sociability.

Default **0.25**; from 0 to 5, in steps of 0.05.

### 5.6 Empathy scruple (`EMPATHY_SCRUPLE`)

How much less an animal values uprooting a plant, times its empathy, and a predator hunting, striking or feasting on the young or those carrying young.

Default **1**; from 0 to 5, in steps of 0.05.

#### 5.6.1 Tolerance scruple (`TOLERANCE_SCRUPLE`)

How much less an animal values striking a foe or driving one out, times its tolerance; hunting to eat is untouched.

Default **1**; from 0 to 5, in steps of 0.05.

### 5.7 Dominance ease (`DOMINANCE_EASE`)

Share less a crowded territory troubles the most dominant, and more the most submissive.

Default **0.5**; from 0 to 1, in steps of 0.05.

### 5.8 Trust heed (`TRUST_HEED`)

Share more the most trusting take in what herd-mates do, and less the least.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 5.8.1 Student intellect (`STUDENT_INTELLECT`)

The least inborn intellect an animal needs to be taught.

Default **4**; from 0 to 7, in steps of 1.

#### 5.8.2 Student age (`STUDENT_AGE`)

The oldest a student may be, as a share of its elder's age.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 5.8.3 Student trust (`STUDENT_TRUST`)

How trusting, 0 to 1, an animal must be to be willing to be taught.

Default **0.25**; from 0 to 1, in steps of 0.05.

#### 5.8.4 Teaching joy (`TEACHING_JOY`)

How glad an elder is of teaching a student; more if empathetic.

Default **1**; from 0 to 5, in steps of 0.05.

#### 5.8.5 Trust reach (`TRUST_REACH`)

Times its trust times its sight away a herd-mate's alert is heeded.

Default **2**; from 0 to 5, in steps of 0.25.

### 5.9 Kinship shun (`KINSHIP_SHUN`)

At full kinship and no tolerance, a herd-mate's or partner's social background may differ by no more than 1 less this.

Default **0.8**; from 0 to 1, in steps of 0.05.

## 6. Mind

### 6.1 Parental instinct (`PARENTAL_INSTINCT`)

How much of the gap between feeding its young and its best other option a parent closes, times the hungriest one's hunger and a half and its empathy (at 2, its young half hungry, level with the best).

Default **2**; from 0 to 5, in steps of 0.1.

### 6.2 Learning rate (`LEARNING_RATE`)

How fast a brain learns, at the highest intellect.

Default **0.027**; from 0 to 0.5, in steps of 0.005.

#### 6.2.1 Memory turns (`MEMORY_TURNS`)

Turns the best memory recalls where it last fed or was struck; fewer the poorer.

Default **300**; from 0 to 3000, in steps of 10.

#### 6.2.2 Resilience (`RESILIENCE`)

Share faster the most resilient get over a seen failure, and less they fear a bad experience.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 6.2.3 Moodiness (`MOODINESS`)

Share more experience moves the moodiest temperament, and less the steadiest.

Default **0.5**; from 0 to 1, in steps of 0.05.

### 6.3 Discount, most patient (`PATIENT_DISCOUNT`)

How much the most patient value what comes later (gamma).

Default **0.99**; from 0.5 to 0.999, in steps of 0.001.

#### 6.3.1 Discount, least patient (`IMPATIENT_DISCOUNT`)

How much the least patient value what comes later (gamma): their foresight, at the least.

Default **0.9**; from 0.5 to 0.999, in steps of 0.001.

### 6.4 Trace decay (`TRACE_DECAY`)

How far back a choice's outcome reaches earlier choices (lambda).

Default **0.7**; from 0 to 1, in steps of 0.05.

#### 6.4.1 Trace decay, smartest (`SMART_TRACE_DECAY`)

Trace decay at the highest intellect; the lowest keeps the one above.

Default **0.95**; from 0 to 1, in steps of 0.01.

#### 6.4.2 Flat spot (`FLAT_SPOT`)

How much learning a saturated neuron keeps: added to each hidden neuron's slope, so one pinned at either end of its range still learns a little; 0 is the plain slope, which lets a jammed neuron stall.

Default **0.1**; from 0 to 0.5, in steps of 0.01.

#### 6.4.3 Lesson limit (`LESSON_LIMIT`)

The most one lesson's error counts for: higher, and a rare but large surprise teaches faster, but learning runs jumpier.

Default **2**; from 0.5 to 20, in steps of 0.5.

#### 6.4.4 Choice temperature (`TEMPERATURE`)

How loosely a brain picks among its options.

Default **0.5**; from 0.01 to 5, in steps of 0.05.

#### 6.4.5 Brain mutation (`MUTATION`)

How much a newborn's brain varies from its parents'.

Default **0**; from 0 to 1, in steps of 0.01.

### 6.5 Learning floor (`LEARNING_FLOOR`)

The share of the learning rate even the dullest animal learns at; its intellect adds to it.

Default **0.25**; from 0 to 2, in steps of 0.05.

#### 6.5.1 Learning boost (`LEARNING_BOOST`)

The highest intellect learns this much faster again; the lowest, no faster.

Default **1**; from 0 to 5, in steps of 0.1.

#### 6.5.2 Replays per intellect (`REPLAY_PER_INTELLECT`)

Recent experiences re-learnt each turn per point of intellect.

Default **2**; from 0 to 4, in steps of 1.

### 6.6 Value limit (`VALUE_LIMIT`)

The most, and least, an animal learns a choice is worth, so its brain's overestimates can't run away; 0: no limit.

Default **50**; from 0 to 500, in steps of 5.

### 6.7 Breeding learning (`BREED_LEARNING`)

Times as much any animal learns from breeding; more unsettles its brain for all else.

Default **1**; from 0 to 20, in steps of 0.1.

#### 6.7.1 Courting learning (`COURT_LEARNING`)

Times as much any animal learns from courting; more unsettles its brain for all else.

Default **1**; from 0 to 20, in steps of 0.1.

#### 6.7.2 Kill food learning (`KILL_FOOD_LEARNING`)

Times as much any animal learns from hunting, attacking and feasting.

Default **2**; from 0 to 10, in steps of 0.1.

#### 6.7.3 Plant food learning (`PLANT_FOOD_LEARNING`)

Times as much any animal learns from eating, foraging, browsing and uprooting.

Default **0.5**; from 0 to 10, in steps of 0.1.

### 6.8 Forced lessons (`FORCED_LESSONS`)

1: an animal learns from every turn, even one it had no choice in (asleep, exhausted, panicking or frozen); 0: only from the turns it chose.

Default **0**; from 0 to 1, in steps of 1.

### 6.9 Predator learning (`PREDATOR_LEARNING`)

Times as fast a predator's brain learns from its own choices; an omnivore gets half the bonus.

Default **1**; from 1 to 5, in steps of 0.1.

### 6.10 Curious exploration (`CURIOUS_EXPLORATION`)

The chance, times its curiosity, that an animal tries any of its options at random rather than as its brain values them.

Default **0.1**; from 0 to 1, in steps of 0.01.

### 6.11 Fatal lesson (`FATAL_LESSON`)

How many times as hard a herd-mate's death teaches those who see it what it last did.

Default **5**; from 1 to 20, in steps of 0.5.

### 6.12 Boredom rise (`BOREDOM_RISE`)

How much boredom a turn of doing what it did lately adds, out of 1.

Default **0.02**; from 0 to 0.2, in steps of 0.005.

#### 6.12.1 Boredom relief (`BOREDOM_RELIEF`)

How much boredom doing something long undone takes away.

Default **0.25**; from 0 to 1, in steps of 0.05.

#### 6.12.2 Boredom drive (`BOREDOM_DRIVE`)

The chance, at full boredom, that an animal not in peril does another option instead, the longest undone the likeliest.

Default **0.2**; from 0 to 1, in steps of 0.05.

#### 6.12.3 Boredom unease (`BOREDOM_UNEASE`)

How unpleasant a turn of full boredom is.

Default **0.2**; from 0 to 2, in steps of 0.05.

#### 6.12.4 Untried hope (`UNTRIED_HOPE`)

How highly an animal values an option it has never taken in its life, as a share of the average of those it has: 1 as well as the average, so that what it has never tried gets tried; 0 as its brain alone does.

Default **1**; from 0 to 1.5, in steps of 0.05.

### 6.13 Foresight learning (`FORESIGHT_LEARNING`)

How fast a brain learns what its next turn will do to its hunger, health, stamina and food, as a share of its learning rate; 0 stops it, and surprise with it.

Default **0.5**; from 0 to 2, in steps of 0.05.

#### 6.13.1 Surprise joy (`SURPRISE_JOY`)

How pleasant a turn wholly unlike what it foresaw is, times a half and its curiosity; 0 turns the feeling off.

Default **0.2**; from 0 to 2, in steps of 0.05.

### 6.14 Imagination (`IMAGINATION`)

How much what it imagines would come of its likeliest options sways its choice: the better it foresees an option leaving it, the likelier; 0 stops it imagining, and surprise with it.

Default **0.5**; from 0 to 5, in steps of 0.1.

#### 6.14.1 Restless curiosity (`RESTLESS_CURIOSITY`)

The chance, times a half and its curiosity, that an animal whose brain chose to rest (not made to) does any of its other options instead; none in peril (a hunter or threat near, struck, or starving).

Default **0.5**; from 0 to 1, in steps of 0.05.

### 6.15 Heading reluctance (`HEADING_RELUCTANCE`)

How much less a new brain values setting off in a direction.

Default **0.5**; from 0 to 2, in steps of 0.05.

### 6.16 Temperament relapse (`TEMPERAMENT_RELAPSE`)

Share of the gap back to the inborn value closed each turn.

Default **0.0005**; from 0 to 0.1, in steps of 0.001.

### 6.17 Panic, unhurt (`PANIC.0`)

Chance a fully fearful animal breaks when struck, unhurt.

Default **0.3**; from 0 to 1, in steps of 0.05.

#### 6.17.1 Panic, near death (`PANIC.1`)

Chance a fully fearful animal breaks when struck, near death.

Default **1**; from 0 to 1, in steps of 0.05.

#### 6.17.2 Primal fear (`PRIMAL_FEAR`)

Chance a turn of being overcome by an overwhelmingly stronger hunter in sight.

Default **1**; from 0 to 1, in steps of 0.05.

#### 6.17.3 Primal fear, predators (`PRIMAL_FEAR_PREDATORS`)

How susceptible predators are, as a share.

Default **0**; from 0 to 1, in steps of 0.05.

#### 6.17.4 Predators loom (`PREDATOR_LOOMS`)

Steps larger and stronger a predator counts as, sizing up a fight for primal fear.

Default **2**; from 0 to 7, in steps of 1.

#### 6.17.5 Predator intelligence (`PREDATOR_INTELLIGENCE`)

Times over a predator's intellect counts, up to the full range.

Default **2**; from 1 to 5, in steps of 0.1.

#### 6.17.6 Brain cost (`BRAIN_COST`)

How much faster the brightest animal's hunger burns than the dullest's.

Default **0.15**; from 0 to 1, in steps of 0.01.

#### 6.17.7 Predator brain cost (`PREDATOR_BRAIN_COST`)

The share of the brain cost a predator pays.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 6.17.8 Predator fear (`PREDATOR_FEAR`)

Share of fear's effects a predator feels: dread, panic, keeping to its herd.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 6.17.9 Primal freeze (`PRIMAL_FREEZE`)

Chance an overcome animal freezes rather than flees, at the lowest intellect.

Default **1**; from 0 to 1, in steps of 0.05.

### 6.18 Cowardly omnivores (`COWARDLY_OMNIVORES`)

1: an omnivore struck always breaks and flees, whatever its fear; 0: it checks its morale.

Default **1**; from 0 to 1, in steps of 1.

### 6.19 Mate reach (`MATE_REACH`)

Steps within which a partner is close enough to breed with.

Default **2**; from 1 to 5, in steps of 1.

#### 6.19.1 Mating call (`MATING_CALL`)

How much more a predator ready to breed values breeding, or else courting a mate it sees.

Default **0.5**; from 0 to 5, in steps of 0.05.

#### 6.19.2 Mating call range (`MATING_CALL_RANGE`)

Times its sight a predator ready to breed seeks a mate, when it sees none.

Default **2**; from 0 to 3, in steps of 0.25.

#### 6.19.3 Mating call, omnivores (`OMNIVORE_MATING_CALL`)

How much more an omnivore ready to breed values breeding, or else courting.

Default **0.25**; from 0 to 5, in steps of 0.05.

#### 6.19.4 Mating call range, omnivores (`OMNIVORE_MATING_CALL_RANGE`)

Times its sight an omnivore ready to breed seeks a mate, when it sees none.

Default **2**; from 0 to 3, in steps of 0.25.

#### 6.19.5 First breeding instinct (`FIRST_BREEDING_INSTINCT`)

How much more one of a world's first Bitzkeys, its brain untrained, values breeding when ready, or else courting.

Default **3**; from 0 to 10, in steps of 0.25.

#### 6.19.6 Breeding cost, hunters (`HUNTER_BREEDING_COST`)

Share of the usual breeding cost a predator or omnivore pays.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 6.19.7 Breeding cost, grazers (`GRAZER_BREEDING_COST`)

Share of the usual breeding cost a grazer pays.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 6.19.8 Predator sting (`PREDATOR_STING`)

Share of a victim's spines a predator striking it takes.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 6.19.9 Hunting instinct (`HUNTING_INSTINCT`)

How much more a predator values hunting and attacking, times its hunger.

Default **1**; from 0 to 5, in steps of 0.05.

### 6.20 Scarce population (`SCARCE_POPULATION`)

Below this many living, a species' breeding cost begins to fall.

Default **100**; from 1 to 2000, in steps of 10.

#### 6.20.1 Rare population (`RARE_POPULATION`)

At this many living or fewer, a species pays the rare breeding cost.

Default **10**; from 0 to 1000, in steps of 1.

#### 6.20.2 Rare breeding cost (`RARE_BREEDING_COST`)

Share of the usual breeding cost a rare species pays.

Default **0.333333**; from 0 to 1, in steps of 0.05.

#### 6.20.3 Predator full litter (`PREDATOR_FULL_LITTER_FOOD`)

Times the prey a predator needs to breed that its full litter needs.

Default **8**; from 1 to 20, in steps of 0.5.

#### 6.20.4 Grazer scarce population (`GRAZER_SCARCE_POPULATION`)

Below this many living, grazers' breeding cost begins to fall.

Default **300**; from 1 to 3000, in steps of 10.

#### 6.20.5 Grazer rare breeding cost (`GRAZER_RARE_BREEDING_COST`)

Share of the usual breeding cost rare grazers pay.

Default **0.111111**; from 0 to 1, in steps of 0.01.

### 6.21 Full litter food (`FULL_LITTER_FOOD`)

Times the home food a mouth needs to breed that a full litter needs.

Default **4**; from 1 to 10, in steps of 0.5.

#### 6.21.1 Predator glut (`PREDATOR_GLUT`)

Above this many predators alive, they turn on other kinds, fully at twice it, and their own kind past that.

Default **600**; from 0 to 5000, in steps of 10.

#### 6.21.2 Rare prey (`RARE_PREY`)

A hunter doesn't breed while its prey species, all together, number under this percentage of its own.

Default **1800**; from 0 to 5000, in steps of 10.

### 6.22 Newborn food (`NEWBORN_FOOD`)

Food, as a share of a stomach, each young must start with: parents conceive no more young than they can give this; 0: no limit.

Default **15**; from 0 to 100, in steps of 5.

#### 6.22.1 Pack mouths (`PACK_MOUTHS`)

Mouths each pack-mate counts as on a hunter's food, sharing its kills.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 6.22.2 Bloodlust (`BLOODLUST`)

How much more a predator values feasting whenever it sees a fresh kill.

Default **1**; from 0 to 5, in steps of 0.05.

#### 6.22.3 Carrion craving (`CARRION_CRAVING`)

How much more the highest carrion trait values scavenging carrion it sees; none up to the middle.

Default **1**; from 0 to 5, in steps of 0.05.

### 6.23 Carrion aversion (`CARRION_AVERSION`)

How much less the highest carrion trait values hunting and attacking; none up to the middle.

Default **1**; from 0 to 5, in steps of 0.05.

#### 6.23.1 Carrion cowardice (`CARRION_COWARDICE`)

The highest carrion trait's least chance of breaking when struck; none up to the middle.

Default **1**; from 0 to 1, in steps of 0.05.

#### 6.23.2 Alert joy (`ALERT_JOY`)

Pleasure of alerting its herd to a hunter (grazers) or a fresh kill (hunters).

Default **0.1**; from 0 to 5, in steps of 0.05.

#### 6.23.3 Alarm reach (`ALARM_REACH`)

Steps within which an alarmed animal knows where the hunter is.

Default **6**; from 1 to 20, in steps of 1.

### 6.24 Call reach (`CALL_REACH`)

Steps a call carries, times a half and its caller's size (as a share of its range): the larger, the further.

Default **16**; from 1 to 30, in steps of 1.

#### 6.24.1 Call cost (`CALL_COST`)

Hunger each sound of a call costs its caller, the alert among them.

Default **0.05**; from 0 to 5, in steps of 0.05.

#### 6.24.2 Call length (`CALL_LENGTH`)

The most sounds a call may run to, in order: 1 single sounds only, 3 pairs and threes too.

Default **3**; from 1 to 3, in steps of 1.

### 6.25 Call habit (`CALL_HABIT`)

Pleasure of a call that matches those it made where it sensed most alike, more than its calls at large; which call goes with what is its own; 0 for none.

Default **0.5**; from 0 to 2, in steps of 0.1.

### 6.26 Call empathy (`CALL_EMPATHY`)

How much a caller learns, by its empathy, from whether those of its kind who heard its call then fared better or worse than they expected, kin most; 0 for none.

Default **1**; from 0 to 5, in steps of 0.1.

### 6.27 Memory steps (`MEMORY_STEPS`)

Choices back a lesson's error reaches the memory gates that wrote what the brain read; 0: memory is only as born.

Default **1**; from 0 to 4, in steps of 1.

#### 6.27.1 Memory learning (`MEMORY_LEARNING`)

How fast memory gates learn, times the brain's learning rate.

Default **1**; from 0 to 5, in steps of 0.05.

### 6.28 Map places (`MAP_PLACES`)

Places an animal's mental map holds, the one visited longest ago forgotten for a new one; 0: no map.

Default **16**; from 0 to 32, in steps of 1.

#### 6.28.1 Map learning (`MAP_LEARNING`)

How far each visit moves what an animal remembers of a place in that season toward what it finds.

Default **0.2**; from 0 to 1, in steps of 0.05.

### 6.29 Acquaintances (`ACQUAINTANCES`)

Others an animal remembers by what they did to it, the one it dealt with longest ago forgotten for a new one; 0: none.

Default **8**; from 0 to 16, in steps of 1.

#### 6.29.1 Regard learning (`REGARD_LEARNING`)

How far each deed moves an animal's regard for the doer toward the deed's worth.

Default **0.3**; from 0 to 1, in steps of 0.05.

### 6.30 Diary entries (`DIARY_ENTRIES`)

Most vivid experiences an animal keeps and re-learns from, one each turn, the faintest forgotten for a more vivid one; 0: none.

Default **8**; from 0 to 32, in steps of 1.

#### 6.30.1 Diary fading (`DIARY_FADING`)

How much of a diary entry's vividness is left after each turn.

Default **0.999**; from 0.9 to 1, in steps of 0.001.

### 6.31 Regard comfort (`REGARD_COMFORT`)

Ease near a friend, more if trusting, and unease near an enemy, more if fearful.

Default **0.5**; from 0 to 2, in steps of 0.05.

#### 6.31.1 Grudge joy (`GRUDGE_JOY`)

Gladness in harming an enemy, more if aggressive, and pain in harming a friend, more if empathetic.

Default **1**; from 0 to 5, in steps of 0.1.

#### 6.31.2 Friend joy (`FRIEND_JOY`)

Extra gladness in feeding or teaching a friend; more if empathetic.

Default **1**; from 0 to 5, in steps of 0.1.

#### 6.31.3 Factions (`FACTIONS`)

How many body colours an animal keeps a reputation for; none at 0.

Default **8**; from 0 to 16, in steps of 1.

#### 6.31.4 Faction share (`FACTION_SHARE`)

How much of a deed's weight also falls on the doer's whole colour.

Default **0.25**; from 0 to 1, in steps of 0.05.

#### 6.31.5 Faction width (`FACTION_WIDTH`)

How far apart on the body ring two animals may be and count as one faction.

Default **0.05**; from 0 to 0.5, in steps of 0.01.

#### 6.31.6 Eavesdropping (`EAVESDROP_SHARE`)

How much a witness's view of one is moved by seeing one it holds a friend treat that one, well or ill (a gift, a blow, a theft, a defeat), times its regard for that friend; no word passed, only seen.

Default **0.1**; from 0 to 1, in steps of 0.05.

#### 6.31.7 Witness share (`WITNESS_SHARE`)

How much of a deed's weight it has on regard when only seen done, not done to the animal.

Default **0.25**; from 0 to 1, in steps of 0.05.

### 6.32 Skill witness (`SKILL_WITNESS`)

How much of a deed's weight a craft, lesson or fire seen done has on how skilled the doer is held; no less than Witness share.

Default **1**; from 0 to 1, in steps of 0.05.

### 6.33 Inherited regard (`INHERITED_REGARD`)

Share of how one holds a child's parents, regard and reputation, it holds the child at until it judges it by its own deeds.

Default **0.5**; from 0 to 1, in steps of 0.05.

### 6.34 Shared bond (`SHARED_BOND`)

How much shared moments, a fire enjoyed together or a herd-mate defended, move regard, as a share of a deed's weight; 0 for none.

Default **0.2**; from 0 to 1, in steps of 0.05.

### 6.35 Skill comfort (`SKILL_COMFORT`)

Ease near one it holds skilled, more if curious; 0 for none.

Default **0.5**; from 0 to 2, in steps of 0.05.

#### 6.35.1 Watch learning (`WATCH_LEARNING`)

Chance that a craft seen done by one of its kind it holds no foe is learnt from the watching, and share of a shot's lesson a hit seen with a bow teaches; 0 for none.

Default **0.25**; from 0 to 1, in steps of 0.01.

### 6.36 Tutor joy (`TUTOR_JOY`)

Joy of giving one of its kind a lesson in a craft or skill, the more for the empathetic.

Default **0.5**; from 0 to 5, in steps of 0.05.

#### 6.36.1 Tutored practice (`TUTOR_PRACTICE`)

Share of a try, or of a shot's lesson, that a lesson from a tutor is worth.

Default **0.5**; from 0 to 1, in steps of 0.05.

### 6.37 Child call learning (`CHILD_CALL_LEARNING`)

Share of its own learning a child that hears its parent call learns to make that call where the parent made it, and, hearing it, what the parent then did; 0 for none.

Default **1**; from 0 to 2, in steps of 0.05.

### 6.38 Body language (`BODY_LANGUAGE`)

Chance, for each point of its intellect, that one who sees the caller of its kind it hears understands what it meant: it learns to make that call where the caller made it, and, hearing it, what the caller then did; 0 for none.

Default **0.1**; from 0 to 0.2, in steps of 0.01.

#### 6.38.1 Elder comfort (`ELDER_COMFORT`)

Gladness at staying near the elder who taught it, more if trusting; 0 for none.

Default **0.5**; from 0 to 2, in steps of 0.05.

#### 6.38.2 Elder reach (`ELDER_REACH`)

Steps from the elder who taught it within which a student is glad of it.

Default **3**; from 0 to 20, in steps of 1.

#### 6.38.3 Follow safety (`FOLLOW_SAFETY`)

Share of its dread spared, and felt as relief, with a friend or one it holds skilled close by; 0 for none.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 6.38.4 Follow reach (`FOLLOW_REACH`)

Steps within which a friend or one it holds skilled eases its dread.

Default **2**; from 0 to 20, in steps of 1.

### 6.39 Warrior garb (`WARRIOR_GARB`)

Regard an omnivore's hide, a warrior's garb, draws those who see it toward.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 6.39.1 Prestige garb (`PRESTIGE_GARB`)

Regard a predator's hide, a mark of prestige, draws those who see it toward.

Default **1**; from 0 to 1, in steps of 0.05.

#### 6.39.2 Garb regard (`GARB_REGARD`)

How much of a deed's weight each turn's sight of a garb has on regard.

Default **0.05**; from 0 to 1, in steps of 0.01.

#### 6.39.3 Lie disregard (`LIE_DISREGARD`)

Disregard for one seen fleeing a fight in a garb, as a share of the regard its garb had won, which is lost too.

Default **1**; from 0 to 3, in steps of 0.1.

#### 6.39.4 Envy level (`ENVY_LEVEL`)

Share of its own kind, ranked by moodiness or dominance, that envies no one: 0.75 leaves the top quarter envious; 1 for none.

Default **0.75**; from 0 to 1, in steps of 0.05.

#### 6.39.5 Envy unease (`ENVY_UNEASE`)

Unease of seeing possessions better than its own, by how much better, at the most envious.

Default **0.5**; from 0 to 3, in steps of 0.05.

#### 6.39.6 Envy joy (`ENVY_JOY`)

Gladness of the envious at gaining a better possession, by how much better.

Default **1**; from 0 to 3, in steps of 0.05.

### 6.40 Foe level (`FOE_LEVEL`)

Disregard beyond which one is a foe, times 0.5 plus half the beholder's empathy and sociability together.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 6.40.1 Victory regard (`VICTORY_REGARD`)

How much a victory seen moves regard for the winner: up for a friend, down for a foe.

Default **0.3**; from 0 to 1, in steps of 0.05.

#### 6.40.2 Foe defeat regard (`FOE_DEFEAT_REGARD`)

How much more regard a victory over one's foe wins.

Default **0.3**; from 0 to 1, in steps of 0.05.

#### 6.40.3 Coward disregard (`COWARD_DISREGARD`)

How much fleeing a fight costs one's regard with those who hold it a friend or foe.

Default **0.3**; from 0 to 1, in steps of 0.05.

### 6.41 Culture regard (`CULTURE_REGARD`)

Esteem for a deed its kind values, seen done by one of its kind (twice from family, or a predator's pack): victories for predators, crafting and building for omnivores, sowing and fruit picked without uprooting for grazers.

Default **0.3**; from 0 to 1, in steps of 0.05.

#### 6.41.1 Give level (`GIVE_LEVEL`)

Share of its own kind, ranked by empathy or sociability, that never gives a friend a possession: 0.75 leaves the top quarter giving; 1 for none.

Default **0.75**; from 0 to 1, in steps of 0.05.

#### 6.41.2 Give joy (`GIVE_JOY`)

Pleasure of giving a friend a possession, times 0.5 plus empathy or sociability.

Default **0.5**; from 0 to 3, in steps of 0.05.

#### 6.41.3 Take level (`TAKE_LEVEL`)

Share of its own kind, ranked by aggression or dominance, that never demands or steals a possession: 0.75 leaves the top quarter taking; 1 for none.

Default **0.75**; from 0 to 1, in steps of 0.05.

#### 6.41.4 Theft disregard (`THEFT_DISREGARD`)

How much a theft seen costs the thief's regard, twice with its victim; regard instead with those who hold the victim a foe.

Default **0.5**; from 0 to 1, in steps of 0.05.

### 6.42 Trade misjudgement (`TRADE_MISJUDGE`)

How far an animal may misjudge a good's worth in a deal, either way, times one less its intelligence.

Default **0.2**; from 0 to 2, in steps of 0.05.

#### 6.42.1 Trade regard (`TRADE_REGARD`)

How much of a deed's weight an accepted offer has on regard, a kindness both ways.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 6.42.2 Trade joy (`TRADE_JOY`)

Pleasure a side takes in a deal it truly gained by; one that truly lost thinks as much the worse of the other.

Default **0.5**; from 0 to 3, in steps of 0.05.

#### 6.42.3 Trade wariness (`TRADE_WARINESS`)

How much worse an offer seems from one its owner thinks ill of, times that ill regard.

Default **0.5**; from 0 to 2, in steps of 0.05.

### 6.43 Leader followers (`LEADER_FOLLOWERS`)

Members of its colour group who must hold one as their standout for it to be recognised a leader, kept until fewer than half as many do; 0 for no leaders.

Default **5**; from 0 to 100, in steps of 1.

### 6.44 Leader deference (`LEADER_DEFERENCE`)

How much kinder a follower judges its leader's offers; it yields to its leader's demands without a fight.

Default **0.5**; from 0 to 2, in steps of 0.05.

Connected: 8.8 Leader rally (`LEADER_RALLY`).

#### 6.44.1 Land ambition (`LAND_AMBITION`)

Pull to stay in the land of a group it dislikes, and unease in the land of one it likes unless welcome there, times its regard for that group.

Default **0.1**; from 0 to 1, in steps of 0.01.

#### 6.44.2 Welcome level (`WELCOME_LEVEL`)

How well a group's members must think of a colour group, on average, to welcome it in their land as guests who neither trespass nor contest it; above 1 for none.

Default **0.5**; from 0 to 1.5, in steps of 0.05.

#### 6.44.3 Leader joy (`LEADER_JOY`)

Pleasure a leader takes in its followers in sight, in full with as many as Leader followers.

Default **0.3**; from 0 to 3, in steps of 0.05.

Connected: 8.8 Leader rally (`LEADER_RALLY`).

#### 6.44.4 Leader allure (`LEADER_ALLURE`)

How much more joy breeding with a leader gives its partner; a leader is courted first by any that may breed with it.

Default **0.5**; from 0 to 3, in steps of 0.05.

Connected: 8.8 Leader rally (`LEADER_RALLY`).

### 6.45 Call payoff (`CALL_PAYOFF`)

Joy a caller takes in each of its kind that heard its call and then got away from a hunter or, hungry, ate; times a half and its empathy or sociability.

Default **0**; from 0 to 2, in steps of 0.05.

#### 6.45.1 Communication joy (`COMMUNICATION_JOY`)

Share of its own gain each takes when one answers a call it heard, within a day, with a deed good for both: the caller glad to be understood, the listener to understand.

Default **0.5**; from 0 to 2, in steps of 0.05.

#### 6.45.2 Heeded regard (`HEEDED_REGARD`)

How much better one helped so thinks of its caller, of a deed's weight.

Default **0**; from 0 to 1, in steps of 0.01.

### 6.46 Rank bonus (`RANK_BONUS`)

Share more of each leader's bonuses (deference, rally, joy in followers, allure) for each rank above chief.

Default **0.25**; from 0 to 2, in steps of 0.05.

#### 6.46.1 Title regard (`TITLE_REGARD`)

Esteem a titled leader wins each turn from each who sees it and holds it no foe, times its rank.

Default **0.01**; from 0 to 0.2, in steps of 0.005.

### 6.47 Warrior mark (`WARRIOR_MARK`)

Regard the mark of the warrior, earned by defeating an omnivore, draws those who see it toward.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 6.47.1 Prestige mark (`PRESTIGE_MARK`)

Regard the mark of prestige, earned by defeating a predator, draws those who see it toward.

Default **1**; from 0 to 1, in steps of 0.05.

#### 6.47.2 Mark regard (`MARK_REGARD`)

How much of a deed's weight each turn's sight of a mark has on regard, or, for a foe, on how dangerous it holds the bearer.

Default **0.05**; from 0 to 1, in steps of 0.01.

#### 6.47.3 Mark dread (`MARK_DREAD`)

Unease of a foe at the sight of a mark, times its worth and a half plus its fear.

Default **0.3**; from 0 to 3, in steps of 0.05.

### 6.48 Land size (`LAND_SIZE`)

Members a colour group needs to claim land; 0 for no lands.

Default **12**; from 0 to 500, in steps of 1.

Connected: 8.9.3 War kills (`WAR_KILLS`).

#### 6.48.1 Land square (`LAND_PATCH`)

Cells on a side of a square of land; a change begins all claims again.

Default **8**; from 2 to 64, in steps of 1.

Connected: 8.9.3 War kills (`WAR_KILLS`).

#### 6.48.2 Land hold (`LAND_HOLD`)

Members of a group standing in a square that claim it.

Default **3**; from 1 to 50, in steps of 1.

Connected: 8.9.3 War kills (`WAR_KILLS`).

#### 6.48.3 Land memory (`LAND_MEMORY`)

Counts a claim lasts with none of its group in the square, or while clearly losing the war for it.

Default **5**; from 1 to 100, in steps of 1.

Connected: 8.9.3 War kills (`WAR_KILLS`).

#### 6.48.4 Land count (`LAND_TURNS`)

Turns between counts of the groups and their lands.

Default **24**; from 1 to 1000, in steps of 1.

Connected: 8.9.3 War kills (`WAR_KILLS`).

#### 6.48.5 Land healing (`LAND_HEALING`)

How much faster an animal heals in its own land: 1 for twice as fast.

Default **1**; from 0 to 5, in steps of 0.1.

Connected: 8.9 Land defence (`LAND_DEFENCE`); 8.9.1 Land herd (`LAND_HERD`).

#### 6.48.6 Land comfort (`LAND_COMFORT`)

Ease in its own land, and unease as much away from it while its group holds land.

Default **0.1**; from 0 to 2, in steps of 0.05.

Connected: 8.9 Land defence (`LAND_DEFENCE`); 8.9.1 Land herd (`LAND_HERD`).

#### 6.48.7 Trespass regard (`TRESPASS_REGARD`)

How much of a deed's weight a trespasser's first turn in a land costs it with those of the land who see it.

Default **0.02**; from 0 to 1, in steps of 0.01.

Connected: 8.9 Land defence (`LAND_DEFENCE`); 8.9.1 Land herd (`LAND_HERD`).

#### 6.48.8 Trespass growth (`TRESPASS_GROWTH`)

How much that grows, of itself, each turn the trespasser stays, to a whole deed.

Default **0.25**; from 0 to 2, in steps of 0.05.

Connected: 8.9.2 Land rally (`LAND_RALLY`); 8.9.4 War nerve (`WAR_NERVE`).

#### 6.48.9 War enmity (`WAR_ENMITY`)

How much of a deed's weight each count of a war sets each member against its enemy's colour.

Default **0.1**; from 0 to 1, in steps of 0.05.

Connected: 8.9.2 Land rally (`LAND_RALLY`); 8.9.4 War nerve (`WAR_NERVE`).

#### 6.48.10 War morale (`WAR_MORALE`)

Gladness of a war clearly being won, and unease as much of one clearly being lost, each turn.

Default **0.1**; from 0 to 2, in steps of 0.05.

Connected: 8.9.2 Land rally (`LAND_RALLY`); 8.9.4 War nerve (`WAR_NERVE`).

### 6.49 Standing joy (`STANDING_JOY`)

Pleasure of being held a friend by those about it, in full with three or more, times a half and its dominance.

Default **0.2**; from 0 to 2, in steps of 0.05.

#### 6.49.1 Follow comfort (`FOLLOW_COMFORT`)

Ease near the member of its own colour group in sight it esteems most, times that esteem and closeness, and a half plus its trust and sociability together.

Default **0.2**; from 0 to 2, in steps of 0.05.

## 7. Food

### 7.1 Share joy (`SHARE_JOY`)

Pleasure of bringing food to its herd's store, times its generosity: the strongest of its patience, sociability and empathy, less the strongest of its dominance, aggression and appetite; the greedy are uneasy.

Default **0.5**; from 0 to 5, in steps of 0.1.

#### 7.1.1 Selfish unease (`SELFISH_JOY`)

Unease at eating fruit off a plant, a kill where it lies or what it carries with a herd to share with, times its generosity; the greedy are gratified.

Default **0.1**; from 0 to 5, in steps of 0.05.

### 7.2 Kin sharing joy (`KIN_SHARE_JOY`)

Pleasure of bringing food to its herd's store with a friend in sight there, times its kinship, whatever its generosity.

Default **1**; from 0 to 5, in steps of 0.1.

### 7.3 Meat portion (`MEAT_PORTION`)

Hunger a portion of meat from a pile sates, times how well the eater digests carrion.

Default **855**; from 0 to 2000, in steps of 5.

#### 7.3.1 Fruit portion (`FRUIT_PORTION`)

Hunger a portion of fruit from a pile sates, times the eater's share of the food in plants.

Default **70**; from 0 to 500, in steps of 5.

#### 7.3.2 Pile life (`PILE_LASTS`)

Times as long as a corpse a pile lasts, since food was last dropped on it.

Default **3**; from 0 to 20, in steps of 0.5.

#### 7.3.3 Plant meal for size (`PLANT_MEAL_FOR_SIZE`)

More hunger a bite sates, up to this for the largest plants.

Default **0**; from 0 to 200, in steps of 1.

#### 7.3.4 Plant bite (`PLANT_BITE`)

Health that makes a fruit: a plant holds one fruit for each, at least one, and picking one leaves it unhurt.

Default **15**; from 0 to 200, in steps of 1.

#### 7.3.5 Plant regrowth (`PLANT_REGROWTH`)

Share of its health a plant regrows each turn.

Default **0.02**; from 0 to 1, in steps of 0.005.

#### 7.3.6 Plant recovery (`PLANT_RECOVERY`)

Turns after it is uprooted from before a plant can be torn into again.

Default **24**; from 0 to 300, in steps of 1.

### 7.4 Wood life (`WOOD_LASTS`)

Times as long as a pile of food a pile of wood or bone lasts, unless sheltered.

Default **10**; from 0 to 100, in steps of 1.

#### 7.4.1 Plant meal (`PLANT_MEAL`)

Hunger a bite of a plant sates; a plant has a bite for every 15 of its health, more the larger.

Default **70**; from 0 to 200, in steps of 1.

### 7.5 Stone scatter (`STONE_SCATTER_PER`)

Squares to each stone scattered, once in a world's life.

Default **100**; from 10 to 1000, in steps of 10.

#### 7.5.1 Archery joy (`ARCHERY_JOY`)

Joy of a hit with a bow, in practice too, the more for the skilled.

Default **1**; from 0 to 5, in steps of 0.05.

#### 7.5.2 Archery regard (`ARCHERY_REGARD`)

Respect of all who see a hit with a bow, of a deed's weight, in skill and liking.

Default **0.1**; from 0 to 1, in steps of 0.01.

#### 7.5.3 Archery learning (`ARCHERY_LEARNING`)

Share of the skill left to learn that each shot with a bow teaches.

Default **0.05**; from 0 to 0.5, in steps of 0.01.

#### 7.5.4 Least armour (`ARMOUR_LEAST`)

The armour worn armour counts from, at the least.

Default **2**; from 0 to 7, in steps of 1.

### 7.6 Discovery chance (`DISCOVERY_CHANCE`)

Chance each turn, times intelligence, of discovering a craft whose materials it holds.

Default **0.0005**; from 0 to 1, in steps of 0.0001.

#### 7.6.1 Craft attempts (`CRAFT_TRIES`)

Tries by which a craft is sure to work.

Default **10**; from 1 to 50, in steps of 1.

#### 7.6.2 Craft thrill (`CRAFT_THRILL`)

The thrill of a failed craft, times curiosity.

Default **1**; from 0 to 5, in steps of 0.1.

#### 7.6.3 Discovery joy (`DISCOVERY_JOY`)

The joy of first discovering a craft; more if curious.

Default **5**; from 0 to 20, in steps of 0.5.

#### 7.6.4 Craft joy (`CRAFT_JOY`)

The joy of a craft that works, a weapon's in full and armour's in half; more if curious.

Default **1**; from 0 to 5, in steps of 0.1.

#### 7.6.5 Tool joy (`TOOL_JOY`)

The joy of the harm a weapon added to a blow, times the share of its victim's health that harm was.

Default **2**; from 0 to 10, in steps of 0.1.

#### 7.6.6 Armour joy (`ARMOUR_JOY`)

The joy of the harm its armour took off a blow, times the share of its health that harm was.

Default **2**; from 0 to 10, in steps of 0.1.

#### 7.6.7 Gear ease (`GEAR_EASE`)

Ease, each turn, of a weapon or bow in hand, and as much again of armour worn.

Default **0.15**; from 0 to 2, in steps of 0.05.

#### 7.6.8 Gear courage (`GEAR_COURAGE`)

Share of its fear's effects a weapon or bow in hand takes off, and as much again armour worn.

Default **0.25**; from 0 to 0.5, in steps of 0.05.

### 7.7 Donning joy (`DON_JOY`)

The joy of gaining a weapon or bow, as this share of tool joy, and armour, as this share of armour joy; dropping one is as much a loss.

Default **0.25**; from 0 to 2, in steps of 0.05.

#### 7.7.1 Gear desire (`GEAR_DESIRE`)

Desire for a weapon, armour or a bow stirred on seeing one pay off, friend or foe, up to 1; envy counts that gear one plus its desire times.

Default **0.05**; from 0 to 1, in steps of 0.01.

### 7.8 Wagon drag (`WAGON_DRAG`)

Share more tiring each step is, dragging a wagon.

Default **0.5**; from 0 to 3, in steps of 0.05.

#### 7.8.1 Ready joy (`READY_JOY`)

Joy, for each part, of first holding all a thing it knows how to make needs: a recipe's materials, a shelter's worth of stone or wood, or a road section's stones, with the tool each wants; mostly more if patient, a little if clever; breaking the set but by using or giving it is as much a loss.

Default **0.3**; from 0 to 2, in steps of 0.05.

#### 7.8.2 Build thrill (`BUILD_THRILL`)

The thrill of each stage of a shelter built, growing with every stage and doubled for finishing it, times curiosity.

Default **1**; from 0 to 5, in steps of 0.1.

### 7.9 Shelter size (`SHELTER_UNITS`)

Units of stone or wood that finish a shelter.

Default **7**; from 1 to 50, in steps of 1.

#### 7.9.1 Gluttony unease (`GLUTTONY_UNEASE`)

Unease at a meal eaten while less than half hungry, the more the fuller, times what it lacks of gluttony (a half, plus half its appetite, less half its kinship); none to the wholly gluttonous.

Default **1**; from 0 to 5, in steps of 0.1.

### 7.10 Shelter comfort (`SHELTER_COMFORT`)

Comfort of resting inside a finished shelter, half of it standing in one.

Default **0.3**; from 0 to 2, in steps of 0.05.

#### 7.10.1 Shelter longing (`SHELTER_LONGING`)

How much more building thrills one that has rested in a shelter, in full after a day inside, whatever its curiosity.

Default **1**; from 0 to 5, in steps of 0.1.

### 7.11 Road fatigue (`ROAD_FATIGUE`)

Stamina each turn of laying road costs, beyond the action's own.

Default **60**; from 0 to 200, in steps of 5.

#### 7.11.1 Road thrill (`ROAD_THRILL`)

The thrill of each turn's work on a road, growing with every turn and doubled for finishing it, times curiosity.

Default **1**; from 0 to 5, in steps of 0.1.

#### 7.11.2 Road link joy (`ROAD_LINK_JOY`)

Gratification of finishing a road that joins two shelters no road joined before.

Default **3**; from 0 to 10, in steps of 0.1.

#### 7.11.3 Road ease (`ROAD_EASE`)

Share of a step's stamina a step from a road square costs.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 7.11.4 Road joy (`ROAD_JOY`)

Pleasure of going by road, growing to the full of it after a day of road travel.

Default **0.3**; from 0 to 2, in steps of 0.05.

#### 7.11.5 Road longing (`ROAD_LONGING`)

How much more laying road thrills one that has travelled by road, in full after a day of it, whatever its curiosity.

Default **1**; from 0 to 5, in steps of 0.1.

#### 7.11.6 Protection joy (`PROTECTION_JOY`)

Relief at the weather kept off it by a shelter, a fire, or a parent close by, by as much unease as it spares.

Default **1**; from 0 to 5, in steps of 0.1.

### 7.12 Fertilizing joy (`FERTILIZING_JOY`)

Pleasure of feeding crushed bone to worked land, times curiosity.

Default **1**; from 0 to 5, in steps of 0.1.

#### 7.12.1 Marrow lean (`MARROW_LEAN`)

How far each marrow meal edges a grazer toward meat; at the full of it, it and its young are omnivores; 0 for never.

Default **0.05**; from 0 to 1, in steps of 0.01.

### 7.13 Skinning joy (`SKINNING_JOY`)

Pleasure of wearing a new, finer hide, times curiosity, the more for a finer one.

Default **1**; from 0 to 5, in steps of 0.1.

#### 7.13.1 Hide warmth (`HIDE_WARMTH`)

Steps of cold a worn hide keeps out.

Default **1**; from 0 to 3, in steps of 0.25.

#### 7.13.2 Hide armour share (`HIDE_ARMOUR_SHARE`)

Share of the armour of the body a hide came from that it adds to its wearer's.

Default **0.5**; from 0 to 1, in steps of 0.05.

### 7.14 Gear uses (`GEAR_USES`)

Uses before a tool or armour of bone or wood breaks.

Default **7**; from 1 to 20, in steps of 1.

#### 7.14.1 Fire life (`FIRE_TURNS`)

Turns a fire burns, and each wood fed to it adds.

Default **24**; from 1 to 200, in steps of 1.

#### 7.14.2 Fire panic (`FIRE_PANIC`)

Chance an animal panics, seeing a fire not its own or its herd's.

Default **0.25**; from 0 to 1, in steps of 0.05.

#### 7.14.3 Fire joy (`FIRE_JOY`)

The sense of accomplishment in lighting a fire, beside the comfort of it.

Default **2**; from 0 to 5, in steps of 0.1.

#### 7.14.4 Fire comfort (`FIRE_COMFORT`)

Comfort in sight of a fire that is its own or its herd's.

Default **0.3**; from 0 to 2, in steps of 0.05.

#### 7.14.5 Cooked food (`COOKED_FOOD`)

Times as nourishing as raw.

Default **2**; from 1 to 5, in steps of 0.1.

#### 7.14.6 Carried spoilage (`CARRIED_SPOIL`)

The chance each turn a raw portion carried goes off; cooked food keeps.

Default **0.02**; from 0 to 0.5, in steps of 0.005.

### 7.15 Weapon force (`WEAPON_FORCE`)

Times as hard an axe or club strikes.

Default **2**; from 1 to 5, in steps of 0.1.

#### 7.15.1 Stone force (`STONE_FORCE`)

Times as hard a wielded stone strikes.

Default **1.5**; from 1 to 5, in steps of 0.1.

#### 7.15.2 Armour force (`ARMOUR_FORCE`)

Times its armour that worn armour counts for.

Default **2**; from 1 to 5, in steps of 0.1.

#### 7.15.3 Dig chance (`DIG_CHANCE`)

Chance that digging finds a stone.

Default **0.1**; from 0 to 1, in steps of 0.05.

### 7.16 Bow uses (`BOW_USES`)

Shots a bow makes before it is spent.

Default **20**; from 1 to 100, in steps of 1.

### 7.17 Fire feeding joy (`FIRE_FEED_JOY`)

The satisfaction of feeding a fire, so that it burns on.

Default **1**; from 0 to 5, in steps of 0.1.

### 7.18 Seed food (`SEED_FOOD`)

How much of a fruit's portion a seed feeds.

Default **0.1**; from 0 to 1, in steps of 0.05.

#### 7.18.1 Planting joy (`PLANTING_JOY`)

The gladness of planting a seed; more if curious.

Default **1**; from 0 to 5, in steps of 0.1.

### 7.19 Harvest joy (`HARVEST_JOY`)

The gladness of picking a fruit from a plant without uprooting it.

Default **0.1**; from 0 to 5, in steps of 0.05.

#### 7.19.1 Regrowth joy (`REGROWTH_JOY`)

The gladness of seeing a plant it last picked bear fruit again.

Default **0.5**; from 0 to 5, in steps of 0.05.

### 7.20 Arable fruit (`ARABLE_FRUIT`)

Times as much fruit a plant on worked land bears each morning.

Default **2**; from 1 to 5, in steps of 1.

#### 7.20.1 Fertilized fruit (`FERTILIZED_FRUIT`)

Times as much again on worked land fed bone; the bone is spent.

Default **2**; from 1 to 5, in steps of 1.

### 7.21 Fallow years (`FALLOW_YEARS`)

Years worked land lies untended before it goes wild, its bone with it; 0, never.

Default **1**; from 0 to 10, in steps of 0.25.

### 7.22 Cooked delight (`COOKED_JOY`)

Pleasure of eating a cooked portion, times a half and its taste for cooked food.

Default **1**; from 0 to 5, in steps of 0.1.

#### 7.22.1 Raw distaste (`RAW_DISTASTE`)

Letdown of eating a raw portion once it knows the taste of cooked, times that taste.

Default **0.5**; from 0 to 5, in steps of 0.1.

#### 7.22.2 Cooking joy (`COOK_JOY`)

Pleasure of cooking a portion, times a half and its taste for cooked food.

Default **1**; from 0 to 5, in steps of 0.1.

#### 7.22.3 Smell reach (`SMELL_REACH`)

How many times as far as it sees an animal smells cooked food and a fire cooked on.

Default **3**; from 1 to 4, in steps of 0.1.

### 7.23 Taste joy (`TASTE_JOY`)

How tasty a cooked portion is, and how untasty a raw one from a pile or carried, times its appetite.

Default **1**; from 0 to 5, in steps of 0.1.

### 7.24 Grass meal (`GRASS_MEAL`)

Hunger a tuft of grass sates.

Default **18**; from 0 to 200, in steps of 1.

#### 7.24.1 Grazer on grass (`GRAZING.grazer`)

Share of the food in grass a grazer gets.

Default **1.25**; from 0 to 3, in steps of 0.05.

#### 7.24.2 Omnivore on grass (`GRAZING.omnivore`)

Share of the food in grass an omnivore gets.

Default **0.5**; from 0 to 3, in steps of 0.05.

#### 7.24.3 Grazer on plants (`BROWSING.grazer`)

Share of the food in plants a grazer gets.

Default **1.25**; from 0 to 3, in steps of 0.05.

#### 7.24.4 Omnivore on plants (`BROWSING.omnivore`)

Share of the food in plants an omnivore gets.

Default **0.75**; from 0 to 3, in steps of 0.05.

### 7.25 Uproot hunger (`UPROOT_HUNGER`)

A grazer this hungry, with food scarce, may uproot a plant.

Default **80**; from 0 to 100, in steps of 1.

#### 7.25.1 Uproot meals (`UPROOT_MEALS`)

Share of a feast on a body of the plant's size an uprooting grazer eats.

Default **0.5**; from 0 to 10, in steps of 0.05.

#### 7.25.2 Uproot bite (`UPROOT_BITE`)

Health a plant loses to being uprooted.

Default **45**; from 0 to 200, in steps of 1.

### 7.26 Uproot sorrow (`UPROOT_SORROW`)

A grazer's sorrow at tearing into a plant, times a half plus its empathy.

Default **0.5**; from 0 to 5, in steps of 0.05.

### 7.27 Carrion meat (`CARRION_MEAT`)

Meat a well-fed corpse leaves.

Default **30**; from 0 to 200, in steps of 5.

#### 7.27.1 Carrion meat for size (`CARRION_MEAT_FOR_SIZE`)

More meat, up to this for the largest.

Default **50**; from 0 to 200, in steps of 5.

## 8. Fighting

### 8.1 Strike force (`STRIKE_FORCE`)

Scales every blow.

Default **3.5**; from 0 to 10, in steps of 0.1.

#### 8.1.1 Grazer defence weapons (`GRAZER_DEFENCE_WEAPONS`)

Share of its weapons a grazer brings to defence.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 8.1.2 Omnivore defence weapons (`OMNIVORE_DEFENCE_WEAPONS`)

Share of its weapons an omnivore brings to defence.

Default **0.75**; from 0 to 1, in steps of 0.05.

#### 8.1.3 Predator weapons (`PREDATOR_WEAPONS`)

A predator's natural weapons count this many times over; an omnivore's 3/4, a grazer's half as many.

Default **3**; from 0 to 10, in steps of 0.1.

#### 8.1.4 Strike effort (`STRIKE_EFFORT`)

Food burned for each point of damage a blow deals.

Default **0.2**; from 0 to 2, in steps of 0.05.

### 8.2 Predator sight (`PREDATOR_SIGHT`)

Cells further a predator sees prey.

Default **3**; from 0 to 3, in steps of 1.

#### 8.2.1 Hunt steps (`HUNT_STEPS`)

Steps further a predator chases prey a turn.

Default **1**; from 0 to 5, in steps of 1.

### 8.3 Grazer defence strength (`GRAZER_DEFENCE_STRENGTH`)

A grazer can strike back only with strength above this.

Default **5**; from 0 to 7, in steps of 1.

### 8.4 Grazer force (`GRAZER_FORCE`)

The share of a grazer's base force in a blow it strikes, not in defence, before weapons, gear and other bonuses: it has no natural weapons to attack with.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 8.4.1 Kill meal (`KILL_MEAL`)

Hunger a kill sates.

Default **185**; from 0 to 200, in steps of 5.

#### 8.4.2 Kill meal for size (`KILL_MEAL_FOR_SIZE`)

More hunger sated, up to this for the largest prey.

Default **200**; from 0 to 200, in steps of 5.

### 8.5 Thorn force (`THORN_FORCE`)

A plant's counterblow at full weapons and size.

Default **15**; from 0 to 100, in steps of 1.

#### 8.5.1 Thorn size share (`THORN_SIZE_SHARE`)

Share of that from the plant's size; the rest its weapons.

Default **0.2**; from 0 to 1, in steps of 0.05.

#### 8.5.2 Infant thorns (`INFANT_THORNS`)

The share of a thorny plant's counterblow an infant takes, of what a grown animal would.

Default **0.25**; from 0 to 1, in steps of 0.05.

### 8.6 Pack bonus (`PACK_BONUS`)

Extra force at prey for each herd-mate also alongside it, up to three.

Default **0.5**; from 0 to 5, in steps of 0.05.

### 8.7 Joined hunt joy (`JOINED_HUNT_JOY`)

Pleasure of striking prey that a pack-mate is also alongside, times a half and its herd instinct.

Default **0.5**; from 0 to 5, in steps of 0.05.

### 8.8 Leader rally (`LEADER_RALLY`)

Share of the chance to panic or break that a follower in sight of its leader overcomes.

Default **0.5**; from 0 to 1, in steps of 0.05.

Connected: 6.44 Leader deference (`LEADER_DEFERENCE`); 6.44.3 Leader joy (`LEADER_JOY`); 6.44.4 Leader allure (`LEADER_ALLURE`).

### 8.9 Land defence (`LAND_DEFENCE`)

Share of a blow from an outsider kept off one in its own land.

Default **0.25**; from 0 to 1, in steps of 0.05.

Connected: 6.48.5 Land healing (`LAND_HEALING`); 6.48.6 Land comfort (`LAND_COMFORT`); 6.48.7 Trespass regard (`TRESPASS_REGARD`).

#### 8.9.1 Land herd (`LAND_HERD`)

How much more its herd counts in its own land, for safety and the pack's blows: 1 for twice.

Default **0.5**; from 0 to 3, in steps of 0.05.

Connected: 6.48.5 Land healing (`LAND_HEALING`); 6.48.6 Land comfort (`LAND_COMFORT`); 6.48.7 Trespass regard (`TRESPASS_REGARD`).

#### 8.9.2 Land rally (`LAND_RALLY`)

Share of the chance to panic, break or be overcome by primal fear that defending its own land overcomes, a born coward's too.

Default **0.75**; from 0 to 1, in steps of 0.05.

Connected: 6.48.8 Trespass growth (`TRESPASS_GROWTH`); 6.48.9 War enmity (`WAR_ENMITY`); 6.48.10 War morale (`WAR_MORALE`).

#### 8.9.3 War kills (`WAR_KILLS`)

Deaths at each other's hands before a war counts as clearly won (two thirds of them or more) or lost (a third or less).

Default **3**; from 1 to 100, in steps of 1.

Connected: 6.48 Land size (`LAND_SIZE`); 6.48.1 Land square (`LAND_PATCH`); 6.48.2 Land hold (`LAND_HOLD`); 6.48.3 Land memory (`LAND_MEMORY`); 6.48.4 Land count (`LAND_TURNS`).

#### 8.9.4 War nerve (`WAR_NERVE`)

How much less likely to panic a war clearly being won leaves its side, and more the losing side.

Default **0.25**; from 0 to 1, in steps of 0.05.

Connected: 6.48.8 Trespass growth (`TRESPASS_GROWTH`); 6.48.9 War enmity (`WAR_ENMITY`); 6.48.10 War morale (`WAR_MORALE`).

#### 8.9.5 War fade (`WAR_FADE`)

Share of a war's kills forgotten at each count of the lands, so recent deaths decide it and an old defeat is in time forgiven.

Default **0.1**; from 0 to 1, in steps of 0.01.

### 8.10 Fresh kill turns (`FRESH_KILL_TURNS`)

Turns after the one it died in that a body is a fresh kill.

Default **9**; from 0 to 30, in steps of 1.

#### 8.10.1 Fresh kill meals (`FRESH_KILL_MEALS`)

Times the kill meal a predator or omnivore gets eating a fresh kill.

Default **3**; from 0 to 10, in steps of 0.5.

### 8.11 Kind difference (`KIND_DIFFERENCE`)

Predators whose rings differ no more are one kind, to hunt and breed with.

Default **0.25**; from 0 to 1, in steps of 0.01.

### 8.12 Flee range (`FLEE_RANGE`)

A hunter is only worth fleeing within this many steps, unless alarmed.

Default **3**; from 0 to 12, in steps of 1.

#### 8.12.1 Herd vigilance (`HERD_VIGILANCE`)

How far a close herd shrinks that range.

Default **0.5**; from 0 to 1, in steps of 0.05.

### 8.13 Prey switching (`SWITCH_SHARE`)

A prey is hunted while it is at least this share of the food.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 8.13.1 Omnivores' last resort (`OMNIVORE_PREDATOR_FOOD`)

An omnivore hunts predators only while no more grazers, plants and grassy cells than this are near.

Default **0**; from 0 to 100, in steps of 1.

## 9. Territory

### 9.1 Territory ramp (`TERRITORY_RAMP`)

Rivals past the limit that bring rivalry to its full strength.

Default **3**; from 1 to 20, in steps of 1.

#### 9.1.1 Rival unease (`RIVAL_UNEASE`)

Discomfort of a fully overcrowded territory.

Default **0.3**; from 0 to 5, in steps of 0.05.

#### 9.1.2 Pack rivalry (`PACK_RIVALRY`)

How much a predator's pack-mates count as rivals crowding its territory.

Default **0**; from 0 to 1, in steps of 0.05.

#### 9.1.3 Rivalry wear (`RIVALRY_WEAR`)

How much an overcrowded territory wears down reluctance to kill its own species.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 9.1.4 Rival kill joy (`RIVAL_KILL_JOY`)

Relief at killing a rival in an overcrowded territory.

Default **0.5**; from 0 to 5, in steps of 0.05.

### 9.2 Predator rivals, cruel (`TERRITORY.predator.fewest`)

Rivals the least empathetic predator tolerates.

Default **1**; from 0 to 50, in steps of 1.

#### 9.2.1 Predator rivals, kind (`TERRITORY.predator.most`)

Rivals the most empathetic predator tolerates.

Default **5**; from 0 to 50, in steps of 1.

#### 9.2.2 Predator rivalry (`TERRITORY.predator.strength`)

How strongly overcrowding moves a predator.

Default **1**; from 0 to 2, in steps of 0.05.

### 9.3 Omnivore rivals, cruel (`TERRITORY.omnivore.fewest`)

Rivals the least empathetic omnivore tolerates.

Default **2**; from 0 to 50, in steps of 1.

#### 9.3.1 Omnivore rivals, kind (`TERRITORY.omnivore.most`)

Rivals the most empathetic omnivore tolerates.

Default **10**; from 0 to 50, in steps of 1.

#### 9.3.2 Omnivore rivalry (`TERRITORY.omnivore.strength`)

How strongly overcrowding moves an omnivore.

Default **0.5**; from 0 to 2, in steps of 0.05.

## 10. Herding

### 10.1 Herd spread (`HERD_SPREAD`)

How far from the herd's middle a fearless animal is content.

Default **3**; from 0 to 12, in steps of 1.

#### 10.1.1 Herd joy (`HERD_JOY`)

Pleasure of being among three or more herd-mates.

Default **0.15**; from 0 to 5, in steps of 0.05.

#### 10.1.2 Herd loneliness (`HERD_LONELINESS`)

Unease at having no herd-mate in range.

Default **0.1**; from 0 to 5, in steps of 0.05.

#### 10.1.3 Solitary omnivores (`SOLITARY_OMNIVORES`)

1: omnivores live alone, in neither herd nor pack; 0: they herd with their own kind.

Default **1**; from 0 to 1, in steps of 1.

### 10.2 Infant herding (`INFANT_HERDING`)

How much more an infant values keeping to its kind; it herds once a cell from their middle.

Default **1**; from 0 to 5, in steps of 0.05.

#### 10.2.1 Nursing reach (`NURSING_REACH`)

Each turn, a grown animal feeds the hungriest infant of its kind this close.

Default **2**; from 0 to 10, in steps of 1.

#### 10.2.2 Nursing limit (`NURSING_LIMIT`)

A grown animal feeds infants only while its own hunger stays below this.

Default **80**; from 0 to 100, in steps of 5.

### 10.3 Herd safety (`HERD_SAFETY`)

How much three close herd-mates calm fear.

Default **0.6**; from 0 to 1, in steps of 0.05.

#### 10.3.1 Herd defence joy (`HERD_DEFEND_JOY`)

Pleasure of striking back at a herd-mate's attacker.

Default **0.5**; from 0 to 5, in steps of 0.05.

## 11. Plants

### 11.1 Seeding, least fertile (`SEED_CHANCE.0`)

A well-fed plant's chance each turn of seeding.

Default **0.03**; from 0 to 1, in steps of 0.01.

#### 11.1.1 Seeding, most fertile (`SEED_CHANCE.1`)

A well-fed plant's chance each turn of seeding.

Default **0.1**; from 0 to 1, in steps of 0.01.

#### 11.1.2 Seed cost (`SEED_COST`)

Hunger seeding costs a plant.

Default **20**; from 0 to 100, in steps of 1.

#### 11.1.3 Seed hunger (`SEED_HUNGER`)

A plant hungrier than this doesn't seed.

Default **40**; from 0 to 100, in steps of 1.

#### 11.1.4 Seedling hunger (`SEEDLING_HUNGER`)

Hunger a seedling starts with.

Default **50**; from 0 to 100, in steps of 1.

#### 11.1.5 Seed reach (`SEED_REACH`)

How far, in cells, a seed may land from its plant.

Default **5**; from 1 to 9, in steps of 1.

#### 11.1.6 Light radius (`LIGHT_RADIUS`)

Plants compete for light with plants within this many cells.

Default **3**; from 0 to 12, in steps of 1.

#### 11.1.7 Light limit (`LIGHT_LIMIT`)

This many such plants stop a plant seeding.

Default **3**; from 1 to 50, in steps of 1.

#### 11.1.8 Plant crowding (`PLANT_CROWDING`)

A seed doesn't take where more plants grow within two cells.

Default **3**; from 0 to 50, in steps of 1.

### 11.2 Avian seed reach (`AVIAN_SEED_REACH`)

An avian plant's seeds land this many times as far.

Default **2**; from 1 to 4, in steps of 0.25.

## 12. Climate

### 12.1 Arctic (`ARCTIC_SHARE`)

Share of the map's height that is arctic, at the top and again at the bottom.

Default **0.2**; from 0 to 0.5, in steps of 0.05.

#### 12.1.1 Equator (`EQUATOR_SHARE`)

Share of the map's height across its middle that is the equator.

Default **0.2**; from 0 to 1, in steps of 0.05.

#### 12.1.2 Polar caps (`POLAR_SHARE`)

Share of the map's height at the top and again at the bottom that is very cold all year.

Default **0.025**; from 0 to 0.5, in steps of 0.005.

#### 12.1.3 Winter equator (`WINTER_EQUATOR_SHARE`)

Share of the map's height at the equator's end away from the winter that stays hot through both winters.

Default **0.05**; from 0 to 1, in steps of 0.01.

#### 12.1.4 Climate hunger (`CLIMATE_HUNGER`)

Times the food needed a step or more from home (a plant, only too hot).

Default **2**; from 1 to 5, in steps of 0.1.

#### 12.1.5 Climate damage (`CLIMATE_DAMAGE`)

Share of full health lost a turn for each step from home (plants from two).

Default **0.025**; from 0 to 1, in steps of 0.01.

#### 12.1.6 Resting exposure (`RESTING_EXPOSURE`)

Times the weather's harm to an animal that chose to rest out of its temperature (not asleep or spent).

Default **2**; from 1 to 10, in steps of 0.1.

#### 12.1.7 Weather grace (`WEATHER_GRACE`)

Days an animal endures out of its temperature, uneasy but unharmed, before the weather harms it (half for the least resilient, half again for the most; twice as long weather it is hardy to).

Default **4**; from 0 to 30, in steps of 0.5.

#### 12.1.8 Plant climate yield (`PLANT_CLIMATE_YIELD`)

Share of its food a plant gives out of its temperature.

Default **0**; from 0 to 1, in steps of 0.05.

#### 12.1.9 Grass dieback (`GRASS_DIEBACK`)

Where it isn't medium, turns for each tuft of a cell's grass to die.

Default **60**; from 1 to 500, in steps of 1.

### 12.2 Season turning (`SEASON_TURNING`)

Share of a winter season its wave takes to wash in, and again to ebb out at its end (at most half).

Default **0.4**; from 0 to 0.5, in steps of 0.05.

#### 12.2.1 Wave band (`WAVE_BAND`)

Rows in each band of a season's wave.

Default **2**; from 1 to 20, in steps of 1.

#### 12.2.2 Wave front (`WAVE_FRONT`)

Bands at a wave's front, part way between the old weather and the new.

Default **4**; from 1 to 30, in steps of 1.

### 12.3 Seed viability (`SEED_VIABILITY`)

Years a seed waits for its temperature before it dies.

Default **0.75**; from 0 to 5, in steps of 0.05.

### 12.4 Starting tolerance (`STARTING_TOLERANCE`)

Chance a random starting genome carries each tolerance flag.

Default **0.1**; from 0 to 1, in steps of 0.05.

#### 12.4.1 Hardiness (`HARDINESS`)

Share of exposure's extra hunger and damage an animal tolerant of it suffers.

Default **0.5**; from 0 to 1, in steps of 0.05.

#### 12.4.2 Frailty (`FRAILTY`)

Share of exposure's extra hunger and damage an animal tolerant of the other extreme suffers.

Default **2**; from 1 to 4, in steps of 0.05.

## 13. Experiences

### 13.1 Kill: aggression (`EXPERIENCES.kill.aggression`)

How kill shifts aggression, as a share of its range.

Default **0.04**; from -0.5 to 0.5, in steps of 0.0005.

#### 13.1.1 Kill: fear (`EXPERIENCES.kill.fear`)

How kill shifts fear, as a share of its range.

Default **-0.02**; from -0.5 to 0.5, in steps of 0.0005.

#### 13.1.2 Kill: intellect (`EXPERIENCES.kill.intellect`)

How kill shifts intellect, as a share of its range.

Default **0.004**; from -0.5 to 0.5, in steps of 0.0005.

### 13.2 Hit: aggression (`EXPERIENCES.hit.aggression`)

How hit shifts aggression, as a share of its range.

Default **0.0067**; from -0.5 to 0.5, in steps of 0.0005.

### 13.3 Stung: aggression (`EXPERIENCES.stung.aggression`)

How stung shifts aggression, as a share of its range.

Default **-0.0133**; from -0.5 to 0.5, in steps of 0.0005.

#### 13.3.1 Stung: fear (`EXPERIENCES.stung.fear`)

How stung shifts fear, as a share of its range.

Default **0.0133**; from -0.5 to 0.5, in steps of 0.0005.

### 13.4 Wounded: fear (`EXPERIENCES.wounded.fear`)

How wounded shifts fear, as a share of its range.

Default **0.0333**; from -0.5 to 0.5, in steps of 0.0005.

#### 13.4.1 Wounded: aggression (`EXPERIENCES.wounded.aggression`)

How wounded shifts aggression, as a share of its range.

Default **-0.0067**; from -0.5 to 0.5, in steps of 0.0005.

#### 13.4.2 Wounded: intellect (`EXPERIENCES.wounded.intellect`)

How wounded shifts intellect, as a share of its range.

Default **-0.002**; from -0.5 to 0.5, in steps of 0.0005.

### 13.5 Wounded alone: sociability (`EXPERIENCES.wounded_alone.sociability`)

How wounded alone shifts sociability, as a share of its range.

Default **0.02**; from -0.5 to 0.5, in steps of 0.0005.

### 13.6 Bred: libido (`EXPERIENCES.bred.libido`)

How bred shifts libido, as a share of its range.

Default **0.02**; from -0.5 to 0.5, in steps of 0.0005.

#### 13.6.1 Bred: sociability (`EXPERIENCES.bred.sociability`)

How bred shifts sociability, as a share of its range.

Default **0.0133**; from -0.5 to 0.5, in steps of 0.0005.

#### 13.6.2 Bred: intellect (`EXPERIENCES.bred.intellect`)

How bred shifts intellect, as a share of its range.

Default **0.002**; from -0.5 to 0.5, in steps of 0.0005.

### 13.7 Bred into hunger: libido (`EXPERIENCES.bred_into_hunger.libido`)

How bred into hunger shifts libido, as a share of its range.

Default **-0.0533**; from -0.5 to 0.5, in steps of 0.0005.

### 13.8 Starving: appetite (`EXPERIENCES.starving.appetite`)

How starving shifts appetite, as a share of its range.

Default **0.0033**; from -0.5 to 0.5, in steps of 0.0005.

#### 13.8.1 Starving: patience (`EXPERIENCES.starving.patience`)

How starving shifts patience, as a share of its range.

Default **-0.0033**; from -0.5 to 0.5, in steps of 0.0005.

#### 13.8.2 Starving: intellect (`EXPERIENCES.starving.intellect`)

How starving shifts intellect, as a share of its range.

Default **-0.0005**; from -0.5 to 0.5, in steps of 0.0005.

### 13.9 Foraged meal: patience (`EXPERIENCES.foraged_meal.patience`)

How foraged meal shifts patience, as a share of its range.

Default **0.0067**; from -0.5 to 0.5, in steps of 0.0005.

### 13.10 Discovery: curiosity (`EXPERIENCES.discovery.curiosity`)

How discovery shifts curiosity, as a share of its range.

Default **0.01**; from -0.5 to 0.5, in steps of 0.0005.

#### 13.10.1 Discovery: intellect (`EXPERIENCES.discovery.intellect`)

How discovery shifts intellect, as a share of its range.

Default **0.002**; from -0.5 to 0.5, in steps of 0.0005.

### 13.11 Ambushed: curiosity (`EXPERIENCES.ambushed.curiosity`)

How ambushed shifts curiosity, as a share of its range.

Default **-0.02**; from -0.5 to 0.5, in steps of 0.0005.

#### 13.11.1 Ambushed: fear (`EXPERIENCES.ambushed.fear`)

How ambushed shifts fear, as a share of its range.

Default **0.0133**; from -0.5 to 0.5, in steps of 0.0005.

### 13.12 Defended: fear (`EXPERIENCES.defended.fear`)

How defended shifts fear, as a share of its range.

Default **-0.0133**; from -0.5 to 0.5, in steps of 0.0005.

#### 13.12.1 Defended: aggression (`EXPERIENCES.defended.aggression`)

How defended shifts aggression, as a share of its range.

Default **0.0067**; from -0.5 to 0.5, in steps of 0.0005.

#### 13.12.2 Defended: intellect (`EXPERIENCES.defended.intellect`)

How defended shifts intellect, as a share of its range.

Default **0.002**; from -0.5 to 0.5, in steps of 0.0005.

### 13.13 Panicked: fear (`EXPERIENCES.panicked.fear`)

How panicked shifts fear, as a share of its range.

Default **0.0067**; from -0.5 to 0.5, in steps of 0.0005.

#### 13.13.1 Panicked: intellect (`EXPERIENCES.panicked.intellect`)

How panicked shifts intellect, as a share of its range.

Default **-0.001**; from -0.5 to 0.5, in steps of 0.0005.
