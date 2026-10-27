# AC2E — Asheron's Call 2 Emulator

AC2E is this workspace's persistent multiplayer server project, derived from Pyrealium `7c2e856840b04302d1c78f057dcf8094326472a4`. It serves the Unreal AC2 client over the native protocol. It is a Rust server, independent of the AC1 ACE code and database; it is not an ACE plugin or a claim of complete retail AC2 emulation.

## Running

Run `start-ac2e.bat` from the workspace root, or `AC2E/Start-AC2E.ps1`. This uses the existing AC2E binary (building it if missing), prepares a compatible world placement file from the installed DATs, starts the account service on loopback HTTP 19400, and listens for game traffic on 127.0.0.1 UDP 9000 and 9001. Rust is needed for builds, not for starting an existing binary. After changing server source, run `start-ac2e.bat -Rebuild` or `Build-AC2E.ps1`. In another terminal run `Launch-AC2.ps1 -Online`. The client builds its native bridge if missing. Sign in with a new account name (1–32 letters, digits or underscores) and a password (12–128 characters). First login creates the account; subsequent logins verify the saved password. Account names are case-insensitive. The server owns character creation, progression and inventory.

Parameters: `-Port`, `-Account`, `-DatDirectory`, `-DataDirectory`, `-Content`, `-Python`, `-SkipBuild`, `-Rebuild`. `-SkipBuild` also prevents the first build when no binary exists; it cannot be combined with `-Rebuild`. Defaults resolve relative to the project directory, regardless of the current working directory. Graceful shutdown: `Start-AC2E.ps1 -Stop`. Back up `data/world` with the server stopped. The store uses append-only journals with exclusive writer locks, validated replay, atomic transactions and persisted placements; it is not a MySQL schema. The CLI also exposes store inspection and compaction commands inherited from Pyrealium.

Starting again with the same data directory reports the existing server's PID and
endpoint and exits successfully. It does not rebuild, rewrite the live content,
or apply different settings to that server. Stop the server before rebuilding or
changing its configuration. The launcher verifies the lock's process, executable,
and data directory. A stale marker is archived only after obtaining exclusive
read/write access and confirming its recorded process has exited. Live,
unreadable and unverifiable markers remain intact. Both world and account
services use this recovery path.

## Initial gameplay ruleset

The default server loads 15,132 DAT-compatible placements from the retained population and authored starter content. Twenty-two rows reference assets absent from this Legions installation and are excluded with a report next to the generated TSV. The authored portal routes and four compatible lifestone fixtures are enabled along with the server's native binding/recall handlers. Recovered world placements are retained. Static NPC/portal placements do not imply complete NPC scripting.

The authored starter encounter supports targeting, range-checked melee/missile/magic attacks, learned racial starter skills, retaliation, health/vigor, mob death/despawn/respawn, XP, gold and automatic potion loot. Rewards and loot commit together before publication and survive logout/restart. The Legions profile validates and grants racial starter gear independently of the obsolete merchant/crafting catalog. Starter weapons can be equipped and their native modes/animations are broadcast to nearby players. The normal launcher enables validated portal routes, lifestones, and the three-Idol combat quest. The human starter circuit can be completed through its reward, outdoor exit and recall; it is authored development content, not full retail tutorial parity. Merchant/crafting lessons remain disabled because their catalog references absent records. Native loot-container protocol requests are forwarded by the client, but retail corpse generation/looting and complete quest/merchant behavior remain separate work. PvP, full original AI, server terrain collision and complete anti-cheat enforcement are not implemented by this integration.

The native server/client layer also supports persistent character appearance, multiple players, scene visibility, cell changes, server position corrections, local/tell/emote chat, owned inventory replication, item use/equipment requests, death/resurrection, and authenticated ticket login. Client compatibility details and verification commands are in `../Network/README.md`.

Unarmed starter attacks now publish native martial-arts mode and attack stories;
supported starter swords retain one-handed mode. The Idol uses Melee_Normal1,
which exists in its Legions animation map, instead of the unavailable Auto1 key.
The scene snapshot retains the selected combat mode for observers entering later.

## Authenticated hosting

Normal startup manages the account backend at `http://127.0.0.1:19400`; the client login form defaults to that address. `-AccountPort` changes its port, `-AuthDirectory` selects the account store, and `-GameHost` sets the game address returned to clients. By default accounts are stored beside the world directory in `accounts/`. Back up both stores with the services stopped. Ticket mode rejects development/no-ticket login. The client can also use `Launch-AC2.ps1 -Online -AccountUrl http://127.0.0.1:19400 -Account NAME`.

For a separately managed backend, run `Start-Accounts.ps1`, then start the game with `-ExternalAccounts -AuthDirectory PATH -PublicOrigin URL`, using the same account/world directories and game endpoint in both launchers. The game launcher leaves external services running. Normal startup stops its own account-service child when the game stops.

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
4. Speak to the Advance Scout in the Vault, then use the shard for the authored
   completion step and 65 XP. The exit portal leads to the outdoor starter hub.

These exchanges use versioned `s1` journal entries, recomputed from a canonical
plan on replay. They preserve inventory, progression and travel across restart.
Introductions retain the earlier `q5` records. Failed or repeated exchanges grant
nothing. The glyph gateway can be reused after unlocking it, allowing return
after death or recall without requiring another consumable glyph.

Placement provenance matters: the Guard/Lieutenant mirror the observed 0298
training instance; its corresponding cells match 0299 after the identity word.
The Advance Scout at 03f40154 is an observed placement. The new Sentry, Scout,
two Drudges, gateways and shard use explicitly authored coordinates in
`training-world.tsv`, not recovered retail spawn locations. The shard currently
uses the installed Master Osteth Menhir asset and an authored completion rule;
the original Menhir history/cinematic has not been reconstructed. Scout potion
items are real DAT definitions, but their reflect, regeneration and speed buffs
are not yet executable. XP, deterministic drops and encounter timing are AC2E
development policy. The separate three-Idol Training Grounds circuit remains.

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

This does not finish every retail spell effect. AC2E's starter magic still uses
direct server damage rather than generating retail missiles with delayed hits.
Native ballistic parameters, scripted missile creation, target cloning, sound/light/streak FX
and the rest of the spell catalog need further work. Human/Lugian starter casts
now select Magic_Nuke1 and Tumerok Flash selects Magic_Harm1 from their native
skill records; normal magic auto-attacks keep Magic_Auto1.

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

Build the server independently with `Build-AC2E.ps1 -Test`, or build the server and Unreal transport bridge with `../Build-AC2Network.ps1 -Test`. Protocol/render integration tests are in `../Network`; they use isolated temporary stores. The source archive can be regenerated with Python `export_source.py`, then `cargo build --release --locked --manifest-path emulator/Cargo.toml --target-dir target`.
