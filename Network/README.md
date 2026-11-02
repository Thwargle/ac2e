# AC2 native networking (0.11.0)

The Unreal client connects to AC2E and Pyrealium using native AC2 UDP, not Unreal replication. The separate Rust bridge uses AC2_Rusted's checksummed, reliable native transport and codecs. Private anonymous pipes carry JSON between Unreal and the bridge; no IPC network port is opened. Object IDs remain decimal strings across JSON to preserve all 64 bits.

## Start a local server and client

From the workspace root, in separate terminals:

```powershell
.\AC2E\Start-AC2E.ps1
.\Launch-AC2.ps1 -Online -Server 127.0.0.1 -Port 9000 -Account local
```

The root `start-ac2e.bat` starts the same server project, reusing the built binary and building only if it is missing. Use `-Rebuild` after changing server source; `-SkipBuild` prevents even a missing-binary build. Create a character in Unreal; it is saved by AC2E under `AC2E/data/world`, independently of offline characters. Stop gracefully with `AC2E/Start-AC2E.ps1 -Stop`.

AC2E normal startup includes an account service on loopback port 19400. First sign-in
creates an account; later sign-ins require the same password (12–128 characters).
The login form defaults to that local service. For a remote Pyrealium server with an account service:

```powershell
.\Launch-AC2.ps1 -Online -AccountUrl https://accounts.example.com -Account myaccount
```

The game opens its account/password form before loading any environment. Enter the account-service URL and password there; the client obtains the server's one-use native ticket with Windows WinHTTP. HTTPS is required except on loopback, and redirects are disabled. Passwords and tickets are not put on command lines or in client logs/save files. Tickets go to the bridge through private stdin. For servers that accept credentialless native login, leave the account-service URL/password blank and enter the host/port. With password login, host/port come from the trusted account endpoint. Explicit launcher `-Credential` automation remains supported through a child-process environment ticket. Public Internet interoperability has not been tested against an independently operated host.

## Network controls and authority

WASD/Shift/Space move. Enter opens server chat (`/say`, `/me`, `/tell Name, message`). T cycles nearby server objects; Z toggles combat/peace; 1 requests repeated attacks. The equipped weapon selects the native melee, missile or magic stance. 2 uses the racial starter magic skill and 3 uses the missile skill; equip a matching weapon first. `/cast <hex skill ID>` sends a learned active skill request; unsupported skills are rejected by the server. `/stop` ends combat. E or `/use` uses the selected world object, including mentors, portals and lifestone binding. Home or `/recall` requests native lifestone recall; a dead character requests resurrection. I opens the native inventory; double-click an item to use/equip it (unequip the current hand item before replacing it). C displays server stats. F4 or Escape → Leave World logs out to character selection. Escape on selection disconnects to sign-in. `/loot` sends the native loot-container request when supported by the connected server.

The cursor starts visible. Hold RMB to look/steer, drag LMB to orbit, and hold both
to run; release to restore the pointer. Click a visible creature to select it, then
press 1 to attack. World picking respects walls and excludes hidden cells, held
weapons and inventory objects. The native start/stop buttons enter combat/peace
even without a target; their ghosted states follow server acknowledgements. Z
also toggles the offline sandbox's unarmed stance. An attack without a selected
target shows guidance in the world log.

Server characters, positions, visuals, appearance aspects, actor visibility, inventory, health/vigor, levels, XP and gold update from server events. Remote actors interpolate server movement and use native locomotion animation. Client collision and prediction are retained for local movement. NPC test spawning, flight and arbitrary map teleports are disabled online. Inventory changes wait for acknowledgement; no network profile is written to the offline character file.

The AC2E initial ruleset tests **automatic loot grants** on supported mob kills. It does not yet supply a complete retail corpse-looting window, every combat animation, every merchant/quest/social panel, terrain-authoritative server collision or anti-cheat parity. Static recovered placements and authored encounter behavior are distinct; seeing an NPC does not mean its full original script has been implemented.

The Legions profile grants DAT-validated racial starter clothes and three weapons once per character. Equipment acknowledgments update native inventory, and held models follow animated holding locations. Clothing changes rebuild the native avatar appearance while preserving face/body traits and colours; current observers, later arrivals, selection and re-entry receive the saved result. Seven additional armor definitions support Human/Lugian torso, legs and feet and a Tumerok torso, including sex-specific appearance tables and server armor values. Unequip the current piece before wearing its replacement. Additional training Drudges drop these profiles after the relevant quest item has been collected; this is an authored loot policy.

Starter attack IDs and animation modes come from DAT tables; damage, vigor cost, range and timing remain development rules. The client runs native animation FX triggers and timed 0x57 FX scripts, plays particle variants and temporary effect models, and retains target references. AC2E sends native arrow/boulder/javelin and racial auto-magic entities through CreateObject/InitializeMissile. Release is currently an authored 350ms delay and flight uses an authored 22m/s speed; damage is revalidated and applied after flight. Starter magic skills use direct cast/impact scripts. Distance-driven DAT trail emitters now run even with a zero timed birth rate. Objects are removed from every original recipient at impact, including after a disconnect or area change. Native ballistic parameters, sound/light/streak effects and full spellbook parity remain incomplete.

Animation playback keeps native clip periods, normalizes sparse blend-cube timing,
uses DAT start/stop ramps, and blends interrupted actions from their outgoing pose.
Body, head and equipment continue sharing phase. No global time-dilation slowdown is applied.

The supported human dungeon circuit is: equip a weapon, defeat three Idol Spawn, use the mentor for the reward, bind the lifestone, use the training gateway to the outdoor hub, then recall. This verifies the **authored AC2E starter circuit**, not the recovered retail tutorial's full quest chain. Starter Idol pursuit is server-owned, limited to its current cell, with an 18m leash; this is not cross-cell pathfinding or line-of-sight collision. Both current observers and later arrivals receive its authoritative pose; respawn returns it to the original placement.

Separately, the Shelter Guard/Lieutenant/Sentry introduction and training-stone
text use native DAT StringInfo references, with per-character durable progress.
`@quest shelter` reports its status. The continuation covers Digger/tool/Scout,
Sentinel/glyph/portal, Advance Scout and a reconstructed Menhir completion step.
Item consumption, rewards and glyph travel use durable atomic transactions.
See the AC2E guide for reconstructed placements and remaining retail gaps.

## Build and verify

The bridge carries native `WM_Craft` registry, ingredient-validation, execution
and rank-increase messages, including full-width profession XP and item IDs.
Skills → Craft lists 356 default recipes across 26 disciplines, plus the Arwic
sword recipe after the Overseer teaches it. The server
rechecks ingredients, recipe level, workshop distance, ownership, life/combat
state, costs and cooldown before committing inventory and progress together.
Manufacturing, refining, dyeing, surveying, five mining disciplines and butchery
use the same durable journal. Recipe grants and missing server formulas are
explicit AC2E reconstruction policies; spellbinding extraction is not enabled.
The authored mentor's Field Supplies/Potion Practice continuation is enabled in
the Unreal profile as well as the development-kit profile. These lessons and
recipe grants are AC2E policy, not recovered retail entitlement rules.

`Network/test_crafting.py` checks the entire mentor prerequisite chain, both
potion recipes, rejection of wrong/foreign ingredients, unknown recipes, combat
and stale requests, then restarts the server to verify persistence. Add `--render`
to click the native Craft tab, recipe row, craft and close buttons in Unreal and
verify the inventory and quest updates after restarting the server. These tests
use isolated ports 19528/19529 and save evidence in `Saved/AC2NetworkTests`.
`Network/test_professions.py` verifies multi-stack manufacture, XP/rank increases,
item stats, refining, pickaxe manufacture, survey cooldowns, native-ground
deposit discovery, mining, depletion and restart persistence over real UDP.
It also uses port 19528; run the two acceptance scripts sequentially.

`Network/test_arwic_mines.py` uses isolated port 19525 to verify the Overseer
conversation, quest-only hammer loot, targeted hammer/sword hand-ins, learned
recipe validation, multi-stage rewards, readable book pages and three restarts.
It rejects combat/distant NPC use, an unlearned recipe and stale ingredients.
Its combat fixture lowers Outcast health; a separate native-DAT unit test checks
all six production mine actors, their native combat/AI values and corpse-aware
spawner refill. Native `MessageDialog` StringInfo is decoded into readable chat
while the original script event remains available for protocol diagnostics.

`Network/test_vigor_loot.py` uses two clients and isolated port 19526 to check
Lightning Bolt's learned-skill/drum requirements, periodic vigor drain, observer
updates and live examination. It also checks Baron Colier's native 3–6 generic
loot slots, ownership, stable reopening and rolled equipment stats preserved on
pickup. Locations, training XP and weakened boss health are disposable fixtures;
target vigor and loot limits come from the installed DAT.

`Network/test_weapon_loot.py` uses isolated ports 19532/19533 to check four
rolled weapon drops, pickup/inspection, native minimum-level refusal, equipped
Flame Bolt damage, unchanged burn ticks, remote spell FX and a full server
restart retaining equipment and stats. The corpse seed, positions, XP and
weakened boss health are test fixtures; generated damage is documented AC2E
reconstruction. Weapon identities and minimum use levels come from the DAT.

`Network/test_wilderness_population.py` uses ports 19534/19535 and two disposable
characters to verify seven regions in the full prepared world: all five Knorr
areas, Arramora and Drudge Citadel. It checks visibility, death replication and
loot pickup. Sample targets have reduced health; native-DAT tests separately
validate every expansion actor's ground, stats, loot and managed spawner refill.

`Network/test_hunting_quests.py` uses ports 19536/19537 to complete all 35 NPC
hunts from both immutable catalogs, including Ikeras, Arramora and Foundry
chains, a partial-progress restart, fixed/capped return rewards, level gates,
duplicate-turn-in protection and completed-journal replay. NPC positions are
production content; ten low-health copies per hunt cycle through its placed
qualifying variants at native ground positions. Production saves are never used.

`Build-AC2Network.ps1 -Test` builds the bridge and AC2E and runs their Rust tests. Install Rust with rustup so `cargo` is on PATH; an optional local installation in `Tools/Rust` is also supported. Python defaults to the bundled runtime if present, otherwise `python`; use `-Python` for another installation. Build Unreal with `Launch-AC2.ps1 -Mode Build`.

The bridge dependency is pinned at AC2_Rusted commit `bcf67b4b9ac3ee9eb429d68ba6766cfce47c5a25`, in `References/AC2_Rusted`; `Cargo.lock` pins registry dependencies. Initialize it with `git submodule update --init --recursive`, or clone this repository with `--recurse-submodules`. Do not silently update that checkout when building.

`Network/test_network.py` uses two native UDP clients and a unique temporary store to verify creation, visibility, movement, chat, mob death, loot, logout, re-entry and persistence across a full server restart. `Network/test_auth.py` verifies account login, wrong-password rejection, ticket replay rejection, anonymous rejection and the source archive. They use loopback ports 19270/19271 and 19280/19282/19283 respectively. Results are in `Saved/AC2NetworkTests`.

All integration scripts generate their compatible content in the temporary test
directory. Set `AC2_DAT_DIRECTORY` to use a retail installation outside this
checkout; no pre-existing `Saved` files are needed.

`Network/test_gameplay.py` uses two real UDP clients on 19320/19321 to exercise weapon equip/unequip, combat modes, rejected wrong-weapon casting, melee/magic/missile attacks, creature pursuit and shared locations, incoming damage, kills, the starter reward, portal visibility removal and recall. It also tests player death/resurrection and journal replay of equipment and quest completion. `Network/test_login_ui.py --gameplay` additionally renders Unreal with a server-equipped staff and checks native magic animation playback, animated attachment and global appearance alongside the existing login/selection/inventory tests. Both use disposable stores under `Saved/AC2NetworkTests`.

`Network/test_combat_recovery.py` uses two authenticated accounts on isolated ports
19490/19492/19493. It checks that a rejected combat-button or attack request returns
both local and remote avatars to peace, allows immediate equipment changes, and
does no damage. Synthetic placements in DAT-visible neighboring rooms verify
cross-cell melee, retaliation and death; overlapping hidden rooms stay excluded.
It also verifies bow/magic kills at 12-14m across a visible room boundary without
health resets or long-range melee hits, replicated missiles and their cleanup,
and cancellation of airborne damage when returning to peace.
Range uses shared landblock coordinates (including outdoor block offsets) plus
native visibility. This does not implement server wall collision or line of sight.
Out-of-range feedback reports distance and weapon reach. Z or Stop Combat returns
to peace before changing equipment; an out-of-range combat-button attack now
returns to peace automatically. Set `AC2_TEST_SERVER` to test a separately built
server binary.

The login UI test also drives the native combat buttons through Slate's hit-test
grid and sends Z/1 through viewport keyboard input. It checks server stance,
model mode, disabled button states, chat input isolation/restoration, world
mouse selection, and server-confirmed attacks, damage, death and loot.
`Network/test_combat_ui.py` runs these combat checks independently of account
creation on isolated ports 19486/19487; `--gameplay` adds equipped-staff combat.
The combined `--mouse --gameplay` run exercises held-button orbit/steering/walking,
pointer restoration and capture loss, then checks moving magic missiles with
live DAT trail particles and inventory interactions in the rendered client.
The two-client gameplay test also checks that a rejected attack restores peace
on both the player's model and the observing client.

The Unreal server profile sends compact native inventory profiles because the pinned bridge decoder does not yet support the optional embedded native VisualDesc in full inventory graphs. Authoritative object visuals still travel in CreateObject. The original-client server profile retains full graphs; servers sending those full inventory graphs to this bridge still need decoder support.

`Network/test_server_launcher.py` runs the public batch launcher from another
working directory, connects a native client, and stops the server gracefully.
It verifies that normal startup reuses the built server without invoking Rust,
repeated startup reports the existing process without rewriting live content,
rebuilding a running server is rejected, and unverified locks are preserved.
It also checks recovery of abandoned world/account markers from an exited process.

`test_armor.py` exercises every starter clothing slot for all six supported bodies
through two UDP clients. `test_gameplay.py` additionally checks appearance for a
late observer and Shelter introduction order/restart persistence. The rendered
login test types through focused Slate fields, verifies visible shirt removal and
replacement, and captures emitted native spell particles.

`test_shelter.py` uses two UDP clients on 19360/19361 to complete the tool/glyph/
Vault path, reject remote or incorrect targets, verify all three Human armor
updates on both peers, and replay completion and travel after restart. Rust tests
also reject a full backpack without consuming the tool or appending a journal
entry, and validate all seven armor profiles for both sexes. Unreal DAT tests
cover the armor metadata, native animation triggers, timed FX scripts and missile
movement without overshoot.

`Launch-AC2.ps1 -Online -Server 127.0.0.1 -Port 9000 -SkipBuild -NetworkSmoke` runs the Unreal render acceptance test against a **test server with an existing human character**. It sends chat, attacks a nearby authored training mob, receives loot, captures the native world/inventory and exits. Use isolated server data: this test intentionally changes its character's progress.

Compatibility details: Pyrealium accepts unencrypted character creation on queue 4, and retains the incoming ordered counter across character logout. The bridge adapts these two behaviors without altering AC2_Rusted or the upstream reference. It decodes Pyrealium's current admin7148 selection layout even when using the experimental 7230 login profile.

Native behavior/mode, damage story, death and impulse events now reach the model/controller.
The bridge preserves instance stamps and 64-bit IDs, including snapshot animation state.
Movement/acceleration/jump scales, global modifiers and motion-descriptor changes apply
to the local player. Item use preserves native unsigned object-ID tags (0x84); lifestone
use has no target and the stone as the item. Recall sends skill 0x343 through the native
active-skill request; the server decides whether and where it succeeds.

`Network/test_login_ui.py` tests the complete in-client password form against isolated
AC2E account/game processes on 19480/19482/19483. It binds and recalls through the native
protocol, then routes pointer events through Slate's rendered hit-test grid to verify
hover, names, list/3D carousel selection, options, credits, delete cancellation, creation
and entry before loading the environment. It also verifies native character deletion, successful
attack/hit/death animation playback, XP/loot and native inventory/vitals. It uses a
random temporary password passed only through the child environment and fresh stores.

## Doors and vendors

Double-click a nearby door or vendor, or select it and press E. Normal doors use
native open/closed modes and opening/closing animations. AC2E replicates their
state to current and newly arriving observers; the Unreal client releases the
door's collision while open. Door state resets when the server restarts.
Key/quest-locked doors report their restriction; unlocking scripts are unfinished.
Use requires a living character within four metres of a visible object.

Vendors are identified by installed EntityDesc StoreTemplate/StoreGroup properties.
The native DAT Shop/Sale window supports selection, Purchase, double-click buying,
and dragging unequipped backpack items onto the shop to sell. Escape or the native
close button closes it. The server rechecks ownership, price, funds, bag capacity,
range, combat state and the active offer identity before committing inventory and
gold together. Purchases use server-issued offer IDs, never client prices.

This is an authored AC2E stock profile: health potion (5 gold), training weapon
(12), Mucor mushroom (2), and a 100-unit tallow bundle (2). Supported starter gear
and recovered armor sell for 3 gold. Quest items are excluded. Retail vendor-specific
inventories, dynamic prices, consignment, and partial-stack sales remain unfinished.
The shop uses DAT frames, fonts and item icons; vendor portrait rendering is pending.
Only verified placements are spawned; this does not restore missing town population.

`Network/test_services.py` uses two native UDP clients and a disposable store on
19386/19387 to check door replication, late observers, locked doors, purchases,
gear sales, insufficient funds, stale offers, ownership and combat restrictions.
`Network/test_services_ui.py` runs separately on the same ports, drives real Slate
drag/select/Purchase/close input, and checks animated door bounds and collision
traces through closed/open/closed states. Its three native-asset placements are
test fixtures, not additions to the production world. Screenshots are written to
`Unreal/Saved/Screenshots/AC2Shop-native.png` and `AC2Door-{closed,open}.png`.

`Network/test_foliage_ui.py` uses a disposable server on 19392/19393 and visits
the reported bare locations in `77B7` and `77B5`, then native grass in `77B4`.
It checks per-block generated/resolved/uploaded foliage, depth-tested materials
and high-resolution texture availability after server-authorized map travel.
The bare samples must stay bare; no substitute scenery is generated. Screenshots
and logs are preserved under `Saved/AC2NetworkTests/foliage-*` for visual review.

## NPC conversations

Wiki NPC conversations use the existing native use completion, named speech and
MessageDialog messages. Double-click an NPC without a dedicated action to read
its next topic. Select any catalog NPC and enter `@talk` to browse information
without invoking its working quest or vendor action. Range, life/combat state,
native noncombat identity and a shared one-second dialogue limit are enforced by
the server. The catalog does not grant quests or rewards. See
[`npc-dialogue-v1.audit.json`](../AC2E/emulator/content/npc-dialogue-v1.audit.json)
for coverage and [`test_npc_dialogue.py`](test_npc_dialogue.py) for the isolated
UDP regression on ports 19540/19541.

## Sources and licenses

AC2E derives from Pyrealium commit `7c2e856840b04302d1c78f057dcf8094326472a4`. The separate bridge and both upstream projects are AGPL-3.0-only. Their original notices and licenses are preserved. AC2E's account backend serves the corresponding server source at `/source.zip`; rebuild it after source changes. The original game DATs are read-only local inputs and are excluded from the source archive. See `AC2E/AC2E-README.md` and `Network/Bridge/NOTICE.md`.
