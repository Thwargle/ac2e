# Stationary world content

## Placement clearance

`placement-clearance-v1.json` is a bounded correction layer over the admitted
world manifest. The November 2026 pass checks 9,635 terrestrial outdoor combat
actors against installed DAT collision and inherited CSetup body dimensions.
It initially corrected 178 anchors: 135 with inadequate ground/body clearance and
43 with overlapping living bodies. Four additional vertical corrections of
15–26 cm address shallow penetrations exposed by the stricter contact check,
bringing this layer to 182 anchors (including the noncombat Mimbu Shaman).
Each correction is within 3.5 metres and pins the
complete original row. `Network/prepare_content.py` fails on changed evidence;
it preserves identity, visual, scale, orientation and spawner membership. Original
capture packs remain intact. The final merged manifest is independently tested
against native floor, dry-ground, footprint and inter-creature clearance.

The initial policy's 26 `unresolved` entries are the input to
`placement-reviewed-v1.json`. This second review corrects 22 more anchors, for
**204 total repairs**: 13 escape native static props containing their original
points, three move from submerged ground onto dry floor, and six gain support
for their full body footprint. Moves range from 0.68 to 12.97 metres, within an
explicit 20-metre bound and the original landblock. IDs, models, scales and
spawner slots remain unchanged. All 26 decisions pin the original rows; the
preparer rejects changed evidence and incomplete or duplicate review decisions.

The review uses installed Legions CSetup dimensions, VisualDesc scales, native
terrain/interior/static collision and admitted capture/recovery mappings. The
local 7160 decompile's `CPhysicsObj::GetRadius/GetHeight`,
`CPartArray::TraceRayThroughPartsPhysics` and `Cylsphere::TraceRay` corroborate
the body/ray behavior. A native ray beginning inside a prop reports a
zero-distance hit; `Geometry::contact` now rejects that as supporting floor.

Each correction follows existing floor in 25-cm samples with slope, step and
body-height ray checks, then verifies the complete destination body, dry ground
and separation from creatures and known grounded NPCs. For escape-route queries
only, the authoring tool may omit a static prop proven to contain the original
anchor. Every prop remains in destination clearance and runtime collision.
The Immense Energy Flame Construct stays on its 139-metre platform rather than
being grounded on terrain about 75 metres below it.

The final four Energy Flame Constructs are **verified unchanged**:
`8140db030fc1c77d`, `814095bffa0c90b2`, `8140e8e67d64bd68` and
`8140f5817d7c1aa4`, all in `4DA2`. Their installed visual `1f001384` has two
native collision spheres (radii 2.520416 and 2.119103 metres), not a flat
3.564406-metre CSetup-radius disk. Requiring floor under the entire disk falsely
rejects their narrow platforms. The lower sphere extends 2.84 cm below the pivot,
matching the captured origins above platform floors at 169.425, 139.415, 125.900
and 135.425 metres. Native AI explicitly disables wandering for this template.
The recovered actors map captured `47002708` / `1f000d76` to installed
`47002760` / `1f001384`; the source and installed `4DA2` landblock record hashes
match. Capture IDs, message hashes and that cell-record comparison are retained
in each verified decision's provenance.

The 7160 decompile's `CPhysicsObj::transition` (`0x004D6BC0`) and
`CTransition::init_sphere` (`0x004CE450`) pass the CSetup sphere array into
placement; `CPhysicsObj::CheckPositionInternal` (`0x004D4910`) and
`CTransition::find_placement_position/find_placement_pos` (`0x004D0680` /
`0x004CFF30`) distinguish checked placement from sliding or forcing. The pinned
native physics port accepts all four captures with flags **zero**, retains their
cells and exact positions, and maintains walkable contact with zero velocity
through 100 consecutive 100-ms quanta. Removing the platforms, raising the actors
into open air, or burying them by one metre all fail verification. This is a
native-data/physics replay, not a visual playthrough in the retail executable.

`placement-reviewed-v1.json` now contains 22 repairs, four `verified` originals
and zero unresolved anchors from this review. It retains the earlier failed ray
probes, native verification results and negative controls as evidence. The
preparer pins unchanged rows and rejects altered scales, models, coordinates or
verification that used sliding/forcing. Source captures and spawner membership
are unchanged. Both authoring tools use the native sphere check for unsupported
original platforms; dry-ground and living-body separation still apply. Runtime
walking retains its conservative swept checks, and no actor-ID collision
exception or reduced model size is introduced. The correction total stays 204.

Three earlier overlap warnings cleared after their neighbours were repaired;
the authoring tool rechecks the final population.
Interior placements, airborne creatures and static
attachments are not automatically relocated: the collision backend does not
represent every repaired dungeon structure, and nearby captured prop variants
can be intentional. This is a conservative placement pass, not a recovery of
retail generator coordinates or a full capsule solver.

Runtime spawners also reserve the bodies of pending children in each tick. An
anchor occupied by another living creature waits and retries after one second;
it does not relocate, duplicate, or consume a group's capacity while waiting.
The retry interval is AC2E scheduling policy, not a recovered retail timer.

Reproduce the review with `cargo run --example placement_audit -- DAT WORLD REPORT`.
Author a candidate using `cargo run --example repair_placements -- DAT ORIGINAL_WORLD POLICY`;
the input must be the merged manifest before these corrections. To reproduce
the follow-up, use `cargo run --release --example review_placements -- DAT INITIAL_WORLD content/placement-clearance-v1.json CANDIDATE_JSON`.
`INITIAL_WORLD` must have the first correction layer applied and the 26 flagged rows
at their original positions, before the follow-up layer. The tool rejects
changed anchors. Review the candidate before replacing the policy. Set `AC2_TEST_GAME_DATA` and
`AC2_TEST_WORLD_CONTENT` to run `cargo test --test placement_clearance` against a
freshly prepared manifest. Source rows changed by a later content recovery require
a new collision review, rather than silently retaining an old correction.
Replay the four platform decisions and negative controls with
`cargo run --release --example construct_platforms -- DAT WORLD REPORT_JSON`.
This read-only tool checks the complete original rows, including orientation and
scale, and exits unsuccessfully if a support or negative-control check regresses.

At runtime, walking checks native scaled radius/height, support around the
footprint and body clearance. Per-tick living-body reservations prevent new
overlaps during wandering, pursuit and retreat, including across outdoor block
borders. Already intersecting bodies may move apart. Client presentation grounds
locomoting creatures even without humanoid feet meshes and resets stale offsets
when a floor disappears; static prop pivots and native hover heights are retained.

Spawn admission also reserves ready, living players and known grounded NPCs
(vendors and protected scripted entities with native motion bodies). Player sizes
use the six supported race/sex CSetups with the inline build/height scale replacing
inherited VisualDesc scale. Loading, disconnected, deleted and dead players do not
hold these reservations. Live NPC edits update the reservation catalog atomically.
An occupied point retries after one second without consuming group capacity or
moving its home. These extra bodies affect spawn admission only; they do not add
player collision or change the existing combat reach/navigation rules.

`Network/test_spawn_clearance.py` checks a delayed native spawn over UDP: it stays
absent while a player occupies its anchor and appears once, at the same anchor,
when that player leaves. Unit tests also cover scaled player bodies, separate
floors, NPC classification and rejected/replaced/removed live edits.

Game-data mode loads the bundled snapshot automatically, independent of the
working directory. Use the normal server options and:

```powershell
pyrealium serve --game-data PATH
```

`--content content/world.tsv` instead loads an editable external manifest.
`--content none` disables world content. Selection-only mode without game data
does not load placements. The compiled bundle changes when the server is rebuilt;
operator edits to an external manifest require the explicit file option and a
server restart. There is no hot reload.

`--content observed` selects the separately bundled 277-object observation pack:
the 247 table-derived placements plus 30 actors recovered from three supplied
packet captures. Six capture inputs were examined; the other three supplied no
additional supported placed actors. It includes Banderling Ravers/Thrashers, Idol/Surrogate Spawns,
Furs Turfna, eggs, chests and other world objects at their captured positions.
It does not populate the human starter dungeon or supply AI, trading, damage,
loot, quest interactions, death or respawn behavior. The regular default pack
continues to use only the table-derived placements.

## Racial Shelter content

`starter-areas-v1.json` describes the Human, Tumerok and Lugian Shelter routes.
`tools/author_starters.py` generates 62 additional actors in `starter-world-v1.tsv`,
seven travel routes in `starter-routes-v1.tsv`, and the hashes consumed during
content preparation. Starting the server through the launcher regenerates the
prepared manifest. A running server loads new placements on restart.

The Human Shelter has 32 actors in the main dungeon and four in the Menhir Vault.
Player captures in the matching 0298/028D instances establish the positions of
three Straw Drudges, four Drudge Diggers, eight Drudge Sentinels, the Shelter
officers, lesson stones, and the two dungeon portals. Their corresponding room
geometry matches 0299 in the installed DAT. Captured entity IDs that changed
between client versions are resolved to the installed template by identity.
The prototype Human practice idols, lifestones, quartermasters, duplicate mentors
and two generic exits are confined to a dedicated legacy test fixture.
Straw Drudges have 25,000 health and can be struck for practice without
retaliating, as described in the [retail strategy guide](https://catalogimages.wiley.com/images/db/pdf/0782141641.excerpt.pdf#page=6).

All three races can progress from the Guard through the Lieutenant and Sentry,
recover the Digger's tool for the Scout, obtain the Sentinel's glyph, enter the
Menhir Vault, speak with the Advance Scout, and use the shard to see the Menhir
history and travel to their racial overworld destination.
In the Human starting room an experienced player can instead read the exit warning,
confirm at the skull and use the early gateway. See the
[Human Shelter map](../../../Docs/HUMAN_SHELTER_MAP.png),
[archived quest walkthrough](https://www.20oz.net/index.php/Rescue_the_Shelter_Hold.html)
and [archived map index](https://www.20oz.net/index.php/Maps.html).

`Network/test_starters.py` exercises production placements, progression, travel
and relogin over the real protocol. `AC2.Data.StarterClearance` separately tests
forward and reverse routes against loaded native geometry using the largest
supported body capsule for each race. See the
[starter audit](../../../Docs/NATIVE_GAMEPLAY_PASS.md#racial-shelter-routes-and-dungeon-traversal--2026-11-02)
for reconstruction limits and the Tumerok stair repair. Server character creation
currently supports Human, Lugian and Tumerok.

## Regional wilderness population (2026-10-31)

`regional-content-v1.json` joins the archived AC2 wiki's regional creature lists
to Pyreal AC2UtilSQL ringway/town geography and installed native creature
templates. `regional-world-v1.tsv` and its ground-verification report add 3,495
accepted slots in 2,071 groups, across 21 Osteth/Omishan/Linvak regions. There are
326 placed native creature types; 329 eligible types were considered. Preparation
verifies catalog/manifest hashes and the installed entity/visual/cell records.

The separate `regional-creature-combat-v1.tsv` and `regional-creature-ai-v1.tsv`
enable native stats and perception/wandering/home behavior. Regional actors use
native stats even when the built-in development profile weakens the same
template for practice encounters. Explicit external encounter overrides still
apply. Generic variable loot and native trophy slots use the existing pipeline.

These are inferred generator locations: one candidate per landblock inside the
convex envelope of documented ringway anchors, with town and existing-content
exclusion distances, native dry-ground/contact/walkability checks, and one or two
simultaneous children. A wiki `group` label means elite/fellowship difficulty,
not a recovered spawn count. Regeneration is 180 seconds after death, with corpse
retirement acknowledged before a replacement appears. The `8140c00000` identity
prefix is reserved for this pack; random capture IDs sharing only `8140c0` are
ordinary existing content and retain priority.

Author with `tools/author_regional_population.py`, ground with the Rust
`regional_population` example, then run `Network/prepare_content.py` and rebuild.
Exact commands, source links, caveats and acceptance checks are in
[content recovery](../../../Docs/ADMIN_CONTENT_RECOVERY.md#regional-wilderness-pack-v1-2026-10-31).

## Knorr, Arramora and wilderness expansion (2026-11-01)

`wilderness-content-v1.json` supplements the regional pack with the archived
Knorr portal map, Arramora camp map and all 27 regional creature rosters.
`wilderness-world-v1.tsv` contains **2,553 additional creatures in 1,555 groups**,
using 443 native templates, including 180 with no prior world placement.
All 563 eligible roster types now occur somewhere in the combined world;
this does not establish coverage of every habitat or native variant. The
prepared production world contains 12,379 total objects.

Habitat restrictions distinguish Arramora's southern Thornlings, central golems,
northeastern Niffis and northern Olthoi. The specific Harvester quest places its
targets southwest. Foundry and Wilds solo/group areas and southern Gardens
families have separate constraints. Map envelopes, camp positions, one/two-child
capacities and 180-second refill are inferred. Native terrain checks and
town/content separation apply. The reserved identity prefix is `8140c10000`.

The combat and AI catalogs feed normal encounters and level-aware variable
loot, native trophy probabilities and recovered quantity limits. No generic
potion-only fallback is added. `wilderness-coverage-v1.json` accounts for all
2,024 native records with health/damage fields, including protected NPCs,
alternate templates, special encounters and unresolved habitats. The Olthoi
Soldier Grub's mound-dependent script is withheld. Ordinary single-kill quest
targets such as Drudge Robbers are not categorically excluded from this pack.

`hunting-content-v1.json` implements ten NPC hunts at eight speakers. Vreema's
Bone/Rime/Uncharneled Golem chain requires completing each previous hunt;
Gregor Adulen, Nola Vontag and Tankaru provide other Arramora hunts. Brelar
Marlage, Illiana Bain, Shurakhe and Endaraen Brightstar provide Foundry hunts.
Two missing Jethric speakers use `hunting-world-v1.tsv`, prefix `8140d2`.
All eight speakers remain noncombat NPCs. Level gates, ten-kill objectives,
seven-day deadlines, return rewards and documented repeat timers persist.
Kills before acceptance do not count; kills alone do not award the hunt XP.

The later retail quest IDs were absent from the recovered admin quest export.
These quests use authored IDs `4000fc01`–`4000fc0a`, summarized dialogue and
immutable `qh1`/`qk1` journal rules. Never rewrite released policy in place.
Old `q6` kill records retain their replay semantics. See
[sources, limits and regeneration commands](../../../Docs/ADMIN_CONTENT_RECOVERY.md#wilderness-and-npc-hunting-expansion-2026-11-01).

### Hunting expansion v2

`hunting-content-v2.json` adds 25 hunts (35 total, at 23 distinct native NPCs):
thirteen Ikeras quests, nine more Arramora quests offered in Linvak Tukal, and
three Foundry quests. It extends Gregor's Harvester quest into Worker Nymphs
and Drones, adds Groli's Crone/Thornling/Reaper chain and Eliza's Niffis chain,
and implements the Ikeras Spawn, Niffis/Nefane and Sclavus chains. Qualifying
variants come from the exact native quest-counter death effects; similarly
named Gauntlet actors do not count. Fixed XP and unlimited deadlines are
supported where the guide specifies them.

`hunting-world-v2.tsv` adds Furakhe at Jethric's Outpost, Ingara at Ikeras,
and guide-anchored Vigilant Spawn, Olthoi Worker Nymph and Dread Dark Mu-miyah
camps on Arramora. All eight placements passed native terrain/collision checks.
The camps use two simultaneous children and 120-second regeneration; those
capacities/timers are inferred, not recovered retail generators. NPCs remain
noncombat; creatures use native combat/AI and the existing loot pipeline.

New IDs are `4000fc0b`–`4000fc23`; new journal records are `qh2`/`qk2`.
Replay of `qh1`/`qk1` retains the original ten-quest catalog, so Gregor's old
repeat turn-ins cannot silently start a newly added chain. Neither released
catalog may be edited in place. The coverage audit includes all 35 hunts.
See [evidence and verification](../../../Docs/ADMIN_CONTENT_RECOVERY.md#hunting-quest-expansion-v2-2026-11-01).

## Retail capture population

`capture-spawns.tsv` holds 14,340 stationary objects read from retail packet
captures and not already placed by the other packs. Most come from captures
made on the 2012–2017 servers shortly before they closed: a human character from
creation and the starter dungeon (landblock `0298`) through Arwic, Kehan and the
Osteth vaults, plus two other players' sessions. The training pack loads it
after `world-observed-expanded.tsv` and `training-world.tsv` during preparation.
`capture-spawns-report.json` records each capture, its hash and why objects
were skipped.

The captures are **not a build-compatible spawn database**. Their visual DIDs
can exist in a different DAT build while naming unrelated models (including a
pavilion over an Arwic house). The launcher runs `Network/prepare_content.py`:
it admits additional capture visuals only when they match the installed entity's
resolved `PhysObj`/template chain or a curated entity/visual pair. Others remain
in the source TSV and are listed in `incompatible_capture_placements` in the
generated `.report.json`; they are not moved to invented positions or silently
assigned a replacement model. The compiled training preset uses only curated
observations and authored training placements. Use the prepared external content
manifest to add the compatible portion of the capture pack.

Recovering withheld placements requires the matching capture-build DAT set or
a verified mapping of entity/visual identities and world geometry between builds.
High-resolution textures cannot resolve this identity mismatch. The report
distinguishes incompatible observations from genuinely absent DAT records.
Lifestone snapshots within 12 metres are merged across outdoor subcell/block
boundaries, keeping the earlier stable IID. Different dungeon rooms and distinct
lifestone entity types are kept separate.

`placement-replacements.json` records explicit duplicate-observation decisions
that cannot be resolved by capture ID or cell ownership. Orven and Adaro each had
an older unverified Arwic observation alongside a semantically mapped,
native-ground-verified recovery at a different subcell/elevation. Preparation
retains the recovered placement and withholds the older row. Both endpoints are
pinned by source filename, ID and row SHA256; changed evidence stops preparation.
If the replacement fails admission, the original is kept. The generated report
lists applied and unavailable replacements. Source manifests are unchanged, and
no other actors are deduplicated by name, template or proximity. Restart the game
server to load a newly prepared manifest; existing sessions do not hot-reload it.

Regenerate it with `python tools/import_capture_corpus.py`; edit `ROOTS` in that
script to point at your capture folders. It uses the same decoder as the
observation pack below, with these differences for the newer captures:

- World objects are accepted in both the `81200000` and `81300000` id ranges,
  and server-sent static objects whose `40` ids encode their landblock.
- Captures made on the player's machine record an IP length of 0; the frame
  length is used instead.
- Messages are rebuilt as they complete, and an unfinished one is dropped when
  more than 8,192 or 16 MiB are pending, so hour-long captures fit in memory.
- Objects later attached to an owner (Physics Parent `0x10087`) or put in a
  container (Contain `0x100cb`) are skipped, so worn and carried items are not
  placed in the world.
- The first capture to place objects in a cell owns that cell.

These are snapshot positions. Creatures stand where they were seen; wandering,
AI, spawn generators and respawn schedules are not reconstructed.

## Creature combat stats

`creature-combat.tsv` lists 317 candidate creature profiles with the combat fields their
`portal.dat` entity records carry: health, natural damage, armor, death XP and
level, with an interval column that is 0 when the record has no natural weapon
time. Parent records are merged first, as the 2012 server's
`Entity.LoadFromPortalDat` does. Regenerate it with:

```powershell
python tools/import_creature_combat.py PORTAL_DAT content/creature-entities.txt content/creature-combat.tsv
```

## Capture-observed actors

`observed-world.tsv` contains the observation pack; `observed-actors.json`
records each source capture SHA256, original IID, entity/visual, position,
quaternion, scale, native masks, message SHA256 and aggregate rejection reasons.
Original capture build identity and checksums have not been verified. Positions
are first materialization observations, not proven spawn origins or schedules.
These records remain distinct from table-derived and operator-authored content.

The offline importer `tools/import_observed_population.py` accepts repeated
`--capture` arguments plus `--base`, `--game-data`, `--output` and `--report`.
It reads bounded classic little-endian Ethernet/IPv4/UDP PCAPs, checks framing,
and reassembles blobs by directed endpoints, recipient, iteration, blob ID,
queue and fragment count. Conflicting duplicates and oversized tables/messages
stop conversion. IP fragments and unsupported transport packets are not used.
Reassembly is not authenticated and does not establish complete capture coverage.

The object decoder is grounded in VisualDesc `0063d7f0`, PhysicsDesc `00641530`,
SliderData `0067cfb0` and map packing `00640f40`, FXScalarAndTarget `004d5720`
and map packing `006410e0`, and gmWeenieDesc `006037c0`. It accepts the narrow
parent-visual-only profile and supported placed physics fields. It excludes
player namespaces, contained/equipped objects, missile/behavior-list profiles,
custom names and unknown description fields. It checks all remaining lengths,
finite transforms, quaternion norm and backing DAT records before placement.

Native packets are not replayed into the server or client. Runtime creates fresh
minimal stationary descriptions with local identities/stamps, observed entity,
visual, transform and optional gmWeenieDesc scale. Captured modes, effects,
sliders, velocity, look/target state and other description overrides are omitted.
This preserves a placement snapshot, not exact live behavior or full appearance.
Two-decimal position keys prevent duplicate placements; source IID repetition
is retained only at the first supported observation in each capture.
For overlapping inputs, the first accepted capture owns a cell; later captures
do not add actors to that cell. This avoids stacking snapshots from different
times. Input order and all exclusions are recorded in the report. It does not
establish a complete or simultaneous population across different cells.
New IIDs derive from capture hash and original IID in the reserved local
`81408...` range, with collisions rejected. Global and per-cell limits remain.

`lifestones.tsv` contains 89 placements imported from the supplied lifestone
database table snapshot dated 2012-08-11. Its source SHA256 is retained in the
file. These are imported placements, not positions recovered from the client
executable. Their original client/build identity has not been established.
They do not populate the human starter dungeon.

`world.tsv` is the combined pack: those 89 lifestones plus 158 portal placements
from the supplied portal table snapshot dated 2012-02-28. The full 160 portal
records, including target telepad IDs, string tokens and eligibility fields,
are preserved in `portals.json`. Two locations (`30780019` and `2e720032`) have
no backing terrain in the supplied assets and are marked `missing-cell` rather
than spawned. None of these placements are claimed to originate from client
decompilation, and the source snapshot's build identity remains unknown.

Portal visuals use four individually pinned qualities layouts, then verify
their physical definitions and visual records. The converter retains the
source quaternions instead of reducing them to byte headings. Source row ordinal
is part of portal IID allocation for this immutable snapshot; regenerating from
a reordered table is a content migration, not a supported hot reload.

The visual chain is checked against the supplied assets: definition `470007de`,
qualities `810007de`, physical definition `47000075`, visual `1f00002d`.
The importer requires the exact qualities SHA256 recorded in the manifest before
reading its physical-definition field at byte 236. It also checks the complete
16-byte physical definition, the visual record and each backing terrain block.
It does not apply these offsets to arbitrary definitions.

The content currently supplies stationary presentation and target visibility.
Lifestone binding, death/resurrection, summon restrictions and use scripts are
not implemented. Portal travel, triggers and destination transfer are also
unfinished. Level/hero/kingdom restrictions in the source table have not
been translated into gameplay rules. NPC AI, quests, merchants, loot and portals
require additional data and behavior; this is not a full world import.

Regenerate the included file from the supplied table and matching assets:

```powershell
python tools/import_lifestones.py --source PATH --game-data PATH --output content/lifestones.tsv
```

The converter parses numeric rows; it does not execute SQL. Unknown table
columns, definitions, rotations, duplicate IDs, missing assets or terrain records
stop the import before the output is written.

The combined importer is `tools/import_world.py`. It accepts `--portals`,
`--lifestones`, `--game-data`, `--output` and `--report`. Missing portal cells
stop conversion by default. `--allow-unresolved` explicitly permits a partial
manifest and retains every excluded portal in the report. No SQL is executed.

## Definition catalog and explicit placements

`definitions.tsv` contains 7,583 full entity/visual mappings from the supplied
workbook's `Entity` worksheet, filtered against actual DAT record presence.
Source annotations remain labels only; they are not injected as native names
or treated as NPC behavior, spawn positions, loot or balance. The catalog's
source SHA256 is embedded in its header. Another 319 rows with unsupported or
abbreviated visual mappings are retained in `unresolved-definitions.json`.
No automatic NPC placements are inferred from this catalog.

`tools/import_definitions.py` regenerates the catalog with `--source`,
`--game-data`, `--output` and `--report`. Reading legacy XLS requires `xlrd`;
the Rust server gains no dependencies. Numeric Excel cells containing full
all-digit hex identifiers are interpreted as their displayed integer strings
in base 16, not as decimal DIDs. The source workbook is never modified.

To author a stationary NPC/object at a position chosen by the operator:

The graphical tool is `tools/content_editor.py`. On Windows, double-click
`OpenContentEditor.cmd` in the emulator directory, or run:

```powershell
python tools/content_editor.py --game-data PATH
```

Each launch writes a diagnostic log under
`%LOCALAPPDATA%\Pyrealium\content-editor\logs`, including Python callback
exceptions and native fatal tracebacks where Python can capture them. This
keeps errors available when the `pythonw` launcher has no console. Closing the
editor waits for its current DAT read to drain before destroying Tk resources.

It requires Python with Tk support and uses no third-party GUI packages. Search
the catalog by label, entity or visual identifier; select an entry; choose the
game-data folder and content file; then enter a cell, X/Y/Z, heading and optional
scale. The **3D world** tab reads actual DAT creature meshes, the selected
interior room and linked nearby rooms. Drag to orbit, right-drag to pan, and use
the wheel to zoom. **F** or **Focus creature** returns to the selected placement.
Shift-click a world surface to set X/Y/Z through a ray intersection with its
actual triangles. **Cut away ceiling** helps inspect an interior from above.
The camera stays fixed while coordinates, heading or scale change.

Outdoor cells (`0001`–`0040` in a block) load the actual height block and one
neighbor ring. Heights use the native 256-entry height table, not a linear guess;
holes, triangle cuts, surface selection, UV tiling and vertex-color variation
come from the DAT definitions and recovered client formulas. Real terrain
material-stage textures are displayed with native vertex colors. Ceiling cutaway
starts disabled outdoors. Shift-clicking another outdoor cell or block updates
the cell ID and local X/Y/Z together; interior room picks retain their room ID.
Saved neighboring outdoor placements receive the proper block offset.
Road/corner surface blending, water and procedural scenery remain unfinished
and are reported in Details. This preview does not establish traversability.

**Play animation** loops a real native clip for the selected supported creature.
The clip list shows recovered asset IDs and periods; the mapped idle clip is
selected first where available. Pause or choose another clip, and change
**Speed** from 0.1 to 3.0. Playback evaluates native bone keys, quaternion
interpolation, hierarchy, inverse bind matrices and skin weights. Time advances
without accumulating catch-up frames. Only the selected preview is animated;
this does not run AI or gameplay state. Unsupported graph branches are listed
in Details. Root-motion application, graph blending, event hooks and attachment
animation remain unfinished.

The **Position map** tab remains available, with a stable view, coordinate grid,
pan, zoom and **Fit cell**. Clicking it sets X/Y while retaining Z. Existing
placements can supply a position and entity for a new addition; their identities
are retained. Neither preview changes existing objects.

The Windows 3D view uses embedded OpenGL with actual native UV texture pixels,
linear filtering, cutout alpha, a depth buffer and preview lighting. DXT1/3/5
mips are decoded read-only from `portal.dat` and optional `highres.dat`; base
material properties are applied before ordered instance overrides. It requires
no extra Python rendering packages. If Windows cannot create its OpenGL context,
the position aid falls back to sampled triangle colors and reports the reason
in **Details**. Full native shader passes, translucency blending, collision and
native BSP visibility remain unfinished. Some attached visual groups and nearby props remain
unsupported; **Details** lists omissions instead of substituting made-up models.
Where a supported first material layer is decoded, alpha testing, its constant
reference, blend factors and texture-alpha use follow that layer. Zero-filled
skin vertex-color fields are not used as tint, and texture alpha is not treated
as transparency when the material ignores it. Full conditional/multi-pass shader
selection remains outside the preview renderer.
Loading is bounded to 32 cells and 50,000 scene triangles; the viewport draws up
to 30,000, prioritizing the selected creature and reporting truncation. DAT work
runs on one background worker so the editor remains responsive. Native in-game
appearance remains unverified. Python syntax and read-only DAT extraction were
checked; no GUI or game was launched by the agent.

The default output is `content/local-world.tsv`, a new standalone pack of your
additions. To include base placements, choose a separate copy of `world.tsv` as
the output. Serve the resulting file through `--content PATH` on the next normal
server start. The tool does not reload or restart a running server.

The command-line interface remains available:

```powershell
python tools/place_content.py --catalog content/definitions.tsv --game-data PATH --output content/world.tsv --entity ENTITY_HEX --cell CELL_HEX --position X Y Z --heading 0
```

The tool requires an unambiguous catalog entry, existing entity/visual/cell data,
valid coordinates and available per-cell/global capacity. It allocates an unused
IID in the operator range beginning `8140000000020000`, marks the placement as
operator-authored, flushes the temporary file and replaces the manifest atomically.
An exclusive `.edit.lock` prevents overlapping tool saves, and a changed-file
check rejects external edits during validation. If a process crashes while
holding that lock, close all placement tools and remove only the corresponding
stale `.edit.lock` before retrying. It does not alter a
running server. Regenerating `world.tsv` from source tables replaces such local
edits, so retain them separately before regeneration. No made-up demonstration
NPCs were added to the shipped pack.

## Manifest format

The first line can be `pyrealium-world-v1`. Following nonempty lines contain eight
tab-separated fields: object IID, entity DID, visual DID, cell, X, Y, Z, heading.
Lines starting with `#` are comments. IDs are hexadecimal, coordinates are
decimal and heading is a decimal byte (0..255). Object IDs use the local
`8140` namespace and must be unique. Entity/visual DIDs use `47`/`1f` prefixes.
`pyrealium-world-v2` instead has eleven fields, replacing heading with quaternion
W, X, Y, Z. All quaternion components must be finite with norm within 0.01 of 1.
The combined pack uses v2; existing v1 files remain supported by the server.
`pyrealium-world-v3` adds a twelfth field: a positive scale up to 100, or `-` to
leave scale inherited. This is local content policy. The observation pack uses
v3 and writes the scale as native gmWeenieDesc bit `0x80`; v1/v2 encodings are
unchanged. The authoring tool accepts v2 and v3. Its optional `--scale NUMBER`
upgrades a v2 file to v3 while preserving inherited scale (`-`) for every prior
object. Both interfaces allocate additions only in the operator IID range
`8140000000020000` through `814000000002ffff`.

The runtime rejects files larger than 4 MiB, placement lines over 512 bytes,
more than 16,384 objects or more than 64 objects in one cell. Positions must be
finite and bounded. All referenced definitions, visuals and backing cell data
must exist before the server opens its sockets. This checks data presence,
not arbitrary entity/visual compatibility or terrain traversability.

Outdoor cell IDs 1..64 within a block use its `ffff` terrain record. Supported
interior cells use their own record. Reserved IDs and the gap before `0100`
are rejected. The player movement data-presence check uses the same mapping,
with a bounded cache of previously accepted cells. It does not enforce collision.

Objects are created after scene readiness, destroyed when their owner-visible
cell is left, and recreated when that cell is entered again. Visibility follows
the existing same-cell policy; nearby-cell/outdoor-block visibility is unfinished.
Encoded spawn/destruction payloads are immutable and shared between recipients.
Content stays outside the player-session index and player count limit.
Object instance stamp 1 is local policy for these immutable server lifetimes;
runtime replacement/hot reload is not supported.

No tests or native-client checks were run for this addition. Compilation and
read-only asset inspection do not prove in-game appearance or interactions.

## Pinned upstream continuation

`capture-spawns-upstream.tsv` is the unmodified v4 capture snapshot from Pyrealium
revision `468793018b332b87228794b0720ea1ce0400f96d` (22,422 rows). Its optional
last column preserves captured names. Preparation merges it after existing inputs,
keeps earlier IIDs/cell snapshots, validates native qualities/physical visuals and
reports conflicts. It does not enable all 22,422 observations. Current installed
DATs admitted 1,276 total placements before the subsequent corpus audit below.

`upstream-content.json` records provenance. Combat values were regenerated from
local DATs against the broader upstream candidate list: 317 usable profiles,
406 failed/missing candidates. The upstream skill/advancement catalog was not
copied because the installed DATs disagree with many of its records.

The supplied 2017 SQL archive has no entity/quest population rows and its 160
portal rows duplicate the retained portal metadata. Use
`tools/audit_pyreal_sql.py` to inspect it without executing SQL. The raw archive
and dump stay outside source control.

## Combined packet and historical emulator audit

`capture-spawns-audited.tsv` adds 121 conservative observations in 86 uncovered
cells from the supplied MEGA capture/emulator collections. Its report records
capture/message hashes, initial state and exact-row exclusions for historical
looted/dead observations. Preparation keeps one source snapshot per cell,
preserves original coordinates, validates installed entity/model mappings and
admits 1,392 combined placements after all filters. A location observed once is
not proof of a retail spawn schedule.

The native offline decoder and bounded PCAP/PCAPNG audit tools can regenerate
this data without publishing captures or account/player information. See
`Docs/CAPTURE_REFERENCE_IMPORT.md` in the repository root for provenance,
commands, verification and the limits of the historical emulator/SQL references.

The historical `creature-entities.txt` list includes town NPCs and is only an
extraction candidate list. It does not establish attackability. Runtime encounters
require a recovered `creature-combat.tsv` row or an explicit authored rule;
services and configured quest guides remain protected even if their templates
carry combat statistics. The current 1,392-object pack has 510 combat actors,
including 135 outdoor placements in 64 blocks. This is incomplete observed
coverage, not retail world generators. `@spawns` lists nearby admitted actors;
it does not generate creatures.

## Population recovered with source-build DATs

The later 2017-client comparison supersedes the coverage counts above.
`recovered-population.tsv` contains 4,217 placements translated by native
localized name, AI table and role, with source visuals and native ground contact
verified. Coordinates, rotations and scales remain those captured. Its report
records every mapping, capture/message hash and both client DAT fingerprints.
The manifest has a required content hash and LF line endings.

The recovered rows include 3,931 monsters (3,742 outdoors), 261 scripted actors
and 25 vendors. The merged pack has 3,872 outdoor combat creatures in 1,642
blocks and 78 native vendors. One capture snapshot per region prevents repeated
journeys/respawns from being stacked. Curated and training cells remain prior.
`population-protected.txt` prevents recovered service/scripted identities from
becoming generic combat encounters through inherited stats.

The normal launcher prepares this pack automatically. Rebuild the server after
changing combat/protection catalogs, then restart it. `@spawns` lists nearby
live monsters on a local test server. A verified Cutter Beetle location is
`78B8002E`, approximately 44.9N, 5.4W.

Restored placements do not imply completed quest scripts, retail loot tables,
vendor inventory parity, or retail spawn schedules. Refer to
`Docs/CAPTURE_REFERENCE_IMPORT.md` for the full source audit, regeneration
commands, validation and remaining gaps.

## Population ownership

Eligible combat placements become server-managed **point spawners**. The server
withdraws the imported monster lifetimes before accepting clients, then the
spawners create their children. Each point has capacity one, including a corpse
that is still present for loot. Death schedules corpse removal and regeneration
separately; replacement waits for confirmed removal. Movement and visibility
changes do not vacate a slot. Vendors, protected quest actors, doors and other
fixed content do not enter this controller.

`development-encounters.tsv` retains its legacy `npc` regeneration field. An
optional `spawner` row overrides initial delay and regeneration in milliseconds
for a placed combat template: `spawner<TAB>entity<TAB>initial_ms<TAB>regen_ms`.
Without an override, initial delay is zero and the existing authored interval
applies (30 seconds for imported native combat profiles). These are AC2E rules,
not recovered retail generation schedules. Lootable corpses remain for 60
seconds, so their replacement may wait longer than the regeneration interval.

`native-generators.report.json` records the installed DAT evidence: 639 templates
carry generator properties and 2,111 profile labels exist, but the parsed outdoor
and interior placements contain no generator profiles, and the encounter table
is empty. Five unsupported template parses are recorded. The template properties
include regeneration periods and quantity overrides; the profile child lists,
region definitions and generator locations have not been recovered. A profile
name is insufficient to bind a camp or treasure generator to a captured monster.
The current point spawners preserve verified capture positions, full quaternions
and scale; they do not add random regional populations.

An optional `spawn-group<TAB>group_hex_id<TAB>capacity<TAB>child_iid_hex,...`
row binds an explicit roster of existing monster points. Capacity is 1–64 and
cannot exceed the roster size. Each child belongs to at most one group; services,
unknown actors and duplicate children are rejected. Pending creation, live
children, corpses and pending removal all occupy capacity. Group rows do not
invent positions, clone templates, or change the members' regeneration policy.
No production grouping is inferred from proximity: native generator rosters and
quantity overrides for these placements have not been recovered.

## Loot and idle AI

`observed-loot.tsv` contains 698 distinct bundles from 820 deduplicated corpses
of 146 monster templates. `loot-items.tsv` identifies 222 compatible items.
The importer matched the landblock or indoor room in both cell DATs for 652
corpses, yielding 497 local tables for 133 monster types. The provenance report
records the capture hashes, matched cells and DAT hashes. The other 168 corpses
contribute to their creature's table without asserting a location.

The server samples the creature's spawn landblock outdoors or its exact room
indoors, then its species and its shared native `NPC_DeathLootProfile`.
Pulling a monster into a different area preserves its source. Generic slots prefer
distinct, level-eligible items from those samples. Captured uncut gems (24),
rings and talismans (10), and shields (11) can fill ordinary slots on the
creatures observed to drop them. Their DAT identity, equipment slots and
use restrictions distinguish them from quest items such as Amulet of Seeking.
These counts refer to base items; their randomized enchantments are unknown.
Only supported equipment and restoratives
can fill remaining slots from the common pool. Undocumented profiles without
an encounter or native quantity do not generate substitute items.
`NPC_LootProof` suppresses drops. Tutorial and quest-gated items have their own
eligibility checks.

Observed frequencies weight sample selection. The captures contain corpses
seen with contents, so they cannot establish the chance of an empty corpse
or the overall drop rate. For encounter templates without native quantity
overrides AC2E gives an observed slot a 35% chance, then rolls independent
70% and 25% chances for additional generic slots. Retail probabilities,
randomized affixes and unobserved tables remain unknown.
The separate `native-trophies.json` now provides explicit installed-DAT trophy
probabilities: matching trophies are removed from observed bundles and rolled
with their native slot rates. Generic treasure weights remain unrecovered.
The [AC2Hero trophy guide](http://ac2hero.e5i5o.com/essence2.html) also identifies
carriers whose trophy slots are present in the 2017 client DAT. The supplement
`hero-reference-trophies-v1.json` translates 25 of those slots onto nine verified
Legions creature variants. It supplies a 1.67% chance per listed beard, heart
or tooth slot on Banderlings and 5% for the Debased Skeleton's jaw bone. Seven
of these slots are directly attested in the 2017 DAT but missing from the guide.
`tools/import_hero_trophies.py GUIDE_HTML REFERENCE_DAT_DIR LEGIONS_DAT_DIR OUTPUT`
rebuilds the supplement using the carrier's level, AI superclass, loot profile,
equipped template and the trophy's item signature. The server checks the Legions
entity and item fingerprints before enabling each rule. Installed native slots
take precedence. The differently equipped level 42 Banderling Raver is excluded.
Each corpse rolls once, supports up to 32 items, and retains unclaimed items on
close/reopen or a full backpack. Single pickup and Loot All use journaled grants;
claimed inventory survives restart. Unclaimed corpses remain temporary.

`native-loot-limits.json` contains 312 inherited native quantity overrides, guarded
by hashes of the entity inheritance chain and property type registry. These
minimum/maximum controls replace the inferred/observed generic item count where
present (for example, Baron Colier: 3–6). Zero is honored. Native trophy slots
remain separate and explicit operator loot tables take precedence. The creature's
native level is a fallback for reconstructed item stat rolls; quality/variance
overrides are retained as evidence without inventing their retail formula.
Uniform quantity sampling, the supported equipment/restorative pool and equipment
rolls remain AC2E reconstruction. Observed supported equipment also receives durable
rolls. Reopening a corpse never rerolls its items, and pickup preserves their stats.
Regenerate with `tools/import_loot_limits.py <DAT-dir> content/native-loot-limits.json`
from `AC2E/emulator`; the report lists five undecoded entity layouts withheld.

`treasure-weapons.json` adds 69 observed weapon identities to generic treasure,
with entity/qualities/physical/visual hashes checked against the mounted DAT.
Native `ImplementType`, valid/preferred hand slots and `Usage_MinLevel` are
preserved. Generated and sampled equipment both respect the encounter's level,
including level-one tutorial variants. Overcharged Hive is withheld
from this pool because its additional `0x2d8` usage restriction is not implemented.
All 69 defaults omit generated damage. The documented AC2E fallback is
`(6 + 2 * treasure level)` with ±15% per-item variance; it is not the recovered
retail treasure formula, affix system or per-hit weapon variance. A roll's treasure
level is at least its native use level, but does not become a new equip requirement.
Inspection reports both levels and damage. Equip/replacement validates the native
minimum, and basic attacks/weapon-based skills consume the equipped damage rating;
periodic damage does not add that rating again. Weapon grants use the distinct
`W` journal record, preserving their stats through pickup, equip and restart
without relabeling them as crafted armor. Existing unrolled damage values
are not migrated. Rolled weapons use the existing authored three-gold equipment
sale policy; native economy and mutation tables remain open.
Regenerate from `AC2E/emulator` with
`tools/import_treasure_weapons.py <DAT-dir> content/treasure-weapons.json`.
`Network/test_weapon_loot.py` exercises a four-weapon corpse, native level refusal,
pickup/inspection, Flame Bolt damage and burns, remote FX and equipped restart
persistence over UDP. The regional population test validates both armor and weapon
rolls for every placed creature template.

`creature-ai.tsv` preserves inherited native perception, homesickness, wandering
enable/radius/probability, free-attack/idle-only flags and loot-profile bindings.
Absent properties stay absent. No wandering range is invented for a creature
without one. The runtime uses native collision for wandering, pursuit and line
of sight. Nearby actors scan once per second on staggered schedules; distant
unobserved actors do not load collision merely to roam. Uniform disk wandering
destinations, the missing-probability fallback (0.2), and generic
hostility exclusions are AC2E policy, not recovered retail AI scripts.

Creature locomotion now resolves the placed `VisualDesc` parent chain to its
`MotionInterpDesc` (`0x32`) and native Y scale. `CMotionInterp` selects the ground
peace/combat mode's forward speed (default-mode fallback), multiplies speed by
visual and instance scale, and scales acceleration only for bodies above unit
size. The server applies those limits with elapsed time, caps stall catch-up at
250 ms, and collision-checks movement in segments no larger than 0.2 m. Homeward
travel spends the same distance budget along recorded segments rather than
retracing just one segment per tick. All 8,612 installed combat actors resolved
native profiles in the population audit; none use the synthetic-fixture fallback.
At unit instance scale, native forward limits include Idol Spawn 3.9 m/s,
Red Soldier Wasp 6.5 m/s, Crawlspace Vermin 7.475 m/s and Drudge Outcast 7.8 m/s.
Captured instance scale can further change these values. These are locomotion
limits, not a claim that retail AI always requests maximum speed: original AI
pace choices, speed-altering effects, flight paths and swimming navigation remain
separate gaps. Source: `ac2_motion_interp.rs`, `motion_interp.rs`, and the admin
client's `CMotionInterp::GetDesiredMotionValues` (0050B660).

`native-quests.json` is a version-fingerprinted authoring catalog of native quest
text, phases, related quests and entity/effect links. It does not enable quests
whose prerequisites, NPC tables and reward flow remain unresolved. Recovery
methods and runtime changes are documented in `Docs/ADMIN_CONTENT_RECOVERY.md`.

`recovered-quest-text.json` adds historical authoring evidence for 291 missing
fields across 98 native quests. Installed parent StringTables must be byte-identical
to the archived Pyreal parent; every private redirect is fingerprinted. Existing
translations are never replaced, grammar markers remain raw, and these strings
are not a live localization overlay.

`reference-tables.json` catalogs the additional Pyreal/AC2Util SQL tables and
the archived wiki's HTTrack cache. It includes 184 NPC guide rows, 67 incursion
starter rows, 162 quest-index rows, 791 bestiary rows, and native-title candidate
links to 83 wiki articles/quests. It also retains 46 incursion reference rows,
eight repeat-timer conflicts, and selected loot/dungeon tables. Full article
text is generated under `Saved/ContentRecovery/wiki-content.json`.

Both catalogs have `runtime_enabled: false`. Guide coordinates lack exact cell,
height, heading and generator rosters. Quest kill targets are not simultaneous
spawn counts; sampled trophy percentages and likely drop-count ranges are not
retail loot weights. Follow the source hashes and resolve version conflicts
before authoring gameplay. Neither catalog is loaded by the game server.

The user-authorized `inferred-content-v1.json` pack now derives a separate playable
reconstruction from those references: 88 quest profiles, 143 capacity-limited
camp rosters and 26 additional noncombat NPC placements. `inferred-world-v1.tsv`
contains 702 accepted native-ground placements, with source/manifest hashes and
rejections in its report. `prepare_content.py` appends this pack after captures;
it never rewrites them. Additional native combat/AI profiles live in the matching
`inferred-creature-*-v1.tsv` files. Generic loot now varies and supported armor
has persistent ratings/tints. These are inferred rules, not recovered retail
weights. See `Docs/ADMIN_CONTENT_RECOVERY.md` for exact assumptions, provenance,
limits, regeneration commands and immutable journal-version requirements.

## Ringway exploration quests

`exploration-content-v1.json` implements the three regional Obelisk Search
conversations and the Find the Arwic Mines Overseer referral using native
quest/entity IDs and recovered journal text. Guide XP, deadlines and cooldowns
are recorded as reconstruction policy. Versioned `q9` journal events preserve
ordered visits, return-to-Explorer rewards and referral completion across restart.

`exploration-world-v1.tsv` adds nine stationary obelisks near the recovered
ringway directions; it adds no NPC copies. These placements are inferred and
native-ground checked, with hashes and zero rejected locations in the matching
report. `prepare_content.py` requires that evidence and installed visual/cell
compatibility before including them. The authoring script refuses to overwrite
an existing policy version; changed reward/state semantics require a new version.

The ordered obelisks grant two-hour bonuses. Ruby grants Aunmauri's Speed
(1.08 run velocity, secondary retail float stat 0x128 at 1.05), Sapphire
grants Courage (+30 maximum health), and Emerald grants Vengeance (+5 damage).
Returning to the Explorer grants Obelisk Ward (+6 armor for three hours).
Each timer survives relog and restart, and the same blessing in another
region refreshes it. Both DAT profiles supply their own effect WState IDs. The
subsequent mining/crafting lesson is implemented below. See the
[recovery notes](../../../Docs/ADMIN_CONTENT_RECOVERY.md#ringway-exploration-pack-v1-2026-11-01)
for exact assumptions and `Network/test_exploration_quests.py` for the real UDP
conversation/restart test.

## Native skill effects

`skill-effects.tsv` imports 1,602 use records from the installed Legions DATs.
`skill-effects.report.json` fingerprints the source skill/effect records and
records 325 withheld catalog entries (320 short roots and five ambiguous curves).
The importer checks the native object graph and typed list fields; instant
health/vigor values use field 36, and periodic values use field 38 in this build.
Scalar values must not be interpreted as references to graph object zero.
The server verifies the source hashes before admitting this profile.

`skill-runtime.report.json` inventories all 1,927 catalog entries. Currently 580
have executable core effects, including **236 racial profiles** (some already
used by starter attacks), and nine shared passive masteries are implemented.
These counts do not waive training, race, level, prerequisites or equipment
requirements. The remaining entries include passives, 431 profiles needing
specialized handlers and 29 recalls needing destination/entitlement handling.
The existing bound-lifestone recall retains its separate implementation.

The supported core includes direct damage, instant/periodic vigor drains, health/vigor restoration, periodic
healing and burns, regeneration multipliers, armor and melee/missile/magic
attack/defense ratings. Flame Bolt (`59`, native record `7000172d`) requires a
stave and uses its DAT cost, range, cooldown, rank-scaled damage, burn and story/FX.
Ease Suffering (`55`) exercises the self/other healing path. Casts validate the
learned skill, implement, target, visibility, range and cooldown before admission.
Failed admission does not spend vigor or cooldown. Burns use the existing kill,
quest-credit, loot and respawn paths; they cannot follow a corpse into its new life.
Damage casts still cannot attack protected NPCs or other players.

Vigor drain handling adds core execution for Dizzying Fumes, Disease Arrow,
Aggravated Energy, Energy Vortex, Convulsions, Neurotoxin, Embolism, Lightning Bolt,
Lightning Strike, Infect and Spiritual Malady. Native rank tables, training and
implement requirements still apply. Drains clamp to the target's remaining vigor;
misses apply neither instant drain nor periodic effects. Health and vigor ticks
can share a compatible timer, and caster cost is paid once. Creature vigor is
replicated to observers, selection vitals and examination, restored on evade/respawn, and does not
cause death or loot when it reaches zero. Creature spell AI and native ability
costs remain unfinished; ordinary creature melee attacks still cost no vigor.

This is **core single-target execution**, not complete retail spell behavior.
Area selection, multi-hit timing, combos, summons, status handlers, retail effect
stacking categories and persistence of timed buffs remain unfinished. Compound
effects with unsupported required components or incompatible timer layouts are
withheld. Generic rank damage is added to the existing authored encounter damage;
spell release timing, creature ratings and the 75%-at-equal-rating hit curve
(bounded to 5–95%) are AC2E policy. Shared mastery attack/defense formulas come
from the recovered 2012 Pyreal `Player.cs`, but that does not verify current retail
balance. Timed effects are bounded runtime state; health, vigor costs and rewards
use the durable player journal.

The importer and runtime were adapted from
[Pyrealium at 4687930](https://github.com/Vanquish-6/Pyrealium/tree/468793018b332b87228794b0720ea1ce0400f96d/emulator)
(AGPL-3.0), then corrected for the installed Legions schema and integrated with
AC2E's existing encounters. `skill-travel.tsv` retains 30 recovered travel records
as evidence, using the supplied Pyreal `Pyreal_DB_Insert_Telepad.sql`; it does not
enable those routes automatically. Raw DATs and reference archives stay unchanged.

Regenerate the import with `tools/import_skill_effects.py --game-data <DAT-dir>
--telepads <Pyreal_DB_Insert_Telepad.sql> --output content/skill-effects.tsv
--travel content/skill-travel.tsv` from `AC2E/emulator`. Regenerate runtime coverage
with `cargo run --example skill_coverage > content/skill-runtime.report.json`.
The importer tests check native field decoding. Rust tests cover casting rules,
cooldowns, mastery progression, periodic effects, expiry and durable burn kills.
`Network/test_active_skills.py` checks trained Flame Bolt over UDP with the actual
DATs, including stave requirements, protected targets, observer animation/FX and
burn ticks, using disposable characters and an isolated server.
`Network/test_vigor_loot.py` checks trained Lightning Bolt, observer vigor updates,
live examination, native boss loot bounds, ownership, stable corpse rolls and
preservation of rolled armor/level through inventory pickup over real UDP.

## Profession crafting and gathering

### Arwic Mines lesson

Speak to the existing **Arwic Mines Overseer at 44.0N, 6.1W**. Enter the
**New Arwic Mines at 45.6N, 2.6W**, defeat a Drudge Outcast and loot its Crude
Flat Hammer. Return in peace stance with the hammer in your backpack; double-click
the Overseer or use the hammer on him. You keep the hammer and receive 20 Iron
plus the special **Arwic Mines Short Sword** recipe under Skills → Craft → Quest
Recipes. Craft it from 20 Iron without a forge, skill rank or gold cost, then
return it the same way for 150 XP, an Arwic Outlands Glyph and The Shaper's Guide.
Double-click the book to read each of its 15 recovered pages in chat.

`arwic-mines-v1.json` pins the native four-phase journal text, recipe/item
identities, quality fingerprints, book pages and guide evidence. The learned
recipe persists and is rejected by validation/execution before the lesson.
Versioned `m1` transactions save progression, ingredients, output and rewards
together, including journal replay and compaction. Split Iron stacks are
accepted; insufficient space, stale ingredient IDs and duplicate turn-ins cannot
consume materials or duplicate rewards. The lesson repeats after 20 hours; a
retained hammer can be reused. Existing `q9` referral journal semantics stay frozen.

`arwic-mines-world-v1.tsv` adds four Outcasts and two Edge Beetles in six
collision-checked interior rooms in `7cb9`. Two additional capture anchors
failed the walking-patch checks and remain withheld. The existing Taskmaster
and Overseer are retained. Two bounded spawner groups own the six children;
the existing 30-second respawn delay never replaces a still-present corpse.
Native Outcast combat and AI properties are recovered reproducibly by
`tools/author_arwic_creatures.py <DAT-directory> <content-directory>`, with input
and output hashes in `arwic-mines-creatures-v1.audit.json`.

Room-to-template assignments are inferred from older capture positions and the
guide because those capture DIDs identify different items in the installed DAT.
The guaranteed hammer for an eligible quest holder supplements ordinary loot;
its original drop probability and spawn schedules are unrecovered. No wandering
radius is invented where the native template has none. The glyph reward is
implemented; the separate Arwic Vault unlocking/quest chain is not completed by
this lesson. `Network/test_arwic_mines.py` verifies the full exchange over UDP,
including three server restarts. Native-DAT tests separately check production
monster stats, aggression and corpse-aware spawner refill.

### Recovered profession catalog

`professions-v1.json` is generated by `tools/author_professions.py` using the
bounded native WState decoder in `tools/craft_catalog.py`. It contains 26 skills,
364 recipes (317 production/practice/potion, 20 refining, 15 surveying, four
dyeing and eight quest recipes), 855 item definitions, 14 workshop definitions, 56 mine definitions,
90 survey maps, six gathering tools and 1,396 eligible butchery entity profiles.
`professions-v1.audit.json` records the source WState SHA-256 fingerprints and
each excluded recipe with its reason. Raw DATs and reference sources are untouched.

Recovered inputs include skill names/icons/caps, cumulative advancement curves,
per-use XP where present, 22-hour learning-limit curves, recipe levels, ingredient
ordinals/types/quantities, success output weights, gold costs, workshop effects,
map regions, mine materials/yields/variance/uses and the 1,001-sample native
success table (`6f002fb9`). Production skills reach 500; gathering/refining reach
150. The legacy recipe index is supplemented by the installed Legions ordering
table and Empyrean dye recipe. Unsupported quest entitlements and special
mutation handlers are excluded rather than silently spending their materials.

### Playing the implemented loop

- **Skills → Craft → All professions:** choose a profession and recipe. The
  ingredient window shows level, cost and success chance. Use −/+ to change the
  output level, fill from the backpack, or click a row to cycle matching items.
  Requirements can span multiple stacks. Failed manufacture consumes its inputs;
  rejected requests consume nothing. Leftover quantities return atomically.
- **Raise:** spends that profession's saved XP at the native cumulative-curve
  difference. The 22-hour learning cap and reset persist through reconnects,
  server restarts and journal compaction. Combat XP remains separate.
- **Workshops.** Cragstone, Cavendo, Ikeras, Hakata, Linvak Tukal and Ondekodo
  each have crafting, refining and spellbinding facilities. Recipes require
  the corresponding native workshop effect within eight meters in the same scene.
  The Cavendo refinery retains its captured frame and model. The other 17 sites
  use the guide's individual service locations. The building footprints were checked
  against native ground and nearby NPCs. Archived photographs identify the models at
  Cragstone Crafting, Cavendo Spellbinding and Ondekodo Spellbinding. The remaining
  regional model assignments and uncaptured facings are inferred. Arwic keeps its
  three supplemental stations, with Crafting moved clear of the weapon vendor.
  Rebuild the source with `author_workshops.py` and validate it with
  `population_authoring`. [AC2Hero workshop coordinates](http://ac2hero.e5i5o.com/workshops.html)
- **Supply:** authored quartermasters offer 100-unit bundles of the basic and
  advanced raw materials. Purchases and refinery outputs merge into compatible
  stacks, including when the backpack is full. This deliberately permits crafting while broader
  gathering/loot coverage is unfinished; stock and prices are not retail economy.
- **Surveying:** create a continent/material map, examine its coordinates and
  use it within 40 meters on dry ground. Map generation checks native terrain;
  the selected location is journaled. A 15-minute per-continent cooldown resets
  when the map is found. The deposit appears beside the finder on reachable
  ground and retains its native three charges across restarts. Located deposits
  expire after one hour. At most 12 unfinished maps/deposits can be active.
- **Gathering:** carry a pickaxe (iron), mattock (silver), chisel (crystal), rock
  hammer (stone) or hatchet (wood). Double-click the deposit or target it and use
  the tool. Higher-level tools require gathering rank at least three times their
  level; level-one tools are always usable. Direct deposit use selects the best
  usable tool. Materials merge into compatible backpack stacks. Tool wear, XP,
  gathered materials and deposit depletion commit together.
- **Butchery:** select a defeated creature from your own recent kill and use a
  hunting knife within four meters. Native `ButcheryProfile` eligibility is
  required. Looting the corpse first does not remove the harvesting entitlement;
  it expires with the recent-kill reward window (60 seconds). Each corpse instance
  can be butchered only once. Friendly actors cannot supply butchery materials.
- **Creation tools:** equip a flat hammer (weapons), shaping hammer (armor),
  magnifying glass (tools), crucible (refining) or athame (implemented spellbinding
  component/practice recipes). A matching profession must be at least ten times
  the tool's level. Each tool level adds one effective rank as a provisional
  bonus; base rank, advancement costs and learning limits do not change. The
  recipe window labels its unmodified probability **Base chance**. Tool wear is
  charged on successful and failed attempts. Exhausted creation tools remain
  inspectable/discardable but give no bonus and cannot be re-equipped.
- **Dyeing:** native plants and race-specific armor predicates choose the finish.
  Dyeing retains the selected item's saved armor/damage/level, costs native gold,
  always succeeds and grants no XP. Other matching garments remain unchanged.

### Reconstruction boundaries

This is not a complete retail crafting implementation. The following policies
are intentionally explicit so later recovery can replace them:

- Most enabled recipes are default grants; the Arwic sword requires its saved
  quest entitlement, Nature garlands require the learned recipe, and Esper
  weapons require an active Drudge Outcast Camp quest. General discovery,
  account restrictions and most
  quest-specific recipes are not implemented. The existing
  Field Supplies/Potion Practice lessons still use their separate authored flow.
- Manufacture XP is twice `(difficulty + 1)` when no native per-use curve survives;
  trivial recipes more than 30 difficulty below rank grant none. Failure gives no
  XP. Refining interpolates 33% at rank one to 83% at 150 and always succeeds.
- Armor/weapon output statistics retain recovered base values plus two per output
  level after level one, bounded to 1,000; armor receives a saved tint. These are
  provisional playable values, not recovered retail mutation or critical formulas.
  Crafted stats participate in equipment/combat and inspection. Apparel recovery
  uses unambiguous native worn/pile appearance families, never guessed meshes.
- Mining starts from native continent yields (25/30/35) and ±15% variance. Survey
  and tool bonuses scale to +40 and +50 respectively. Tool durability defaults to
  20 durability where the template lacks a maximum; each use has a 50% wear roll.
  Durability is saved on the item, so transfer/pickup cannot repair a tool.
  Old character-based wear records migrate on use. Mining critical results,
  timed gathering animations and crafted armor/weapon wear remain unfinished.
- Butchery's original material tables are missing. Eligible arthropod families
  yield chitin, vermin yield Bone (Esper camp guide evidence), and other eligible
  families yield hide, at a reconstructed base of five with ±15% variance.
  Sinew still comes from supply stock/loot; this is not a recovered complete
  retail butchery distribution.
- Spellbinding Practice and component recipes work. AC2E also supports a
  workshop infusion for crafted weapons and body armor. At a spellbinding workshop,
  examine the equipment in your backpack and type `@bind`. A Good Luck Charm
  adds three weapon damage or armor points. Use `@extract` at the workshop
  to destroy the infused equipment and store its bonus in a Totem. Use the Totem
  on an uninfused piece of the same category, or examine it and type `@transfer`.
  Applying a totem consumes it and works away from the workshop. These are AC2E
  infusions. Random retail enchantments and harmonic stones need separate handlers.
- `@area on` shares half of an eligible direct heal or vigor restoration with
  up to three other injured fellowship members within six meters of the target.
  It applies only to the instant portion of a targeted support skill. `@area off`
  restores single-target use, and relogging resets the setting.
- Use a Pet Rock or type `@ward` to summon a stationary Healing Ward for 15
  seconds at a cost of 20 vigor. It restores five health every three seconds to
  its owner within six meters. Each player can have one ward. The ward disappears
  if the owner loses the focus, leaves the area, dies or logs out. `@companion`
  summons a hovering version using the same focus, cost and timer. It follows
  its owner where geometry permits and shares the ward's one-summon limit.
- `@cleave on` adds a bounded splash to eligible direct damage skills. A hit
  can strike up to three additional visible creatures within four meters of the
  primary target. Each additional target uses its own hit and armor check. The
  caster pays once. Skills that apply a status, damage over time or vigor damage
  keep their single-target behavior. `@cleave off` disables it, and relogging
  resets the setting.

Explosion, Earthquake and Volley apply their full strike to visible enemies around
the selected target (10, 10 and 5 meters respectively). Each rolls separately.
Explosion also applies the target's burn on a hit. AC2E limits each cast to 32
additional creatures. The skill request carries a selected object ID.
A trained Lugian Elementalist can cast Sand Fiend from the skill panel for one
vigor with a 90-second reset. The fiend walks toward its owner's combat target
and assists within melee reach. It persists until dismissed with `@dismiss`,
its owner leaves or it loses its owner. It shares the ward slot. Pet damage
uses five points per rank with the documented 50% variance. Combat timing, pursuit leash and hit rolls are
AC2E rules. Pet self-buffs and attackable pet health are pending.

Legions record `6f002775`, Enchantment Extraction, describes itself as a test
recipe. It takes a magical weapon and a Trophy of Wrath. The audited major/minor
recipe families and the native Generic Totem are separate records. AC2E uses
the Generic Totem's asset identity for its transferable workshop bonus. `RecipeAdaptor` exposes client recipe fields, and `gmWeenieDesc`
serializes summon ownership. These records do not supply the missing world
server formulas for extraction, skill area shapes or native pet AI.
- Active survey maps cannot be dropped or traded; survey jobs are owner-bound.
  Abandoning maps and specialist summoned mines
  need separate implementations.

The native data is the authority for recovered values. Historical descriptions
helped bound reconstruction: [crafting overview](https://asheron.fandom.com/wiki/AC2%3ACrafting_2.0_-_The_Ultimate_Newbie_Guide),
[mining](https://www.20oz.net/index.php/Mining.html),
[refining](https://www.20oz.net/index.php/Refining.html),
[toolmaking](https://20oz.net/index.php/Toolmaking.html) and
[spellbinding](https://20oz.net/index.php/Special%253ASearch/Spellbinding.html).
These span different retail eras and do not establish exact Legions server formulas.

### Verification and regeneration

Set `PYTHONPATH` to the absolute path of the repository's `Tools/AC2`, then run `tools/author_professions.py <DAT-directory>`
and `tools/recover_apparel.py --dat <DAT-directory>` from the emulator directory.
Workshop source is authored by `tools/author_workshops.py`; validate its placements
with the `population_authoring` example before running `Network/prepare_content.py`.
Do not treat source files alone as proof that the production manifest includes them.

Unit tests cover native recipe assets, XP caps/rank costs, partial stacks,
manufacture failure, insufficient gold, ownership/replay, dye preservation,
refining quantities, full-backpack gathering, transferred tool wear, creation-tool
rank/type restrictions, exhausted tools, quest armor sale restrictions, duplicate butchery,
full-backpack supply/refinery merging, survey cooldowns and durable deposit charges. `Network/test_professions.py` exercises
the crafting-to-mining loop over UDP and restarts an isolated server mid-deposit.
`Network/test_crafting.py --render` clicks the actual Unreal recipe, ingredient,
profession-browser and level controls, checks Raise visibility and recipe-title
clipping, and verifies saved potion/quest results.
Production account/character stores are not used by these tests.

## Wiki NPC conversations and placements

`npc-dialogue-v1.json` binds 217 exact native entity variants to 326 authored
conversation pages covering 229 quest topics. These summarize wiki facts and
selected walkthroughs; they are not recovered retail speech. Empty lore sections
are not replaced with invented quotations. Mirror aliases with identical quest
bodies share a topic, and discontinued quests are labeled rather than activated.

Ordinary double-click/use reaches this catalog after working quest, merchant,
tutorial and service handlers. `@talk` browses the selected NPC's information
without accepting a quest, spending items or opening its shop. Success returns
native use completion, named speech, and a readable MessageDialog. Repeat use
cycles pages; switching speakers resets browsing. Both paths require a living
player out of combat, a visible noncombat NPC and a distance of at most four
metres. The one-second limit applies across both paths. No rewards or journal
state are changed by informational conversation.

The [audit](npc-dialogue-v1.audit.json) covers 225 wiki names: all 89 NPC-category
entries, town NPC lists and exact quest-starter matches. Seventy-four category
entries match native data; 15 do not. Aliases and multiple actor variants explain
the different name/profile counts. `npc-world-v1.tsv` adds 27 previously missing
actors, including Brisin, Old Herrigar, Sachem Mimbu, Dark Agent, Justiciar Maronak,
Dell, Otonku, Feral Pet Lasher, Rashan's friendly spectre, the Gurog Supreme
Warlord and several Greenstone NPCs. Their coarse wiki positions are adjusted
by at most 32 metres to dry, reachable native terrain, at least six metres from
existing actors. This is inferred placement, not a recovered retail transform.
Existing identities are retained to avoid duplicate Orven/Adaro-style placements.

Lataua's coordinate is withheld: her article repeats a Cavendo position while
her town and quest walkthrough specify Zu. Ambiguous native variants are also
withheld. The Gurog High Shaman's proposed location failed native walkable-ground
validation and remains unplaced. Hostile Zroth/Rashan variants and
non-NPC quest-starter objects cannot receive friendly conversation through this
catalog. All catalog variants pass native combat classification checks.

This adds conversations and population, **not complete quest state machines**.
Most listed quests still need eligibility, item exchanges, stage transitions,
dungeon access and replay-safe rewards. Consignment, kingdom services and
unsupported vendor inventories are not synthesized from an NPC's name.

Regenerate with `tools/author_npc_dialogue.py` from the repository root. It reads
the retained wiki cache and `Saved/current-native-templates.json`; their hashes
are recorded in the manifest. Run the `population_authoring` example with the
DAT directory, `content/npc-dialogue-v1.json`, output `content/npc-world-v1.tsv`,
report `content/npc-world-v1.report.json`, and the prepared production world.
Run the authoring script again to annotate the audit with verified placements,
then `Network/prepare_content.py` to install the pack. The preparation step
rejects stale proof or mismatched native visuals. Source DATs remain unchanged.

`tests/npc_population.rs` validates placement, duplicate prevention and all 217
native noncombat identities. `Network/test_npc_dialogue.py` exercises every
profile over UDP, actual new placements, topic cycling, admission restrictions,
and preservation of the existing explorer quest action in an isolated store.
`Network/test_npc_authoring.py` verifies shared quest starters and avoids binding
a person named in an NPC's location as a second quest starter.

### Nature's Ally material exchanges

`nature-exchanges-v1.json` pins four guide-backed exchanges to native entity,
visual, physical and quality records. The [Nature's Ally walkthrough](https://20oz.net/index.php/Nature's_Ally.html)
specifies Rabbit Meat → Ancient Monouga Bone at Feral Pet Lasher, Scented Blossom
→ Wasp Egg Cluster at Outcast Wasp Queen, eggs → Succulent Truffle at Digger
'Dillo Mama, and a truffle → eight Anthracite Gems at Grutt Gourmand. All take
one input item. Their existing production placements are reused.

Normal NPC use gives authored instructions without spending anything. Select
the NPC, then double-click the input in the backpack to send the existing native
targeted-use request. This client binding is an AC2E implementation choice, not
evidence that the original client's Give command used that wire message.
`@talk` retains informational topic browsing. Visible recipient, four-metre
distance, living player, peace stance, account ownership, exact item identity,
appearance and a shared one-second interaction throttle are checked on the server.

The transaction removes the submitted stack, returns any remainder under a fresh
identity, and grants the reward in one durable inventory journal entry. It admits
the complete outbound update before persisting. Insufficient backpack space fails
without spending the input; a single-item trade can reuse the consumed item's
slot even in a full bag. Repeated requests using consumed identities cannot duplicate rewards.
Native Rabbit Meat and Anthracite Gem stack limits are ten; historical singleton
loot with limit one remains accepted. Rewards use recovered native item visuals
and transport metadata. Separate reward stacks are not automatically consolidated.

The four trades and the Black Mother Earth path below do not complete Nature's
Ally, award quest XP or its final items, or open Chaos Dungeon. No overall quest level gate
or cooldown is imposed on these exchanges without evidence for that restriction.
Guide instructions are paraphrased; retail NPC quotations have not been recovered.

Regenerate from the repository root:

```powershell
python AC2E/emulator/tools/author_nature_exchanges.py "Asheron's Call 2 - Legions" Saved/current-native-templates.json Saved/ContentRecovery/wiki-content.json AC2E/emulator/content/nature-exchanges-v1.json
```

`nature_exchanges` unit tests cover amounts, stack remainders, singleton loot,
full bags, wrong items/identities and replay rejection. The DAT-backed integration
test verifies all fourteen NPC/object/item assets, eight item transport records,
and the native Flower Garland recipe fingerprint, ingredient slots and cost.
`Network/test_nature_exchanges.py` exercises the actual four world placements,
network hand-ins, rejected uses and persistence through two server restarts.
Its inputs are seeded offline in a disposable store; the separate gathering test
below obtains Scented Blossom through world interactions. The existing all-NPC dialogue
test continues to cover topic browsing and precedence of working quest actions.

### Black Mother Earth blossoms and learned crafting

The same guide and native profile now support the material path leading into
the Wasp Queen / Digger Mama / Grutt Gourmand exchanges. Double-click one of the
three existing `47002301` Flowering Bush placements to collect Withered Blossoms
(`470022f3`). At the existing `4700230b` Shrine to Black Mother Earth in Ariaki
River Basin (13.5N, 61.0E), select the shrine and double-click one blossom to learn
`6f001fe4` **Flower Garland**. The native recipe has seven one-blossom ingredient
slots, a 100-gold cost, no workshop requirement, and one `47002355` Garland of
Flowers output. Offering that garland to the shrine yields one `470022f9`
Scented Blossom. Normal shrine use gives instructions without consuming items.

The guide specifies eight blossoms in total (one for learning and seven for
crafting). It does not establish bush yield, resource-sharing semantics or
regrowth time. AC2E reconstructs **four blossoms per harvest, with a five-minute
cooldown per character per bush**. It reuses captured/prepared placements; this
does not recover the guide's random-bush spawning distribution. There is no
claimed retail level gate or overall Nature's Ally timer on this material path.
Recipe knowledge persists; another blossom is not consumed after learning.

`nature_gathering` plans the version-one `n1` journal transaction. Collection,
cooldown, learning, all ingredient changes and the native crafting cost commit
atomically after complete transport validation. Replay and journal compaction
preserve the entitlement and cooldown. Recipe initialization and ingredient
validation hide/reject the unlearned recipe. The native seven slots may share
stack identities; the server reserves seven actual units across validated owned
stacks before consuming anything. Removed stacks and remainders receive the
same native inventory notifications as other material exchanges. Failed funds,
capacity, range, ownership, stance, clock or replay checks leave inventory and
quest state unchanged. No quest or profession XP is granted by these steps.

`Network/test_nature_gathering.py` collects its inputs from actual bush placements,
learns at the actual shrine, crafts, and continues through the three downstream
NPCs to eight Anthracite Gems, with two server restarts. Only an isolated Idol
encounter's health/gold are adjusted to supply the 100-gold test budget; quest
items are never seeded. Unit tests additionally cover full backpacks, cooldown
expiry, insufficient total quantity, insufficient gold, and journal compaction.
The remaining Redberry/Spirit branches, summoned actors, dungeon progression,
whole-quest timed state and final rewards remain incomplete.

### Kehan windmill and the Esper referral

This continues the starter route after the Arwic lesson:

1. After returning the Arwic Mines Short Sword, speak again to the Overseer at
   **44.0N, 6.1W** for **Find the Miller of Kehan**. Reach the Miller at
   **42.5N, 11.2E** within 48 hours for 30 XP. The windmill task can also be
   started directly at the Miller without this optional referral.
2. The Miller gives **The Miller's Map** and starts the 24-hour windmill task.
   Use the map for coordinates, travel south of Kehan to approximately
   **40.4N, 11.9E**, and use it within the native 70-metre radius to reveal your
   Stone Quarry. Craft a **Rock Hammer** under Skills → Craft → Starter Tools;
   its current native recipe requires 13 Iron and a nearby crafting workshop.
3. Select the quarry and use the Rock Hammer. The native mine permits three
   harvests, with a base yield of 25 Stone and 15% variance. Tool wear and
   deposit ownership use the existing gathering system.
4. Return to the Miller, select her and double-click Stone in your bag. The
   trade spends **one Stone**, preserves the rest of the stack and gives a
   **Massive Gear**. Merely speaking does not spend materials. Hand-ins require
   a living player in peace stance within four metres.
5. At the **Windmill near 47.0N, 11.5E**, select the gear receiver named
   Windmill and use the Massive Gear. Return to the Miller for **200 XP and
   one Lost Wish Glyph**, then follow **Find the Esper Reaches Warder** to
   the existing Esper Warder at **37.5N, 19.9W** in Millstone.

`windmill-v1.json` pins the native map, gear and glyph quality records, the fixed
map destination, recovered journal fields and retained wiki references. The
server uses authored guidance for the current controls. The existing Miller
and Warder placements are retained. `windmill-world-v1.tsv` adds one missing
`GearWindmillKehan` receiver at the guide's coarse windmill location, grounded
against the native terrain/collision. Its position is reconstructed, not a
captured retail coordinate; the report and manifest hashes are checked by
`Network/prepare_content.py` before inclusion.

The `w1` durable transaction records quest phases, survey ownership, all item
changes and XP together. Quarry charges and tool wear use the profession
journal. Reconnects and compaction preserve both. Expired tasks replace their
old map/gear and survey jobs; lost maps or gears can be recovered from the
Miller without duplicating a live deposit or resetting the original deadline.
Full backpacks, wrong items, invalid ownership and repeated consumed identities
fail before anything is spent. The windmill repeats 20 hours after completion;
the Miller referral can repeat after another Arwic completion and its own
20-hour cooldown. Visiting the Esper Warder completes the referral without XP.

The guide overview awards 200 XP, while its transcript says 150; this version
uses the overview. An older Miller Quest page says zero XP, while Find the
Miller of Kehan says 30; this version uses the latter. These conflicts are
recorded in the manifest. Coordinate guidance replaces the retail hot/cold
survey messages. The Miller grants Renewed Focus for three hours. In both
installed DATs, its stat modifier references maximum vigor (0x40108) and
its magnitude table (field 36) contains +40. The guides give +10.
The repaired windmill animation and the Lost Wish/Esper vault chains
remain incomplete. Esper's weapon lesson is implemented below.
Receiving a glyph does not implement its vault progression.

Regenerate the pinned manifest with
`python AC2E/emulator/tools/author_windmill.py`, then regenerate the placement
and report with the `population_authoring` example using `windmill-v1.json`.
The authoring tool requires the retained native-template export and wiki cache.
Unit tests cover quest ordering, expiration, referral repeats, partial stacks,
full bags and duplicate rewards. Store tests cover journal replay/compaction;
the DAT test checks all three item assets, quarry definition and fixed site.
`Network/test_windmill.py` crafts its tool and obtains its quest materials
through real UDP interactions, checks the three production actor identities,
and traverses the entire windmill/Esper route across three server restarts.


### Warder introductions after Esper

`warder-referrals-v1.json` implements three conversations using native quest/NPC
identities and cached guide evidence:

- **Find a Warder in Cragstone** (`400000cf`): after Drudge Outcast Camp, speak
  to Esper Warder again, then the Warder of Cragstone at **32.8N, 12.7W**.
  Acceptance requires level 3; the Cragstone gateway requires level 6.
- **Find the Warder Corporal** (`400000d1`): speak to the Warder of Cragstone,
  then the Corporal near Rithwic at **23.9N, 4.3E**. Direct acceptance works.
- **Find the Warder Scout** (`400000cc`): speak to the Corporal, then the Scout
  near Molwirth at **14.0N, 0.6E** within **48 hours**, for **30 XP**.

The first two have no recovered deadline or XP reward. All three repeat after
20 hours; an expired Scout referral can be accepted again at the Corporal.
Completion and the next eligible introduction happen in one conversation.
`wr1` journals the transition and reward atomically; repeated interactions and
server restarts cannot duplicate rewards. Existing NPC placements are reused.
Regenerate with `python AC2E/emulator/tools/author_warder_referrals.py`.

The Notebook and **Seek the Warder Captain** continuation is implemented in its
own profile below. **Seek Dantry the Warder** remains excluded because its Potion
Raid prerequisite is not implemented. NPC information pages are not evidence
that those tasks are playable.

### Esper Warder and Drudge Outcast Camp

After the Kehan referral, speak to the **Esper Warder in Millstone at
37.5N, 19.9W**. The same conversation completes the referral and begins the
24-hour **Drudge Outcast Camp Quest**. Direct acceptance also works.

1. Use a Hunting Knife on defeated vermin to gather **25 Bone**. Craft a
   **Drudge Cadre Slayer** weapon under **Skills → Craft → Quest Recipes**.
   No crafting rank or workshop is required. These native weapons do 22 base
   damage, require level 6 to equip, and retain their racial restrictions.
2. Travel via the Southwest Esper ringway outpost, then follow the road east
   to the island southeast of Millstone. Defeat a **Drudge Outcast Cadre**.
   The special weapon triples direct weapon-bearing hits on Cadres; periodic
   burns and attacks on other monsters do not receive the bonus. Using the
   special weapon is optional for completing the quest.
3. Open your defeated Cadre's loot, collect its **Drudge Cadre Bone**, then
   return to the Warder. Select him and use the bone, or speak with him while
   carrying it. Receive **320 XP and an Esper Glyph**. Repeat after 20 hours.

`esper-camp-v1.json` pins native items, original journal text, guide provenance,
Slayer/Sound Heart effect graphs and the three captured camp rows in block
`67AA`. Existing independent point spawners are reused; no invented extra
Cadres are added. Native stats retain level 10, 285 health/vigor, 30m perception,
60m homesick range and loot profile 954. The retained creature guide says level
8; native DAT values take precedence. Respawn timing remains AC2E policy.

`e1` transactions atomically commit quest state, material consumption, crafted
items and rewards. Crafting combines Bone stacks and preserves leftovers.
Quest recipes are available only during an active run and removed on completion;
server validation rejects stale UI requests. The guaranteed bone is restricted
to an eligible corpse owner, deduplicated against observed drops, cannot be
traded, and advances the quest only after successful pickup. Full bags and
invalid hand-ins do not consume inputs. Expired runs require a new bone;
restarts retain deadlines and cannot duplicate rewards.

All six recovered weapon recipes are exposed while active because the original
three-recipes-per-race assignment is not recovered. This is a documented UI
approximation; the native six outputs and equip restrictions are used. The
**Sound Heart reward remains unimplemented**: its retained graph contains a
10,800-second duration row, stat `0x40107`, and a final value row of 40, but the
server does not yet apply timed maximum-health modifiers. The separate Esper
Vault chain is also unfinished; receiving its glyph does not complete it.

Regenerate with `python AC2E/emulator/tools/author_esper_camp.py` and
`author_professions.py`. `Network/test_esper_camp.py` checks production placement
counts and the full UDP path, actual vermin butchery, recipe restrictions,
material conservation, combat/loot, wrong hand-ins, and three server restarts.
Rust tests cover native assets, six recipe inputs/outputs, full bags, expiration,
replay/compaction, actual Slayer combat and camp respawn lifecycle.

### The Warder Scout's Notebook and Seek the Warder Captain

At **level 10**, speak to the **Warder Scout near Molwirth, 14.0N, 0.6E** to
accept the task. Completing the prior introductions is optional. If Find the
Warder Scout is active, this conversation completes that referral as well.

1. Defeat the **Drudge Slinker Bandit** at the camp near **15.5N, 1.7W**.
2. Open the corpse and take **The Warder Scout's Notebook**. Killing the Bandit
   and collecting the notebook are separate native journal stages. If its corpse
   expires, another Bandit can supply the notebook during the same active task.
3. Return within **24 hours**. Select the Scout and use the notebook, or speak
   to him while carrying it. Make room for three rewards: **Emblem of Order**,
   **Mad Crone Glyph** and **Prosper Glyph**, plus **800 XP**. Hand-in needs two
   free backpack slots in addition to the space occupied by the notebook.
4. The hand-in starts **Seek the Warder Captain**. Enter the **Warder's Hideout
   at 20.6N, 14.0E**, stay left through the Lasher caves and climb the circular
   ramp. Speak to the Captain within **48 hours** for **2,500 XP**.

Both tasks repeat after 20 hours. Speak to the Scout to restart an expired
Notebook task or renew an expired Captain introduction. Deadlines and rewards
persist across restarts. `wn1` journals whole transactions, and `qk3` advances
the kill stage without changing earlier kill-record replay semantics. Full bags
do not consume quest inputs or award partial rewards. Old notebooks are removed
when restarting an expired attempt; old corpse drops cannot satisfy a new one.
Notebook drops require an active eligible owner, ignore generic captured copies,
and cannot be traded or dropped. No fellowship-shared quest credit is claimed.

`warder-notebook-v1.json` pins native assets, original localized journal text,
cached guide evidence and three existing recovered placements. The native Bandit
records include death effect `Eff_AdvanceQuestWarderScoutPhase2` and loot profile
949; the notebook has acquire effect `Eff_AdvanceQuestWarderScoutPhase3`.
The guide's Scout at 25.0N, 3.8E and Bandit at 28.1N, 3.9E conflict with retained
placements. This implementation preserves those placements and gives directions
to the live actors; it does not claim the differing sources describe one layout.
Existing Bandit spawners are reused; respawn cadence remains authored AC2E policy.

Regenerate with `python AC2E/emulator/tools/author_warder_notebook.py`, using the
retained native-template export and wiki cache. Rust tests cover native asset
validation, level/phase gates, deadlines, full bags, duplicate hand-ins, old corpse
eligibility and durable replay/compaction. `Network/test_warder_notebook.py` checks
the production actor rows and the full conversation/combat/loot/hand-in route
through native UDP messages across four server restarts.

**Drudge Potion Raid, Seek Dantry and the separate Mad Crone/Prosper vault chains
remain unfinished.** Receiving their glyphs does not implement those chains.

## Native spell presentation

`skill-presentation.json` recovers 906 cosmetic profiles from the installed
Legions skill hook graphs. `import_skill_presentation.py` retains the attacker
cast FX (one-based hook value 19, enabled by value 20 bit `0x40`), release FX
(21, bit `0x2000`), and missile entity (7), then resolves the entity's visual.
The existing skill-effects import supplies target FX and gameplay. Source skill
and missile records are SHA-256 pinned and validated at server startup. Multiple
distinct cues in one skill are rejected instead of silently picking one; the
current extraction has no rejected records. Regenerate with:

```powershell
python AC2E/emulator/tools/import_skill_presentation.py --game-data "Asheron's Call 2 - Legions" --output AC2E/emulator/content/skill-presentation.json
```

Named offensive skills now publish their own caster FX at cast start, release
FX after the existing windup, and native missiles before target impact. Support
and self effects release with their committed result. A recipient application
cue is queued even for HoTs or buffs without an instant heal; recipient visual
stamps remain owned by that recipient. Periodic damage no longer causes fresh
racial impact effects on every tick. These records do not grant skills or make
unsupported summons, area attacks, or other gameplay executable. Unconverted
7230 servers retain their existing presentation; Legions identities are not
applied to that build. The 350 ms windup and 22 m/s projectile timing remain
authored server policy, not a claim of recovered retail timing.

Flame Bolt (`59`) uses fire cast `81000281`, release `410004A9`, and mesh missile
`47000D04` / `1F000B85`. Its appearance is a native material/mesh effect, whereas
Fireball (`58`, `47000306` / `1F000189`) has a particle trail. Flame Bolt's legacy
target key `41000241` has no references in the installed FX tables, visuals,
appearance tables, or FX scripts. It remains unresolved rather than receiving
an invented replacement; the cast and missile are now visible. Ease Suffering
uses caster `810001D0`, release `410004A8`, and recipient `41000033`.

A follow-up reference scan across all 235 currently executable Human, Lugian,
and Tumerok core profiles found no unreferenced cast cues. Remaining unreferenced
keys are the Death's Grasp release (`8100005D`), Heart Rend / Soul Rend impact
(`4100030E`), and the shared Flame Bolt / Pyroclastic Flow impact (`41000241`).
Asset references alone do not prove rendering; the representative particle
checks below verify actual playback separately.

Verification: importer flag/graph/entity tests; server cast/release/flight/impact
and burn regression tests; `Network/test_active_skills.py` and
`Network/test_support_skills.py` against disposable real UDP worlds; and
`AC2.Data.NamedSpellParticles` in Unreal. The client test isolates 21 cues on
Human, Lugian, and Tumerok bodies, verifies actual particle counts and expiry,
and checks Fireball particles plus Flame Bolt geometry/materials. Existing
combat, animation FX, and projectile script tests also pass. This is automated
simulation/render-data verification, not a side-by-side retail playthrough.

### Osteth vault creatures, glyphs and Prosper dungeon travel

The Arwic and Esper vault recovery pack publishes 28 actors in 27 room spawners:
14 Arwic Drudge Outcasts, 13 Esper Drudge Outcasts and one Esper Drudge Keeper.
Each row in `vault-world-v1.json` retains its capture identity, original entity,
room, position, scale and orientation. The generator resolves the corresponding
Legions template and requires reachable native ground with clearance before it
publishes the row. Eight source positions failed these checks. Spawner membership
follows captured room occupancy; the observed headcounts are a reconstruction.

Arwic, Esper and Lost Wish glyphs can drop from their documented Vermin variants
inside the corresponding outdoor Ringway. The 10% drop chance is emulator tuning.
Explicit operator loot overrides and quest item awards retain their own rules.
Sources: [Arwic](https://20oz.net/index.php/Arwic_Vault.html),
[Esper](https://20oz.net/index.php/Esper_Vault.html) and
[Lost Wish](https://20oz.net/index.php/Lost_Wish_Vault.html).

Fiery Crypt and Illuminated Drudge Cave have working entrance and exit routes.
Both exterior entrances and interior exits use recorded portal positions. The
Fiery Crypt entrance appears in two January 2017 captures and the Illuminated
Drudge Cave entrance in five. All four destinations come from named Pyreal
telepads 681/682 and 126/125. The region guide records a level-10 entrance for Illuminated Drudge Cave.
Fiery Crypt uses an unrestricted entrance because the regional guide gives no
entry level and the walkthrough sends level-18 players there. These routes provide
travel; individual dungeon quests and encounters require their own data.
Sources: [Prosper Marches](https://20oz.net/index.php/Prosper_Marches.html)
and [Illuminated Drudge Cave](https://20oz.net/index.php/Illuminated_Drudge_Cave.html).

Player-dropped ground items expire after 15 minutes (emulator tuning). The
deadline survives restarts, and existing drops receive one full lifetime when
their original save is first migrated. Corpses and authored world spawns use
their own lifecycles.

Illuminated Drudge Cave has 17 additional native combat spawns in 10 rooms.
The January 2017 captures listed in `illuminated-cave-v1.json` independently
record these creatures at the same coordinates. Installed templates match by
name and loot-profile ID. Combat and AI properties for the Illuminated Drudge
come from its installed DAT inheritance. Grounded creatures use the local
floor. The boss, Mystic and three Cabalists retain their captured hover height.
Four candidates failed clearance or spacing checks. Room group capacities
reflect simultaneous placements. The regeneration schedule is emulator tuning.
The cave walkthrough places the manuscript on the ground behind the boss.
It is excluded from the boss's treasure table.
[The Illuminated Manuscript](https://20oz.net/index.php/The_Illuminated_Manuscript.html)

The [loot catalog](#loot-and-idle-ai) uses captured local and species bundles
to fill native generic slots, then uses the shared loot profile and reconstructed
common pool. Native trophies have separate rolls.

### Aerfalle's Generals

Four singleton spawners place Baron Colier (31.2S, 56.8E), Lord Nuvillus
(35.4S, 51.2E), General Dreyvon (28.9S, 61.3E) and General Sarocete
(27.6S, 64.2E) at the guide's reported coordinates. Native land heights and
creature clearance were checked for each spawn. Levels 56, 58, 60 and 62,
health, collision dimensions, scale and corpse identities come from the DAT.
Facing and the two-minute respawn interval are inferred.

Each general carries their own quest key. The implementation awards one per
corpse based on the walkthrough. The original key probabilities are unknown.
Colier and Nuvillus also roll the DAT's independent 5% Skeleton Helm slot.
The corpses require level 45. Each key requires its corresponding active quest
objective, including during bulk looting. Foundry Gates and the Troop Status
quest starter are still missing, so players cannot yet qualify for the keys
through normal play. The authoring inputs and their hashes are in
`foundry-generals-v1.json`, generated by `tools/author_foundry_generals.py`.
[Aerfalle's Generals walkthrough and map coordinates](https://www.20oz.net/index.php/Aerfalle%27s_Generals.html)

### Warder Hideout and guide-documented outdoor encounters

The Warder Hideout entrance at 20.6N, 14.0E and both surface exits are in
`warder-hideout-v1.json`. The guide floor plan, two 2017 captures and Pyreal
telepads 136 and 137 establish the routes. Four spawners support five Lashers
at positions with native floor contact and body clearance. The Warder Cache
has its own access requirements and is outside these routes.
[Hideout floor plan](http://ac2hero.e5i5o.com/maps/dungeonimg/AC2_Warder_Hideout.png)

`hero-wilderness-camps-v2.json` places 59 creatures in 25 outdoor spawners
in Omishan and Knorr. Each camp cites the guide behind its location and roster.
Several Combat Archaeology digs and the Darkstone encounter now have guards.
Exact spacing, unrecorded group sizes and the two-minute respawn are inferred.
These placements alone do not implement the associated quest scripts.
[Combat Archaeology](http://ac2hero.e5i5o.com/quest/knorr_combatarchaeology.html) �
[The Darkstone](http://ac2hero.e5i5o.com/quest/omishan_darkstone.html)

Lupe in Hakata starts Shreth-Hide Leather. The Scarred Shreth Hunter near
39.2N, 68.1E drops the quest rawhide for eligible characters. Lupe takes the
hide and 100 Tallow, and Gallus in Whitebay accepts the tanned leather for
120,000 XP, 1,250 gold and 600 quest points. Shreth sharing the Hunter's native butchery profile can
yield Hide or Tallow. The guide identifies both materials. Their relative odds
are inferred. [Quest walkthrough](http://ac2hero.e5i5o.com/quest/omishan_shrethhideleather.html) �
[Butchery traits](http://ac2hero.e5i5o.com/traits.html)

In the Aerie, Jasper Urleg, Temakhe and Bertrand Jas offer three repeatable
hunts. Together with the existing fourth hunt, they unlock Otonku's Aerie
Report for delivery to Maekarn in Fort Strathelar. The encounter IDs, quest
timers, reward calculations and source pages are in `hunting-content-v5.json`.
[Journey to Fort Strathelar](http://ac2hero.e5i5o.com/quest/knorr_journeytofortstrathelar.html)

### Cragstone Excavation

The exterior entrance and both return portals use frames recorded in two
January 2017 sessions. Their destinations come from Pyreal telepads 674-676.
Twenty-six room spawners reproduce 44 observed inhabitants, including the
Breeder of Pestilence. Twelve floor positions needed an adjustment of at most
one metre horizontally in the same room. The source frames and adjusted
positions are retained in the placement catalog and report.
[Cragstone Excavation floor plan](http://ac2hero.e5i5o.com/maps/dungeonimg/AC2_Cragstone_Excavation.png)

In the captured corpses, Slinkers carried Jagged Seal Fragments (6 of 15),
Prowlers carried Sharp fragments (4 of 16), and Raiders carried Small fragments
(3 of 15). These observations determine the emulator's provisional chances
inside the Excavation. For Shear Beetle Flesh the 2017 DAT supplies the 100%
trophy rate. The Order Cache and sealed library remain separate content gaps.
[Cragstone Excavation walkthrough](http://ac2hero.e5i5o.com/quest/osteth_cragstoneexcavation.html)

### Omishan forts, temples and trackers

Four Sclavus forts have their guide-listed preceptors and guards. The four
talisman pickups and their matching temple doors have separate capture records
in `hero-wilderness-camps-v2.json`. Their quest phase restrictions still
need a server implementation. The location recorded while looting the boss
identifies the chamber, but only the player's position was transmitted.
[The Sclavus Hierophant's Cabals](http://ac2hero.e5i5o.com/quest/omishan_sclavushierophantscabals.html)

The Warder Tracker involved in Temple Search can be found in Zu at
16.1N, 73.3E. The shrines to Mother Audetanga, Blue Mother Heaven and Brother
Tanae are placed near their guide coordinates. AC2E also places the Frostbound
One near 3.0S, 37.9W. These actors currently provide information. Three other
shrine candidates and the Totem of Vakentu lack a verified footprint within
ten metres of their guide points.
[Temple Search](http://ac2hero.e5i5o.com/quest/omishan_templesearch.html) /
[Icefire](http://ac2hero.e5i5o.com/quest/linvak_icefire.html)

### The Ossuary

The exterior entrance and interior exit match four January 2017 captures.
Pyreal telepads 1027 and 1028 supply the destinations. The exterior return
uses the player's first stationary ground position after the recorded airborne
arrival, 4.4 metres from that arrival. Sixteen dungeon spawners provide ten
Revenant Legionaries, six Revenant Prefects and Ossein, Digger of Graves.
Fifteen other recorded positions failed the installed DAT's body clearance
checks. Outside, one Servant of the Bones can appear at three possible homes
94 to 146 metres from the entrance. The guide documents the encounter and its
quest. The three exact positions and two-minute refill are AC2E reconstructions.
The Gurog Captain's exact native variant and the Trove encounter are
still unresolved.
[Kill Ossein](http://ac2hero.e5i5o.com/quest/linvak_killossein.html) /
[Ossuary floor plan](http://ac2hero.e5i5o.com/maps/dungeonimg/AC2_Ossuary.png)
