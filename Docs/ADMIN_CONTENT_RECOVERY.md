# Admin client content recovery

This pass uses the local build-7148 admin decompile, build-7160 client reference,
the installed Legions `portal.dat` / English string tables, and the pinned
AC2_Rusted binary readers. The DATs and source captures remain unchanged.

## Changes enabled in the server

- Native gateway and road-sign script classes now enter the readable-sign service.
  Within four meters, use sends the original localized Description StringInfo.
  Missing descriptions fall back to examination without pretending to run a quest.
  Cragstone's gateway text describes Lake Blessed, the River Prosper and Kingdom
  Shrines. It no longer reaches the generic unimplemented-action response.
- Examination includes available native descriptions, including NPC biographies
  such as Orven's Arwic Factotum description. This does not invent a conversation
  or grant a quest merely because an NPC has descriptive text.
- `native-trophies.json` supplies 482 explicit trophy slots for 333 creature
  templates. Runtime validates the complete EntityDesc inheritance fingerprints,
  item records, visuals and item transport before admitting a rule. Native
  `NPC_TrophyDropN_Rate` probabilities replace guaranteed copies of that same trophy
  in observed bundles. Each slot rolls independently once per corpse; loot-proof
  creatures still drop nothing. Thirty-six of these templates appear in the
  current imported combat catalog.

The recovery pass did not recover random item affixes, every item stat,
special-trophy eligibility, or the full retail treasure generator. Capture
frequency is not a retail probability. The separately authorized inferred
implementation below now supplies variable generic treasure and armor rolls.

## Inferred gameplay pack v1 (2026-10-30)

The user authorized best-guess quest/spawn/loot implementation using the content
recovery section. `inferred-content-v1.json` separates those authored decisions
from unchanged DATs, captures, SQL and reference catalogs. This is playable
reconstruction, not a claim to have recovered retail server scripts.

### Enabled quests and population

- **88 quest profiles**: 55 counted hunts, 29 boss hunts, three hunt elixirs and
  Gambling Debt. Native death effects identify hunt bestowers and eligible
  targets; native phase text supplies counts. Each profile preserves its source
  package and any matched guide URL/hash. Generic native "Incursion" titles
  are disambiguated with their targets.
- **143 camp groups**: 129 from old guide/SQL coordinates and 14 near compatible
  captured creatures. These cover 61 hunt/boss profiles; the remaining profiles
  can advance wherever their native starter/target templates are already placed,
  but do not imply newly located camps for every quest.
- **702 new accepted placements**: 676 creature candidate slots plus 26 NPCs.
  Native terrain/collision checks reject nine candidate slots. The resulting
  groups allow at most **548 simultaneous creature lifetimes**, including
  lootable corpses. All 702 pass installed entity/visual/cell compatibility.
- Regular camps nominate one starter and four target slots, normally with a
  four-child cap; boss camps nominate a starter and boss, with a two-child cap.
  Caps shrink when grounding rejects candidates. A missing starter rejects the
  camp. The controller owns initial creation, corpse retirement and regeneration;
  these are not static creatures inserted on every player login.
- The coarse guide point is searched within 32 metres for dry, walkable native
  ground with a clear route through the camp and six-metre separation from
  existing server placements. Exact retail camp centres, facing, densities and
  generator regions remain unknown. Child regeneration is inferred at 120 seconds.
  Native perception, wandering controls and homesickness are retained for the
  169 creature templates used by this pack. No foliage placement is changed.
- The additional NPC placements remain noncombat. Their placement alone does not
  enable dialogue trees, vendor stock or arbitrary scripted interactions.

Kills bestow/advance solo hunts and pay completion XP once. Guide reward/level
values are used when matched; otherwise the explicit fallback is at least
100 XP, or 20 XP per target level per required kill, with a minimum level of
target level minus three. Missing timers default to a one-hour objective and
20-hour repeat. A detail page wins a conflicting index timer; this choice is
recorded rather than treated as certain retail behavior. The native trophy
probabilities remain authoritative where available.

Beetle Slayer, Wasp Bane and Armoredillo Doom consume their owned native hunt
elixir and create a journal objective atomically. They are quest starters, not
health potions. Gambling Debt uses the guide's level 11 requirement, Kanten's
Bag, delivery to Orven in Arwic, two-hour limit/repeat and 1,000 XP reward.
Double-click Orven or target him while using the bag. A successful hand-in consumes
the bag and pays only once. Expired jobs can be renewed with Kanten, reusing the
existing bag. The source describes a random Kanten quest set; only this option
is implemented. Because the missing Kanten1/Kanten2 definitions do not establish
which ID is Gambling Debt, it uses reconstruction ID `4000fe01`.

### Treasure policy and persistence

Creatures with a supported native combat level now use an explicitly inferred
variable treasure policy instead of repeating an entire observed bundle:
one sampled observed item at 35%, plus independent 70% and 25% rolls from the
supported apparel/restorative pool. Duplicate item definitions are suppressed.
Unsupported quest/special items are not inserted into the generic pool. Native
trophy slots are then applied at their recovered rates, and loot-proof creatures
remain empty. These percentages are AC2E tuning, not extracted retail weights.

Supported armor gains persistent rating, treasure level and native palette tint.
Rating varies from 75–125% of a known base, or an inferred base of twice creature
level plus two where no base exists. Inspection shows the roll, equipped defense
uses it, and the native appearance receives its tint. Original equipment models
and race restrictions remain in force. This does not implement the full retail
weapon/jewelry affix system. Existing unrolled items keep their historical values.

`q6` journal events bind hunt credit, timestamps and rewards; `q7` binds elixir
consumption and acceptance; `q8` binds delivery inventory/reward/state exchanges.
Rolled item grants use `G`, distinct from historical `g` grants. Old journal
events retain their old meaning. Corpse loot rolls once, reopening never rerolls,
and claimed rolls survive pickup, equipment changes and restart. Keep v1 rules
immutable after release; changed rewards/quest semantics need a new journal
policy version and migration rather than editing historical replay behavior.

### Regeneration and verification

From the repository root, regenerate the separate pack without editing references:

```powershell
python AC2E/emulator/tools/author_recovery_content.py
python AC2E/emulator/tools/import_creature_combat.py "Asheron's Call 2 - Legions/portal.dat" AC2E/emulator/content/inferred-creature-entities.txt AC2E/emulator/content/inferred-creature-combat-v1.tsv
cargo run --release --manifest-path AC2E/emulator/Cargo.toml --example population_authoring -- "Asheron's Call 2 - Legions" AC2E/emulator/content/inferred-content-v1.json AC2E/emulator/content/inferred-world-v1.tsv AC2E/emulator/content/inferred-world-v1.report.json AC2E/data/world/legions-world.tsv
python Network/prepare_content.py "Asheron's Call 2 - Legions" AC2E/data/world/legions-world.tsv
./AC2E/Build-AC2E.ps1
```

The grounding report records the source/manifest hashes, existing outdoor-world
hash, rejected slots and exact accepted group roster. Preparation rejects stale
or mismatched inferred manifests. Tests cover all quest completions, timers,
levels, replay-safe rewards/consumption, rolled armor persistence/defense/tint,
varied Vermin treasure, native ground and noncombat roles, group capacities and
starter availability. `Network/test_inferred_content.py` exercises real UDP
delivery/targeted hand-in, elixir use, a five-kill incursion across two camps,
native journals and a full server restart using a disposable test account store.

## Regional wilderness pack v1 (2026-10-31)

The previous world followed captured travel routes and individual quest camps;
it did not contain the missing regional wilderness generators. This pass joins
the locally archived [AC2 wiki locations](https://20oz.net/index.php/Locations.html)
and regional rosters (for example [Arwic Outlands](https://20oz.net/index.php/Arwic_Outlands.html),
[Esper Reaches](https://20oz.net/index.php/Esper_Reaches.html),
[Kydi Delta](https://20oz.net/index.php/Kydi_Delta.html) and
[Rings of Oriad](https://20oz.net/index.php/Rings_of_Oriad.html)) to the recovered
Pyreal AC2UtilSQL `ringways` and `towns` rows in `reference-tables.json`.
Those SQL rows contain geography; they are not a recovered spawn database.
Article URLs, HTML hashes, SQL source hashes, native template-cache hash and
the source catalog hash are retained in `regional-content-v1.json` and its
grounding report. This pass adds no guessed NPC dialogue or quest state machines.

The new separate pack admits **3,495 native-grounded creature slots**, using
**326 native creature templates**, in **2,071 spawner groups**. The source roster
matched 329 eligible templates; three had no accepted placement. Sixty-five
regional creature names with no eligible ordinary native profile at the guide's
level were withheld. Protected NPCs, loot-proof/script-only actors and known
single-kill quest targets are excluded from this generic wilderness pool.

Coverage spans:

- Osteth: Arwic Outlands, Esper Reaches, Lost Wish, Mad Crone Mountains, Cobalt
  Sweeps, Drudge Citadel and Prosper Marches.
- Omishan: Kydi Delta, Ariaki River Basin, Lumari Spill Isles, Tou-Tou Peninsula,
  Naderu Greenwood, Farali Wasteland and Vesayen Islands.
- Linvak: Rings of Oriad, Stoneshadow Plains, Thusik Delta, Nepeth Strata,
  Winterhollows, Mount Malthabbor and Gevoth.

Prepared world placements increase from **6,306 to 9,801**. Outdoor landblocks
containing server content increase from **1,991 to 3,833**, including **1,842
newly populated blocks**. The complete world has 8,612 managed creature points,
8,056 outdoors in 3,623 blocks, and 2,214 capacity-limited camp groups. These
counts include the earlier packs; they do not imply complete retail coverage.

### Explicit inference and validation policy

The convex hull of a region's documented ringway anchors bounds its candidate
area; it is not the missing retail region polygon. Overlapping hulls select the
closest documented ringway centre. A stable hash chooses one jittered candidate
camp per 160-metre landblock and one eligible creature from that region's roster.
The source's `group` marker means elite/fellowship difficulty, so the authored
capacity is one elite or two ordinary creatures, not a supposed retail count.
Grounding can shrink a two-child group: 1,424 accepted groups have two children
and 647 have one.

Candidates must stay inside their inferred region, at least 220 metres from
documented town centres, 80 metres from existing outdoor server objects and
other new camp centres, and 12 metres from their own group member. A bounded
32-metre search requires native dry ground, collision contact, a walkable patch
and a clear route between members. The report records 899 rejected attempted
slots; rejecting a camp's first slot withholds its remaining candidates too.
Existing captures, services, quest camps and all DAT scenery/foliage stay intact.
No expansion is inferred for dungeons, Arramora, Knorr or missing regional edges.

The normal server uses native health, level, vigor, natural damage, armor,
perception, wandering and homesickness for these actors. Existing generic
treasure rolls and native trophy probabilities apply, so they can be killed and
looted through the usual ownership/persistence path. This does not recover full
retail loot affixes, creature spell AI or exact spawn weights. Child regeneration
is an explicit **180-second** policy; the controller owns creation, corpse
retirement and replacement rather than inserting permanent static mobs on login.
Idle distant regions perform no collision or wandering work until players approach.

### Regeneration and acceptance

From the repository root (using the configured local Python/Rust tools):

```powershell
python AC2E/emulator/tools/author_regional_population.py
python AC2E/emulator/tools/import_creature_combat.py "Asheron's Call 2 - Legions/portal.dat" AC2E/emulator/content/regional-creature-entities.txt AC2E/emulator/content/regional-creature-combat-v1.tsv
cargo run --release --manifest-path AC2E/emulator/Cargo.toml --example regional_population -- "Asheron's Call 2 - Legions" AC2E/emulator/content/regional-content-v1.json AC2E/emulator/content/regional-world-v1.tsv AC2E/emulator/content/regional-world-v1.report.json AC2E/data/world/legions-world.tsv
python Network/prepare_content.py "Asheron's Call 2 - Legions" AC2E/data/world/legions-world.tsv
./AC2E/Build-AC2E.ps1
$env:AC2_TEST_GAME_DATA = "${PWD}/Asheron's Call 2 - Legions"
cargo test --release --manifest-path AC2E/emulator/Cargo.toml --test regional_population
python Network/test_regional_population.py
```

The grounder ignores only this pack's exact reserved identity range when run
against an already expanded world. Preparation refuses stale catalog/placement
proof or mismatched native visuals. Native acceptance checks all 3,495 positions,
regional rosters, town/content separation, combat health and varied loot for
every placed template, plus group capacity, one-time initial creation and delayed
replacement. Two real UDP clients tested visibility, kills and loot pickup in
Esper (Drudge Outcast, 38.6N 22.6W), Kydi (Akeha Mimbu, 10.3N 40.6E) and Oriad
(Undead Stony Armoredillo, 15.0S 4.7W) with the complete 9,801-placement world.
Only those three test templates had reduced health/damage for the bounded UDP
test; separate native acceptance verifies the normal stats. These are protocol
tests, not a rendered-client visual inspection of all new locations.

## Quests and flow evidence

`AC2E/emulator/content/native-quests.json` contains 582 unique quest definitions,
449 readable titles, 1,650 readable phase-journal entries, 2,575 quest-effect
definitions and 2,806 entity records with NPC-table or death-effect links. Of
those death-effect references, 581 resolve to a native quest and status word. Native
IDs, localization keys, phase flags, related-quest lists, effect status words and
unresolved references are retained. It is an authoring catalog, not a runtime
switch that enables all quests.

Examples recovered directly from the data:

- `QuestKillGreenWaspStriker` (`40000097`, **Incursion**) has ordered journal
  entries for five through one remaining Green Phyntos Wasps. Creature templates
  `47000f9c` and `47000f9d` name its increment effect in `NPC_DeathEffect1`.
  `47001623` names its bestow effect with a different native stat value. Those
  values are preserved, not silently interpreted as identical kill-credit rules.
- `QuestArwicBoss` (`40000032`) says to slay the Drudge Keeper in the Arwic Vault;
  `QuestArwicShard` (`40000033`) says to use the Arwic Shard. Retained setup
  bytecode describes both as hidden quests controlling the Outland quest flow.
- `QuestBrothOfTanacha` retains a phase list of five related quest IDs. These are
  references to other quests, not callable script functions.
- Orven resolves to `NPCTable_Orven`. This is a table name, not its dialogue,
  prerequisite and reward rows. Those rows were not recovered in this pass.
- Old event packages such as `QuestMucorMushrooms` retain setup bytecode but have
  empty localized text entries. Missing text is reported rather than replaced
  with a package name or new prose.

Function-name exports and import symbols are stripped. Virtual tables still
point to real bytecode, including setup constants and text references. Other
methods are empty frame/return stubs or trap instructions. The exporter records
these distinctions; an empty exports list must never be reported as proof that
the entire script is missing. Conversely, a retained constructor does not prove
that its server-side eligibility, advancement or reward implementation exists.

To activate a recovered quest, still verify its bestow trigger and prerequisites,
phase transitions, fellowship/kill-credit policy, completion/repeat rules and
rewards. Bind those to authenticated server actions, persist the state changes
and test replay/duplicate events before exposing it as a playable quest. Native
text and effect names alone are insufficient. Existing AC2E-authored tutorial
quests remain distinct from recovered retail quests.

## Population evidence

The installed-DAT generator audit found 639 templates with generator properties
and 2,111 profile labels. Across 20,784 outdoor and 151,868 interior placements,
none referenced a generator profile; the EncounterDesc table was empty. Five
unsupported template parses remain recorded in the audit. The `Generator`
bytecode exposes property/editor setup but has not yielded the missing child
rosters, region records or placed generator database.

Accordingly this pass adds no speculative spawn coordinates or camp sizes.
Existing compatible captured monsters continue through server-owned point
spawners; verified services remain fixed objects. Matching profile names is not
enough to reconstruct retail placement. The supplied SQL's entity and quest
tables were also empty, as documented in `NATIVE_GAMEPLAY_PASS.md`.

## Reproduction and validation

From the repository root, after building the reference probe:

```powershell
cargo build --release --manifest-path Tools/RetailProbe/Cargo.toml
python AC2E/emulator/tools/import_native_quests.py "Asheron's Call 2 - Legions" --output AC2E/emulator/content/native-quests.json
python AC2E/emulator/tools/import_native_trophies.py "Asheron's Call 2 - Legions" --output AC2E/emulator/content/native-trophies.json
```

The quest catalog fingerprints WLib and MasterProperty; each trophy rule
fingerprints the actual inherited EntityDesc records used. These are separate
from generated `Saved/` research listings.

Server/bridge and Unreal Editor/game builds passed. Installed-DAT tests validate
sign classification, native text resolution, trophy compatibility, a 5% trophy
distribution, loot-proof suppression and stable corpse rolls. Real UDP tests
validate sign reading and range limits, self/NPC/item examinations, doors,
vendors, corpse naming, single pickup/Loot All, ownership, replay and inventory
persistence. These checks do not establish complete retail quest or loot parity.

All 52 Unreal data tests passed. A rendered client/server journal test also
passed skill training, advancement, untraining/cancellation, quest tabs and
window dragging. That test exposed two input defects fixed in this pass: saved
off-screen window offsets delayed visible dragging, and underlying health-bar
controls intercepted clicks on an overlapping floating panel's title bar.

## Second corpus pass: Pyreal SQL, cached articles, and missing private strings

The second pass audited 70 SQL files, deduplicated into 26 distinct byte payloads,
across the extracted server projects, packet-corpus source snapshots and supplied
2017 SQL. A fresh copy of **5 AC 2 Pyreal SQL Database.7z** from the supplied MEGA
folder contains the same `pyreal.sql`: SHA-256
`e70658d4856784ae8a032c81cac2664c014caf009c1cadbbe6387ba5c2e2dd0d`.
Its `entity`, `quest`, `questphase`, `skill` and `submission` tables remain empty.
The version 18–22 database drafts contain schemas, not the missing population.
The file `ikeras-shops.sql` contains ten **portal** rows, not vendor inventories.

There are useful tables elsewhere in Pyreal. Three byte-identical copies of the
old **AC2UtilSQL** guide database contain:

- `npc`: 184 NPC names and coarse map coordinates.
- `incursions`: 67 starter-monster rows with up to five guide locations each.
- `quests`: 162 quest names, guide levels and detail/guide identifiers.
- `overview`: 282 summary rows, including suggested/minimum levels and rewards.
- `bestiary`: 791 creature reference rows with level, health and XP fields.
- `dungeons`: 65 entries; also outposts, vaults, portals, resources and towns.

These previously escaped the narrow SQL audit because it only admitted emulator
table names. The guide DDL also uses unquoted table names, single-line schemas,
and column identifiers with leading spaces. The reader now supports those
formats through an explicit public-content allowlist. It never executes SQL or
exports account/character rows. The corpus catalog retains source hashes,
duplicate paths and original row values. All 70 SQL files parsed without errors.
Zero-coordinate placeholders are excluded from location hints, not spawned at
the world origin. Repeated tables in separate dumps are not independent evidence.

### Previously hidden article content

The archived wiki's ordinary extracted files are incomplete. Its internal
`AC2/hts-cache/new.zip` contains the actual article responses under URL-shaped
member names. Reading that ZIP directly recovered **2,075 main articles and 902
HTML tables**; 397 articles include a Quest Starter field. No HTML is executed
and URL-shaped members are never extracted as filesystem paths. File/talk/user
namespaces, revision URLs and missing-page responses are excluded.

`reference-tables.json` records 597 guide-row/article candidate links, 968 guide
rows with usable coarse location hints, and 83 native quests with title-matched
article candidates. Names are search aids, not proof that two versions have the
same template identity. The full article text, links and tables remain in the
generated `Saved/ContentRecovery/wiki-content.json`; each article has its raw
HTML SHA-256 and original URL. The compact catalog retains selected loot and
dungeon tables plus all 46 incursion-index rows.

Useful examples from the cache:

- **Gambling Debt** explains Orven's role: Kanten randomly assigns a quest and
  gives the player Kanten's Bag; the player hands it to Orven to finish. This is
  an item hand-in with quest state, not a universal click-to-reward action.
  The page lists level 11+, a two-hour limit/repeat and 1,000 XP. These are guide
  claims requiring build validation, not newly installed server rules.
- **Broth of Tanacha** identifies five monster/ingredient pairs and the order:
  gather the ingredients, place them in the cauldron, then use it. That agrees
  structurally with the native five related quests and final use-cauldron phase.
- **Incursion** has 46 starter/target/reward/region rows. Its target amounts mean
  kills required by the quest, not generator capacity. The Green Phyntos Wasp
  article separately describes a five-wasp location south of Arwic; this is a
  guide observation, not a recovered generator roster or terrain transform.
- **Monster Statistics** explicitly describes its Drops column as how many items
  a creature is *likely* to drop, and labels its source Hero 1.0. It provides
  useful comparisons and unique-trophy lists, not item-selection probabilities.
- **Loot Jewelry** has effect ranges. **Loot Weapons** describes random modifiers,
  exceptional quality and cosmetic variation, while explicitly saying several
  prefix effects are untested. No general affix/stat-roll formula was recovered.
- **Tyrant Armor** includes Tyrant's Loft room/corridor occupants, companion
  counts, boss levels and scale quantities. Its trophy percentage table is
  explicitly a sample of 30 or 50 kills per creature, not native probabilities.
  The room roster can guide a future dungeon pass once matched to actual cells.

Version disagreements are retained. Eight incursion index/detail pairs disagree
on repeat time; the tool reports both values and hashes. Green Phyntos Wasp is
five minutes on its detail page but twenty hours in the index. Broth of Tanacha
has 400 XP in the old SQL overview and 850 on the cached page. Two Miller of
Kehan pages disagree on XP, repeat time and the Miller's location. Orven's own
article gives 16.2N 28.4E, while Gambling Debt places the hand-in near 44.9N 7.1W
and the old SQL says 44.9N 7.0W. None of these conflicts is resolved by silently
preferring the last imported source.

### Native text recovery

Pyreal's `StringTables/English/LocalEnglish.rar` contains loose native localization
records, including private StringTables absent from the installed DAT. The new
`recover_quest_text.py` uses the pinned packed decoder to follow the same key
through private-table redirects. Recovery requires an identical installed parent
record; any installed intermediate record must also match byte for byte. Missing
keys, cycles, variables requiring formatting, empty text and version mismatches
remain unresolved. Existing readable fields are never replaced.

This recovered **75 names, 18 summaries and 198 journal phases**, across **98
quests**. Examples include Broth of Tanacha, Arwic/Esper/Lost Wish obelisks, Rescue
the Shelter Hold, Arwic Mines and the Miller introduction. The 203 remaining
MissingPrivateTable fields comprise 178 absent archived records and 25 empty
texts. Other error categories, such as uninitialized fields, are not rewritten.
The generated `recovered-quest-text.json` preserves raw text/grammar markers,
quest/field identity and all record hashes. A matching parent does not prove the
missing private record came from exactly the same build, so this remains
authoring evidence rather than an automatic localization overlay.

### Sources that do not fill the gap

The old `spawn list.xls` is a lookup of entity/name/visual and related IDs, not
placed generator coordinates. The newly checked **1 Utilities.zip** contains
AC1 tools; its CreatureMapper `spawn.xml` and Dereth Explorer `quests.xml` are
for AC1 and are excluded. The password-protected `AC2 Users.rar` remains unread.
The source tree's test quest-advance packets and `IQuest` interfaces do not
supply NPC dialogue/eligibility/reward rows.

No complete executable NPC table, exact placed generator database, or general
loot-weight/stat-generation table was found in this pass. The new sources do
provide substantially more quest text, flow references, coarse locations,
dungeon rosters and loot comparisons than the admin client alone. Activation
still needs source/build reconciliation, exact entity/cell bindings, durable
quest progress, authenticated item hand-ins and replay-safe rewards.

### Reproducing this pass

Run from the repository root; the HTML tool requires `lxml`. Extract only the
`0x25` folder from the historical English RAR to the indicated scratch folder
before the first command. Archive extraction does not modify the retail DATs.

```powershell
python AC2E/emulator/tools/recover_quest_text.py "Asheron's Call 2 - Legions" --loose-strings Saved/ContentRecovery/old-local/0x25 --source-archive Saved/ServerReference/extracted/199ae71fc5ca72c6/PyrealSourceCode/StringTables/English/LocalEnglish.rar --output AC2E/emulator/content/recovered-quest-text.json
python AC2E/emulator/tools/recover_reference_tables.py --sql-root Saved/ServerReference/extracted --sql-root Saved/PacketCorpus/extracted --sql-root Saved/PyrealSQL-2017 --wiki-cache Saved/ServerReference/extracted/595afb35dbea396a/AC2/hts-cache/new.zip --wiki-output Saved/ContentRecovery/wiki-content.json --native-quests AC2E/emulator/content/native-quests.json --recovered-quest-text AC2E/emulator/content/recovered-quest-text.json --output AC2E/emulator/content/reference-tables.json
```

Validation covers SQL formats/deduplication, exclusion of private tables,
coordinate placeholders, inert HTML/cache reading, conflicting timers, generic
quest-title ambiguity, exact-parent text recovery, missing keys, variable text,
cycles and preservation of existing translations. Installed-DAT quest vectors
also pass. This pass changes recovery tooling and authoring catalogs only; it
does not change live NPC behavior, spawn placement or loot probabilities.
