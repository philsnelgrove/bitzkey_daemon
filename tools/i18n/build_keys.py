"""The English side of the translation catalogs: every piece of text the web page can show.

    python tools/i18n/build_keys.py COLLECTED.json [COLLECTED.json ...]

COLLECTED files are what web/i18n.js gathered with ?i18n=collect (window.bitzkeyMissing) while
walking the page. They are merged with the patterns below -- messages built from parts, which a
walk shows only some of -- and the rarer messages read from the code, into tools/i18n/keys.json.

Keys: the English as shown, with {#} for numbers and {*} for words that change (translated in turn).
"""
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).parent

# Messages built from parts (src/daemon/Api.cpp, Views.cpp, web/*.js), and text a walk rarely meets.
PATTERNS = [
    # Names and genomes
    "#{#}  {*}", "#{#} {*}", "DOT {*} RING {*} SOCIAL {*}", "A {*}.  Health {#}.  Metabolism {#}.",
    "Moves {#} cell a step.  Sees {#} cells.", "Moves {#} cells a step.  Sees {#} cells.",
    "At home where it is {*}; elsewhere it withers.", "At home where it is {*}.",
    "At home where it is {*}.  Tolerates heat.", "At home where it is {*}.  Tolerates cold.",
    "At home where it is {*}.  Tolerates heat and cold.",
    "At home where it is {*}; elsewhere it withers.  Tolerates heat.",
    "At home where it is {*}; elsewhere it withers.  Tolerates cold.",
    "At home where it is {*}; elsewhere it withers.  Tolerates heat and cold.",
    "Tolerates heat.", "Tolerates cold.", "Tolerates heat and cold.",
    "LAST ACTION: {*}", "LAST ACTION: {*}     MEAT LEFT {#}", "HOME {*}", "HERE {*}", "{*} FEEBLE",
    "AGE {#} / ~{#} FEEBLE    GENERATION {#}    CHILDREN {#}", "LIFESPAN ~{#}", "CHANCES  {*}",
    "born", "now", "neurodiv.", "burn", "hot", "medium", "cold", "v. cold", "very cold",
    "burn {#}", "stomach {#}", "herd instinct {#}", "changed {#}", "lifespan {#} turns", "health {#}%",
    # The catalog and exports
    "Added {*} to the catalog.", "Added {*} to the catalog ({#} already there).",
    "Added {*} to the catalog. {*}", "Added {*} to the catalog ({#} already there). {*}",
    "{*} already in the catalog.", "{*} already in the catalog. {*}",
    "{#} Bitzkey", "{#} Bitzkeys", "{#} selected Bitzkey", "{#} selected Bitzkeys",
    "{*} from the export list", "{*} ({#} as they were when added)", "{*} ({#} dead, so without a brain)",
    "Saved {*} to exports/{*}", "Copied {*} to the clipboard.", "Nobody is alive to export.",
    "{#} dead left out: their brains are gone.", "Nothing on the export list to catalog.",
    "Only living Bitzkeys can be catalogued, with their brains.", "Designed and added to the catalog.",
    "Removed {#} from the catalog.", "Removed from the catalog.", "Nothing chosen from the catalog to deploy.",
    "Deploy on the map.", "Deployed {#} at ({#}, {#}).  RIGHT-CLICK or ESC  stop",
    "DEPLOY: click or drag on the map to place it.  RIGHT-CLICK or ESC  stop",
    "DEPLOY: click or drag on the map to place them.  RIGHT-CLICK or ESC  stop",
    "DEPLOY: click or drag on the map to place {#} of it.  RIGHT-CLICK or ESC  stop",
    "DEPLOY: click or drag on the map to place {#} of them.  RIGHT-CLICK or ESC  stop",
    "{*} must be up to 8 hex digits", "Couldn't add to the catalog: {*}", "Couldn't save the report: {*}",
    "Removed #{#} from the export list ({#} on it).", "Added #{#} to the export list ({#} on it).",
    "Cleared {#} from the export list.", "{#} Bitzkeys added to export.", "{#} added to export.",
    "Copied #{#}'s genome and brain to the clipboard.", "A new world with {*}.",
    "Brain report saved to reports/{*}", "{#} KB", "{#} MB",
    # The world, history and speed
    "Speed {*}.", "SPEED {*}", "The history reaches back only to turn {#}; moved there.", "Moved to turn {#}.",
    "A new world, from the same settings.", "A new world has begun.",
    "The simulation ended at turn {#}: {*}.", "every animal has died", "the world is full, with no cell left free",
    "Died at turn {#}: {*}.", "Died at turn {#}: {*} by #{#} {*}, {*}.", "Dead: only its genome is known.",
    "No such Bitzkey.", "Only a living animal has a brain to inspect.", "Bad request: {*}", "unknown action {*}",
    "The daemon answered {#}: {*}", "The daemon answered {#}",
    "This world now plays by {#} rule changed from the defaults. Paused: SPACE plays on.",
    "This world now plays by {#} rules changed from the defaults. Paused: SPACE plays on.",
    "{#} rule changed", "{#} rules changed",
    "{#} advanced rule changed from the defaults.", "{#} advanced rules changed from the defaults.",
    "thinking on the GPU", "thinking on {*}", "thinking on the CPU: {*}", "thinking on the CPU: the GPU failed ({*})",
    "GPU {*}", "gpu {*}",
    # Saved worlds
    "Name the world with up to 60 letters, digits, - and _.", "A world named {*} is already saved; choose another name.",
    "Saved the world at turn {#} as {*}.", "No such world.", "Deleted {*}.", "Couldn't load {*}: {*}",
    "Loaded {*} at turn {#}. Paused: SPACE plays on.", "Loading {*}, please be patient...", "Delete {*}", "Delete {*}?",
    "No such file.",
    # A new world's settings and imports
    "{*} is needed", "Can't import: {*}", "{*} is too large to import.", "Loaded {*}.",
    "{*} ready to import ({*}; {#} with saved brains, {#} with saved temperaments), each spawning as a group of {#}-{#}",
    "{*} ready to import ({*}; {#} with saved brains, {#} with saved temperaments), each spawning as a group of {#}-{#}. Only the first {#} of {#} are used",
    "{#} plant", "{#} plants", "{#} grazer", "{#} grazers", "{#} omnivore", "{#} omnivores", "{#} predator", "{#} predators",
    "Added a designed {*} to the import list.", "high-{*} {*}", "mid-{*} {*}", "low-{*} {*}", "SORT  {*}",
    "A year must be at least 4 turns.", "World dimensions must both be at least 10 cells.",
    "Population cannot be negative.", "Grass density must be between 0 and 100 percent.",
    "Mutation chances must be between 0 and 100 percent.",
    # Deaths and what animals were doing (death notes, the death report)
    "old age", "seeded to death", "bred to death", "starved", "killed", "found it eaten", "was spurned",
    "sought plants", "sought carrion", "sought a kill",
    "went north", "went south", "went east", "went west", "walked north", "walked south", "walked east", "walked west",
    "go N", "go S", "go E", "go W", "walk N", "walk S", "walk E", "walk W",
    # The brain inspector's situations
    "Fed and safe, a willing mate beside them", "Fed and safe, a willing mate in sight",
    "Hungry, grass underfoot, a willing mate beside them", "Hungry, grass in sight", "Hungry, a plant in sight",
    "Hungry, carrion in sight", "Hungry, a fresh kill in sight", "Peckish, prey in sight", "A hunter in sight",
    "Wounded, a dangerous hunter beside them", "Wounded, the attacker still beside them",
    "Crowded territory, a rival beside them", "Alone, its herd in range", "A herd-mate struck close by",
    "Peckish, frail prey beside them", "Too cold, warmer to the south", "Too cold, warmer to the north",
    "Too hot, cooler to the north", "Too hot, cooler to the south", "Far too cold, warmer to the south",
    "At home, cold coming from the north", "At home, cold coming from the south", "At home, the same all around",
    "Starving, food scarce, a plant beside them", "Nothing around", "Brains of #{#}, {*}", "SITUATION",
    # Reading files (shown when an import or a saved world can't be read)
    "\"mind\" must map trait names to numbers.", "Expected a Bitzkey with \"ring\" and \"dot\", found: {*}",
    "Not valid JSON at line {#}, column {#}", "This isn't a Bitzkey world file.", "The world file is cut short.",
    "The world file's numbers are cut short.", "The world file's whole numbers are cut short.",
    "The world file is damaged ({*})", "The world file is incomplete ({*})",
    "This world file is format {#}; this version reads format {#}.",
    "This world was saved by a version of Bitzkey whose brains sense or act differently.",
    "The world file's grass doesn't fit its map.", "The world file's deaths are damaged.",
    "couldn't write it", "couldn't write it all; is the disk full?", "can't open it", "can't read it",
    "The catalog {*} can't be read.", "The catalog {*} can't be read: {*}", "The catalog {*} holds something that isn't a Bitzkey.",
    "Couldn't write the catalog {*}", "Temperatures run from 0 to 3: {*}", "Neurodivergence runs from 0 to 15: {*}",
    "Traits run from 0 to 7: {*}", "Unknown traits: {*}", "Genome version {*} is not one this Bitzkey knows (1 to 3).",
    "The brain is incomplete or not numeric ({*})", "The brain has no hidden layers.",
    "The brain's weights don't match its listed senses and actions.",
    "This browser won't let the page reach the clipboard here. Press CTRL+C to copy, then close.",
    "Copy these Bitzkeys", "Select a Bitzkey first, then add it to export.",
    "Only a living Bitzkey can be added to export.", "Nothing has been added to export yet.",
    "Select a Bitzkey and press ADD TO EXPORT (or A).", "Select a living Bitzkey first, then add it to the catalog.",
    "Click a Bitzkey first, then press C to copy its genome.",
    "Pick living Bitzkeys on the graph first, then start a new world with them.", "Deploying stopped.",
    "The catalog is empty.", "Add Bitzkeys from a running world: ADD TO CATALOG in the sidebar, the export list or the colour graph, or press K.",
    "Remove from the catalog?", "It will be gone from the catalog for good.", "They will be gone from the catalog for good.",
    "KEEP", "DEPLOY {*}", "ADD {*} TO IMPORT", "ADD {#} TO IMPORT", "DEPLOY {#}",
    "Use up to 60 letters, digits, - and _.", "It is deleted from the daemon for everyone, and can't be brought back.",
    "DELETE", "Nothing selected", "{#} selected", "The living sorted by {*}, lowest at the bottom.     {*}",
    "Rewound to turn {#} of {#}.     {*}", "TURNS {#} - {#}  (ZOOM x{#})", "  (HEIGHT x{#})",
    "{*} died; they export as they were when added.", "{#} died; they export as they were when added.",
    "Every death in this world up to turn {#}: {#} in all, by cause and species, who killed whom and what the killers were doing, and the deadliest killers.",
    "Nothing has killed anything yet.", "Who killed whom: {#} killed, by the species of the dead (rows) and of their killers (columns)",
    "What the killers were doing, by the killers' species", "The deadliest killers, {#} of {#}",
    "THE DEAD", "ACTIVITY", "KILLER", "SPECIES", "KILLS", "DOING", "CAUSE", "ALL",
    "Its chance of choosing each action open to it, in each standard situation, with its temperament now. The likeliest is lit; – marks an action not open. The world is paused.",
    "not enough chances yet", "HISTORY  {#}/{#}", "Language",
    "MILD", "NORTHERN WINTER", "SOUTHERN WINTER",
    # What an animal last did, how it died, and what a killer was doing (src/core/Simulation.cpp)
    "alerted", "ate", "attacked", "browsed", "browsing", "courted", "defended", "defending", "exhausted", "feasted",
    "foraged", "froze", "herded", "hunted", "hunting", "mated", "missed", "photosynthesized", "pounced", "rested",
    "revisited", "scavenged", "seeded", "spines", "uprooted", "uprooting", "withering", "wounded", "overheated",
    "eaten", "attacking", "fled", "grazer {#}", "predator {#}",
    "UP/DOWN  choose   +/- buttons adjust (SHIFT x{#})   ENTER  start   ESC  cancel",
    "{*}     |     #{#}  {*}  alive   ring {*}   dot {*}   social {*}",
    "{*}     |     #{#}  {*}  {*}   ring {*}   dot {*}   social {*}",
    "died: {*}", "died: {*} by #{#} {*}, {*}", "dead",
    "{*}   turn {#}   {*}", "{*}   turn {#}", "designed {*}", "{*}     {*}",
]

# Text that isn't words to translate: the page's own plumbing, met while collecting.
JUNK = re.compile(r"\[object |^,|^[\d{}#.,:\s%/x-]+$|^DOT \{#\}|^#\{#\} \w+ +turn |reports/brain-report|exports/bitzkeys-")
# The inspector's monospaced rows: translated name by name before padding (web/inspector.js).
ROWS = re.compile(r"^[a-z.]+ {2,}\{#\}|^(MIND|SOCIAL) {2,}born")


def main(paths):
    keys = {}
    for path in paths:
        for text in json.load(open(path, encoding="utf8")):
            text = text.strip()
            if text and not JUNK.search(text) and not ROWS.search(text):
                keys[text] = None
    for text in PATTERNS:
        keys.setdefault(text, None)
    ordered = sorted(keys, key=lambda k: (k.lower(), k))
    out = HERE / "keys.json"
    json.dump(ordered, open(out, "w", encoding="utf8"), indent=0, ensure_ascii=False)
    print(f"{len(ordered)} keys -> {out}")


main(sys.argv[1:])
