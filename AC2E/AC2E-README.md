# AC2E — Asheron's Call 2 Emulator

AC2E is this workspace's persistent multiplayer server project, derived from Pyrealium `7c2e856840b04302d1c78f057dcf8094326472a4`. It serves the Unreal AC2 client over the native protocol. It is a Rust server, independent of the AC1 ACE code and database; it is not an ACE plugin or a claim of complete retail AC2 emulation.

## Running

The development training mode has been removed. `serve --game-data PATH`
starts normal gameplay; `serve-training` and `--training-kit` are no longer accepted.
Login grants only the racial starter equipment and basic skills, without the
extra supply pack, training-only attack grant, or automatic practice-quest enrollment.
Shelter Drudges give needed quest tools/glyphs and otherwise use normal corpse
loot, rather than automatic equipment/potion gifts. Crafting, vendors, travel,
and optional NPC quests remain available. Historical training grant records
still replay so existing characters retain their earned and already-granted items.

The Unreal client now uses the existing native `UploadGameOptions` persistence
path for damage-text, radar and hotbar display preferences, including bar height.
Options remain owned by the selected character and replay from existing `o1`
records. This client/bridge integration does not change the server journal format.

Logout no longer fails when scene loading fills the reliable packet window.
The server retains the exit and character-list response, stops gameplay for the
departing character, and resumes sending as acknowledgments free capacity.
The character list follows acknowledgment of world exit. Pending item animations,
attacks, targeting and merchant state are cleared on departure; the existing
timeout/retry policy still handles disconnected clients. No store migration is
needed for this change.

Hotbar preferences distinguish a never-saved bar from an intentionally empty
bar. The first native edit writes an `h2` checkpoint containing the complete
100-slot array, retaining untouched starter buttons; later edits use existing
`h1` records. Both replay through restart and journal compaction. Existing `h1`
journals remain readable. A fresh starter bar follows the client's tutorial
order: melee, magic, missile. This preference storage grants no skill or item
entitlements. Updated journals containing `h2` require this server version or
newer; keep a store backup when testing an older binary.

Game builds now use `target/game/release/ac2e.exe`; the account service keeps
`target/release/ac2e.exe`. This lets a game build finish while standalone accounts
are running. The build refreshes the account copy only when it is not in use;
otherwise it reports the pending account update. The normal launcher prefers
the game copy and prints its path, with a fallback for older installations.
It recognizes either managed executable when verifying an existing service.

World time advances while idle and across server restarts. The server preserves
its origin in `data/world/clock-origin.json` and sends native time synchronization
every five seconds. Back up that file with the world store. Clients use the
installed DAT calendar: a Legions day lasts 7,347 real seconds (2h 2m 27s), with
lighting and weather selected from its authored sky keys. New worlds establish
their own origin; this does not reproduce the date of a historical retail server.

For local population testing, enter `@spawns` in chat while outdoors. It lists
the nearest five live imported creatures with map coordinates and distance.
This requires local test map travel (enabled by the default loopback launcher).
It does not add spawns or bypass interaction distance checks. A verified Edge
Beetle location is approximately **42.2N, 7.0W**, cell `76B5002B`.

The regional wilderness expansion adds 3,495 creature slots in 2,071 managed
groups, using 326 native creature templates across 21 regions. The prepared world
now has 9,801 placements; 8,056 outdoor creature points span 3,623 landblocks.
Two-client network tests found, killed and looted Drudge Outcasts around
**38.6N, 22.6W** (Esper Reaches), Akeha Mimbu around **10.3N, 40.6E** (Kydi Delta),
and Undead Stony Armoredillos around **15.0S, 4.7W** (Rings of Oriad).
These are inferred placements grounded on native terrain, not recovered retail
generator coordinates. See the regional population section of the recovery guide.

Wild creatures now use native perception and defined wandering controls. Their
movement reads native motion descriptors, inherited visual scale and captured
instance scale instead of giving every creature the same 2 m/s pace. Speed and
acceleration apply to pursuit, wandering and homeward travel, with short native
collision checks and positions shared with nearby players. Corpse loot combines observed item
profiles, recovered native trophy rates and the versioned inferred treasure
policy. Supported equipment can roll persistent armor ratings and palette tints.
There is no universal potion fallback. Full retail treasure probabilities,
weapon/jewelry affixes and AI/generator scripts remain unverified. See
`emulator/content/README.md` for catalog coverage and spawner group configuration,
and [content recovery](../Docs/ADMIN_CONTENT_RECOVERY.md) for the inferred quest,
camp and loot rules and their evidence.

Attacking retains the selected target and combat stance, including after a kill.
Use Z/Stop Combat to return to peace before changing equipment. Corpse pickup,
Loot All and Transmute All temporarily enter peace and restore combat afterward,
without restarting attacks on the corpse. Camera drags no longer clear selection
on mouse-down.

Switching targets cancels a queued skill and stops attacks on the previous
target, including when the new target is out of range. Selecting the same
target refreshes its status without turning a one-shot spell into auto-attack.
Pickup/drop animations and corpse transmutation also complete any cancelled
skill request, so the hotbar does not retain its pending marker.

Animated healing and support spells accepted from peace mode now enter the
equipped weapon's combat stance before their native cast story. Nearby players
receive the same mode/story order. A rejected cast leaves stance unchanged,
and support casting does not start attacks on the selected target.

Run `start-ac2e.bat` from the workspace root, or `AC2E/Start-AC2E.ps1`. This uses the existing AC2E binary (building it if missing), prepares a compatible world placement file from the installed DATs, starts the account service on loopback HTTP 19400, and listens for game traffic on 127.0.0.1 UDP 9000 and 9001. Rust is needed for builds, not for starting an existing binary. After changing server source, run `start-ac2e.bat -Rebuild` or `Build-AC2E.ps1`. In another terminal run `Launch-AC2.ps1 -Online`. The client builds its native bridge if missing. Sign in with a new account name (1–32 letters, digits or underscores) and a password (12–128 characters). First login creates the account; subsequent logins verify the saved password. Account names are case-insensitive. The server owns character creation, progression and inventory.

Parameters: `-Port`, `-Account`, `-DatDirectory`, `-DataDirectory`, `-Content`, `-Python`, `-SkipBuild`, `-Rebuild`. `-SkipBuild` also prevents the first build when no binary exists; it cannot be combined with `-Rebuild`. Defaults resolve relative to the project directory, regardless of the current working directory. Graceful shutdown: `Start-AC2E.ps1 -Stop`. Back up `data/world` with the server stopped. The store uses append-only journals with exclusive writer locks, validated replay, atomic transactions and persisted placements; it is not a MySQL schema. The CLI also exposes store inspection and compaction commands inherited from Pyrealium.

Starting again with the same data directory reports the existing server's PID and
endpoint and exits successfully. It does not rebuild, rewrite the live content,
or apply different settings to that server. Stop the server before rebuilding or
changing its configuration. The launcher verifies the lock's process, executable,
and data directory. A stale marker is archived only after obtaining exclusive
read/write access and confirming its recorded process has exited, or that Windows
reused its PID for a process created after the marker was written. Live,
unreadable and unverifiable markers remain intact. Both world and account
services use this recovery path.

## Initial gameplay ruleset

The compatible capture pack was audited at 5,606 placements, including 4,428 combat point spawners and 78 vendor identities. The separately versioned inferred pack adds 702 accepted placements in 143 camp groups plus noncombat NPCs, with 88 quest profiles. Counts depend on the installed content and recovered evidence; the generated report next to the prepared TSV is authoritative. Version mapping, per-cell snapshot selection and duplicate/dead-object filtering withhold incompatible observations. Authored portal routes and compatible lifestone fixtures use the native binding/recall handlers. Recovered NPC or vendor identity does not imply complete scripting or retail stock.

The authored starter encounter supports targeting, range-checked melee/missile/magic attacks, learned racial starter skills, retaliation, health/vigor, death/despawn/respawn, XP and gold. Claimed loot is journaled before publication and survives logout/restart; unclaimed corpse contents expire. Separate authored tutorial rewards remain independent of recovered creature loot tables. The Legions profile validates racial starter gear, and equipped weapons publish their native modes/animations to nearby players. The human starter circuit can be completed through its reward, outdoor exit and recall; it is authored development content, not full retail tutorial parity. Complete quest/crafting scripts, original loot generation probabilities and affixes, PvP and complete anti-cheat enforcement remain unfinished.

The native server/client layer also supports persistent character appearance, multiple players, scene visibility, cell changes, server position corrections, local/tell/emote chat, owned inventory replication, item use/equipment requests, death/resurrection, and authenticated ticket login. Client compatibility details and verification commands are in `../Network/README.md`.

Unarmed starter attacks now publish native martial-arts mode and attack stories;
supported starter swords retain one-handed mode. The Idol uses Melee_Normal1,
which exists in its Legions animation map, instead of the unavailable Auto1 key.
The scene snapshot retains the selected combat mode for observers entering later.

## Authenticated hosting

Normal startup manages the account backend at `http://127.0.0.1:19400`; the client login form defaults to that address. `-AccountPort` changes its port, `-AuthDirectory` selects the account store, and `-GameHost` sets the game address returned to clients. By default accounts are stored beside the world directory in `accounts/`. Back up both stores with the services stopped. Ticket mode rejects development/no-ticket login. The client can also use `Launch-AC2.ps1 -Online -AccountUrl http://127.0.0.1:19400 -Account NAME`.

Normal startup reuses a healthy local account service when its executable, account/world directories, account port, public origin and game endpoint match. This also handles an account child left running after its launcher window closed. Reused services remain running when the game stops; a newly started account child stops with its game server. Incompatible or unverifiable lock owners are preserved and reported, rather than bypassed.

For a separately managed backend with a different local bind, run `Start-Accounts.ps1`, then start the game with `-ExternalAccounts -AuthDirectory PATH -PublicOrigin URL`, using the same account/world directories and game endpoint in both launchers. The game launcher leaves external services running. To stop an account backend, use `Start-Accounts.ps1 -Stop -AuthDirectory PATH`.

Legacy password-free stores are never claimed by auto-registration. Reserved names (`local`, `admin`, `administrator`, `system`) and names owning pre-existing characters without an account record cannot be registered. To access an old development account locally, explicitly start with `-DevelopmentLogin` and clear the account-service URL/password in the client. This option is restricted to loopback. Existing character/account files are not migrated or erased.

For external hosting, put the loopback account backend behind a TLS reverse proxy and start the managed pair with `-Bind 0.0.0.0 -GameHost game.example.com -AuthDirectory PATH -PublicOrigin https://accounts.example.com`. For a separately managed backend, also pass `-ExternalAccounts` and configure matching `PublicOrigin`, `GameHost`, `GamePort`, account and world directories in `Start-Accounts.ps1`. The proxy must overwrite `X-Forwarded-For` with the client's single IPv4 address; tickets bind to that address. Publish both UDP ports. No firewall/router settings are changed by these scripts. Remote deployment itself has not been performed.

The account backend serves `/source.zip` with the source used to build this modified server, preserving the AGPL source offer. Build after changing source; do not point the offer at unmodified upstream. `export_source.py` packages the current server source, docs, dependency lockfile, and build/launch helpers using explicit source directories. It works in this monorepo and in an unpacked source download, and excludes game DATs, account state and journals.

## Local map testing

The default loopback launcher enables `--test-map-travel true`. The native Unreal
world map sends `@maptravel LAND_X LAND_Y` on a double-click only after receiving
the server's private `[AC2E:map-travel-v1]` capability. Land coordinates cover
`[0, 2040)` on each axis. The server decodes height samples and triangle diagonals
from LandscapeDefs / CLandBlockData, rejects absent ground, cave openings and
unwalkable slopes, persists the move and replicates it through normal visibility.
The client waits for streamed collision and checks capsule clearance at arrival.
The minimap and illustrated continent-overview windows retain their existing
selection and viewing functions; travel uses the georeferenced world/detail map.

Travel requires a living character out of combat and has a five-second cooldown.
It does not grant tutorial progress. `-DisableMapTravel` disables it in the
launcher. The raw server defaults to disabled, and rejects the feature on a
non-loopback bind. Existing Pyrealium servers without this capability retain
normal gameplay travel. This is a testing extension, not a retail feature.

## Implementation notes

### Shelter continuation, armor and effects

The Shelter Guard, Lieutenant, Sentry, Scout and Advance Scout use native DAT
StringInfo dialogue. Training stones show their original text. `@quest shelter`
reports the ordered per-character progress. After the introductions:

1. Defeat a Drudge Digger and collect the Imbued Digging Tool.
2. Speak to the Shelter Scout within four metres, out of combat. The tool is
   consumed once; the Scout grants the three native tutorial potions and 40 XP.
   The inventory must have room for all three rewards after consuming the tool.
3. Defeat a Drudge Sentinel for the Menhir Glyph. Select the gateway and use the
   glyph in the backpack while nearby. Its consumption and Vault placement are
   saved together. An unrelated or distant target cannot consume it.
4. Speak to the Advance Scout in the Vault, then use the Menhir Shard. After
   activation it plays the Menhir history and takes the character through portal
   space to their outdoor starter hub. The final transfer awards 65 XP once.

Item exchanges use `s1` journal entries. The shard saves quest completion,
its XP award and the destination together in an `sx2` entry. They preserve inventory, progression and travel across restart.
Introductions retain the earlier `q5` records. Failed or repeated exchanges grant
nothing. The glyph gateway can be reused after unlocking it, allowing return
after death or recall without requiring another consumable glyph.
Travel advances the native teleport counter, rather than the force-position
correction counter, so clients enter portal space and load the destination.
Old-room movement packets are rejected until the client acknowledges that counter.
`Network/test_starters.py` completes all three quest chains, retries a portal
through its cooldown, and checks teleport stamps and persistence after relogin.
The Tumerok gateway uses the DAT pedestal center in cell `03FD0130`; its elevation
comes from the cooked platform collision. The reported `/loc` identifies this
pedestal, but its Z is the player's capsule center rather than ground height.

The [Human Shelter map](../Docs/HUMAN_SHELTER_MAP.png) shows the main dungeon
and its Menhir Vault. Historical player captures and byte-identical native room
records establish the Human officers, lesson stones, Straw Drudges, Drudge Diggers,
Drudge Sentinels and portal locations. The warning stone and skull operate the
optional early exit. The Human practice lifestones, idols and duplicate NPCs are
available only in the legacy combat test fixture. Reconstructed positions for the
other races are identified per actor in `starter-areas-v1.json`; the Tumerok
Advance Scout in 03FD0154 was observed directly. The three racial
`porting.from.shardroom` recordings identify the shard as entity `4700079A`,
its position in each vault, the 19 frames of story `40000021`, and the
departure effects. XP, quest drops and encounter timing use AC2E policy.

Seven additional armor profiles are validated against the installed entity,
qualities and physical records: Human 47001951/1952/1954, Lugian
4700195E/195F/1960, and Tumerok 47001968. They supply torso/legs/feet for Human
and Lugian and a torso for Tumerok. Native race/sex selectors choose appearance
tables; worn pieces contribute their DAT armor values to development damage
mitigation. Once the relevant Shelter quest drop is owned, additional Digger or
Sentinel kills grant missing race-compatible profiles. This deterministic loot
policy is not an original retail loot table. Unequip the existing slot first.

Starter and additional armor use saved full VisualDesc updates for the owner,
nearby players, late observers, selection and re-entry. Character creation traits
remain intact. No debug command grants arbitrary item definitions.

The client now decodes native AnimInfo FX triggers and 0x57 timed FX scripts,
including simultaneous start/stop events. FX nodes can create native temporary
models with their own behavior and target FX. Particle variants, bone/holding
attachments, initial attachment frames and static effect roots are supported.
Detached particle emitters retain their birth frame while attached emitters follow
the animated socket. Projectile models face along the full flight direction.
Native InitializeMissile and moving PhysicsDesc snapshots preserve target,
offset, velocity and instance. The renderer handles fixed-point, object-target
and untargeted flight, bounded lifetime and trail cleanup; missiles are excluded
from selectable world contacts. Server health changes remain authoritative.

The server now imports native skill costs, equipment/target requirements, range,
cooldowns, rank curves and core effects from the installed DATs. The executable
subset includes 224 racial profiles: Flame Bolt applies its damage and periodic
burn, while supported healing, vigor, regeneration and combat-rating effects
use the same cast path. Train the skill and equip the required implement first.
Nine shared mastery skills can be trained and raised for the supported races;
their ranks contribute to combat ratings. See `emulator/content/README.md` and
`skill-runtime.report.json` for the exact scope and pending profiles.

This does not finish every retail spell effect. Magic damage remains a server
impact with an authored release delay rather than a native simulated missile.
Native ballistic parameters, scripted missile creation, target cloning,
sound/light/streak FX, area/multi-hit timing, combos, summons and specialized
statuses need further work. Human/Lugian starter casts select Magic_Nuke1 and
Tumerok Flash selects Magic_Harm1; normal magic auto-attacks keep Magic_Auto1.
Unsupported required effects withhold the cast rather than consume vigor for
a silently incomplete status effect. Non-lifestone recalls remain unavailable.

`Network/test_shelter.py` verifies the complete implemented continuation with two
UDP clients, including three additional armor appearances and server restart.
`Network/test_armor.py` covers the starter wardrobe on all six supported bodies.
The Unreal DAT suite checks added armor metadata, native animation FX triggers,
timed FX scripts and missile movement; the rendered login/combat test captures
native impact particles and appearance changes. These checks do not establish
full retail quest or spell parity.

- The opt-in `PYREALIUM_UNREAL_LEGIONS_DATA=1` profile recognizes the supplied Legions state fingerprints. It does not modify DAT bytes or claim compatibility with the original 7148 client VM. Unreal decodes wire data and does not execute the merchant/group/allegiance WLIB packages whose retail layouts differ.
- Skill metadata (1,927 resolved definitions; 186 unresolved) and 30 advancement curves were regenerated with the existing strict DAT readers. Unsupported records remain unsupported. Server metadata validation remains active for supported skills, inventory and loot.
- Ordered network counters wrap as native 16-bit stamps; a reordered rollover is covered by a transport regression test. Client re-entry keeps the upstream-compatible incoming sequence.
- `emulator` retains the `pyrealium` library name for existing tests/modules; the executable is **ac2e.exe**. Original licensing/notices remain intact. `UPSTREAM.md` describes upstream features, not the completion level of this AC2E/Unreal ruleset.
- Upstream native-client launcher assets and launcher-specific startup scripts are omitted. The `deploy/` templates and upstream documentation remain reference material for porting; use the AC2E scripts above for this Windows/Legions setup.

Build the server independently with `Build-AC2E.ps1 -Test`, or build the server and Unreal transport bridge with `../Build-AC2Network.ps1 -Test`. Protocol/render integration tests are in `../Network`; they use isolated temporary stores. For a manual game-only build, regenerate the source archive with Python `export_source.py`, then run `cargo build --release --locked --manifest-path emulator/Cargo.toml --target-dir target/game`. Use the PowerShell build script to also refresh the stopped account-service executable.

Inventory drags reorder the native dense backpack list, including adjacent slots.
Release a backpack item over the world to drop the whole item or stack at your feet;
double-click it within four meters to pick it up. Unequip worn items first. Nearby
players see the same dropped model and may pick it up. Drops and pickups commit
atomically in the character journal and survive server restart. Ground items are
bounded to 512 per server; partial-stack world drops remain unfinished. Corpse
containers now support owned multi-item drops, single pickup and Loot All.

The reconstructed Vault arrival and Advance Scout now use floor positions clear
of the authored Menhir Shard. Logging in at the old obstructed arrival moves the
character to that clear landing. The client also waits for received world-object
collision before releasing a portal arrival. Native DAT collision tests cover
both supported capsule extremes and walking away from the landing/scout.
`Network/test_world_drops.py` checks two-player ownership and restart recovery;
`test_shelter.py` covers the tutorial continuation and old-arrival recovery.
