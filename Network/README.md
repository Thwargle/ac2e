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

Alt shows the cursor: click a visible creature in the world to select it, then
press 1 to attack. World picking respects walls and excludes hidden cells, held
weapons and inventory objects. The native start/stop buttons enter combat/peace
even without a target; their ghosted states follow server acknowledgements. Z
also toggles the offline sandbox's unarmed stance. An attack without a selected
target shows guidance in the world log.

Server characters, positions, visuals, appearance aspects, actor visibility, inventory, health/vigor, levels, XP and gold update from server events. Remote actors interpolate server movement and use native locomotion animation. Client collision and prediction are retained for local movement. NPC test spawning, flight and arbitrary map teleports are disabled online. Inventory changes wait for acknowledgement; no network profile is written to the offline character file.

The AC2E initial ruleset tests **automatic loot grants** on supported mob kills. It does not yet supply a complete retail corpse-looting window, every combat animation, every merchant/quest/social panel, terrain-authoritative server collision or anti-cheat parity. Static recovered placements and authored encounter behavior are distinct; seeing an NPC does not mean its full original script has been implemented.

The Legions profile grants DAT-validated racial starter clothes and three weapons once per character. Equipment acknowledgments update native inventory, and held models follow animated holding locations. Clothing changes rebuild the native avatar appearance while preserving face/body traits and colours; current observers, later arrivals, selection and re-entry receive the saved result. Seven additional armor definitions support Human/Lugian torso, legs and feet and a Tumerok torso, including sex-specific appearance tables and server armor values. Unequip the current piece before wearing its replacement. Additional training Drudges drop these profiles after the relevant quest item has been collected; this is an authored loot policy.

Starter attack IDs and animation modes come from DAT tables; damage, vigor cost, range and timing remain development rules. The client runs native animation FX triggers and timed 0x57 FX scripts, plays particle variants and temporary effect models, and retains target references. The bridge forwards InitializeMissile and moving PhysicsDesc snapshots; the client handles fixed-point, object-target and untargeted flight, arrival and trail cleanup. These presentation paths never apply damage. AC2E still resolves starter magic attacks directly, without retail missile creation or flight-delayed damage. Native ballistic parameters, sound/light/streak effects and full spellbook parity remain incomplete.

The supported human dungeon circuit is: equip a weapon, defeat three Idol Spawn, use the mentor for the reward, bind the lifestone, use the training gateway to the outdoor hub, then recall. This verifies the **authored AC2E starter circuit**, not the recovered retail tutorial's full quest chain. Starter Idol pursuit is server-owned, limited to its current cell, with an 18m leash; this is not cross-cell pathfinding or line-of-sight collision. Both current observers and later arrivals receive its authoritative pose; respawn returns it to the original placement.

Separately, the Shelter Guard/Lieutenant/Sentry introduction and training-stone
text use native DAT StringInfo references, with per-character durable progress.
`@quest shelter` reports its status. The continuation covers Digger/tool/Scout,
Sentinel/glyph/portal, Advance Scout and a reconstructed Menhir completion step.
Item consumption, rewards and glyph travel use durable atomic transactions.
See the AC2E guide for reconstructed placements and remaining retail gaps.

## Build and verify

`Build-AC2Network.ps1 -Test` builds the bridge and AC2E and runs their Rust tests. Install Rust with rustup so `cargo` is on PATH; an optional local installation in `Tools/Rust` is also supported. Python defaults to the bundled runtime if present, otherwise `python`; use `-Python` for another installation. Build Unreal with `Launch-AC2.ps1 -Mode Build`.

The bridge dependency is pinned at AC2_Rusted commit `bcf67b4b9ac3ee9eb429d68ba6766cfce47c5a25`, in `References/AC2_Rusted`; `Cargo.lock` pins registry dependencies. Initialize it with `git submodule update --init --recursive`, or clone this repository with `--recurse-submodules`. Do not silently update that checkout when building.

`Network/test_network.py` uses two native UDP clients and a unique temporary store to verify creation, visibility, movement, chat, mob death, loot, logout, re-entry and persistence across a full server restart. `Network/test_auth.py` verifies account login, wrong-password rejection, ticket replay rejection, anonymous rejection and the source archive. They use loopback ports 19270/19271 and 19280/19282/19283 respectively. Results are in `Saved/AC2NetworkTests`.

All integration scripts generate their compatible content in the temporary test
directory. Set `AC2_DAT_DIRECTORY` to use a retail installation outside this
checkout; no pre-existing `Saved` files are needed.

`Network/test_gameplay.py` uses two real UDP clients on 19320/19321 to exercise weapon equip/unequip, combat modes, rejected wrong-weapon casting, melee/magic/missile attacks, creature pursuit and shared locations, incoming damage, kills, the starter reward, portal visibility removal and recall. It also tests player death/resurrection and journal replay of equipment and quest completion. `Network/test_login_ui.py --gameplay` additionally renders Unreal with a server-equipped staff and checks native magic animation playback, animated attachment and global appearance alongside the existing login/selection/inventory tests. Both use disposable stores under `Saved/AC2NetworkTests`.

The login UI test also drives the native combat buttons through Slate's hit-test
grid and sends Z/1 through viewport keyboard input. It checks server stance,
model mode, disabled button states, chat input isolation/restoration, world
mouse selection, and server-confirmed attacks, damage, death and loot.
`Network/test_combat_ui.py` runs these combat checks independently of account
creation on isolated ports 19486/19487; `--gameplay` adds equipped-staff combat.
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

## Sources and licenses

AC2E derives from Pyrealium commit `7c2e856840b04302d1c78f057dcf8094326472a4`. The separate bridge and both upstream projects are AGPL-3.0-only. Their original notices and licenses are preserved. AC2E's account backend serves the corresponding server source at `/source.zip`; rebuild it after source changes. The original game DATs are read-only local inputs and are excluded from the source archive. See `AC2E/AC2E-README.md` and `Network/Bridge/NOTICE.md`.
