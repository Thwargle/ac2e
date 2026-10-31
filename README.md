# Asheron's Call 2: Unreal client and AC2E server

AC2E combines an Unreal Engine 5.8 client for Asheron's Call 2 with a persistent
Rust server derived from Pyrealium. The client is in `Unreal/`, and the server is
in `AC2E/`. This is an experimental implementation, with offline exploration and
native multiplayer; it is not yet complete retail emulation.

The client now uses the pinned AC2_Rusted audio engine for DAT SoundInfo/DBWave
effects and DirectMusic/DLS music. Native UI states trigger their authored sounds;
the creator and character selector choose the music specified by their layouts.
Animation FX sounds are positional, including terrain variants. Region sound
types from nearby land points and visible rooms drive ambient music and intermittent
sounds. Client options include sound, music, player-music and ambient volumes.
`Launch-AC2.ps1` builds the audio DLL along with the client. See
[`Audio/README.md`](Audio/README.md) for setup and verification.

The retail gameplay comparison also exposed missing selection background artwork,
omitted portal/torch viewports and frozen animated preview materials. Selection now
preserves the DAT star backdrop; the extra viewports use native scenes and transparent
compositing. Sky HSB interpolation retains native packed RGB until the lighting stage.
The video includes different hours and room lights, so it is not a global brightness
preset. Programmable materials, exact fog/shadow parity and all UI media timing are
still incomplete; these changes do not claim a frame-exact retail match.

The Shelter sequence now includes the digging-tool exchange, Scout potion rewards,
Menhir Glyph travel and an authored Vault completion step. Seven additional DAT
armor profiles join the racial starter clothes, with saved appearance replicated
to other players. The client reads native animation FX triggers, timed FX scripts,
temporary effect models and incoming missile flight. AC2E now sends native racial
missile and auto-magic projectiles, with damage after flight. Starter magic skills
use their direct cast/impact path. Full retail spell FX, ballistic physics and the
Menhir history/cinematic remain unfinished. See `AC2E/AC2E-README.md` for scope.

Scenery now applies native appearance overrides (including the town-sign labels),
and localized signs render readable on both faces. World materials use the DAT
highlight color, animated power and enable flags; zero power disables highlights
without adding Unreal's generic white specular sheen. The Menhir's independently
moving second texture is supported.
Material culling honors native template defaults as well as instance overrides.
The small scenery bird's single-sheet wings have a targeted two-sided rendering
repair using the original feather texture; the Legions material itself is authored
one-sided. Above/below render checks cover several poses of its native animation.
Distant terrain uses a filtered atlas of native terrain surfaces instead of flat
palette colors. Closed dungeons stream their own block without waiting for unrelated
neighboring dungeons. High-resolution DAT surfaces remain preferred where available.
Terrain cells explicitly marked without frills remain bare; this does not synthesize
grass or replace missing retail assets.

For the installed Legions DATs, the reported positions **43.6N, 6.6W** (`77B7`)
and **42.5N, 6.5W** (`77B5`) select the empty scenery record `02000090`.
Those blocks have no procedural grass group; their bare ground is preserved.
Nearby **41.5N, 6.4W** (`77B4`) has authored grass. The connected foliage test
checks each destination's own uploaded batches, rather than counting grass in
neighboring streamed blocks: `python Network/test_foliage_ui.py`.

Slope-aligned scenery uses the original client's plane orientation. Zero-offset,
land-relative grass also gets a render-only ground-contact correction: wide cards
split at terrain boundaries and follow the ground under their bases instead of
bridging over creases. Texture coordinates interpolate across the new edges.
This fitting is an Unreal rendering correction, not a change to native
instance placement. Scene choices, density, slope exclusions and empty areas stay
DAT-authored; water-relative and deliberately elevated frills are not fitted.
The foliage regression includes **41.6N, 6.5W**, checks card-base contact and verifies
that grass texture `40000008` uploads surface `41000034` from `highres.dat` at
**1024 x 256**, rather than the **512 x 128** portal version. The authored grass
group excludes slopes over roughly 15 degrees, so some patch boundaries remain.

The world now uses the same neutral display-gamma path as character portraits,
without a second ACES/bloom/exposure pass over native material colors. Desktop
rendering stays at 100% resolution with spatial AA and anisotropic filtering.
Cutout textures have separate alpha-weighted, coverage-preserving mipmaps to keep
thin grass/leaf silhouettes from disappearing; translucent effects and UI textures
retain their original filtering. Grass distance fading uses a stable integer hash
instead of a floating-point sine hash at large world coordinates.

The material renderer selects authored high-detail fixed-function alternatives,
evaluates up to four color/alpha texture stages in native order, retains four UV
sets and independent animated UV transforms, and honors per-axis texture wrapping.
Terrain tint filtering stays in native byte RGB until display conversion, avoiding
the extra color-space conversion that changed terrain brightness and saturation.
World objects use native ambient/directional lighting before texture combination;
interior meshes bake the DAT `CLightInfo` per-cell vertex lists and attenuation.
Nearby actors/props use up to eight static lights from their own room. High-resolution
surfaces are preferred for every texture stage. Empty optional texture stages follow
[Direct3D's cascade termination rule](https://learn.microsoft.com/en-us/windows/win32/direct3d9/texture-blending).

This is not full renderer parity: authored programmable pixel shaders, generated
reflection/projected coordinates, framebuffer distortion/refraction, additional
simultaneous material passes, and native shadow/fog parity remain unfinished.
Unsupported programmable materials use an authored fixed-function alternative when
one exists; otherwise the log identifies the base-texture fallback. Terrain/water
and standalone particle templates still use their existing rendering paths.
See the [retail comparison and material audit](Docs/RETAIL_VIDEO_COMPARISON.md)
for reference evidence, validation and remaining rendering differences.

## Gameplay and recovered world content

The prepared world combines compatible captured placements with a separately
versioned reconstruction pack. It is playable content, not a complete recovered
retail server database. Source DATs, captures and reference SQL remain unchanged.

- **Population and AI:** native combat/AI properties identify supported enemies
  while service NPCs remain noncombat. Spawners manage creation, corpse retirement
  and regeneration; explicit camp rosters enforce group capacity. Creatures use
  recovered perception, wandering and homesickness properties with native terrain
  and collision checks. Movement uses native per-visual speed/acceleration and
  captured body scale, with time-based pursuit, wandering and homeward travel.
  Unrecovered AI timing and navigation rules are documented.
  The regional wilderness pack adds **3,495 creature slots in 2,071 spawner
  groups across 21 regions** on Osteth, Omishan and Linvak. Archived wiki
  regional rosters select native creatures; Pyreal ringway/town coordinates
  bound inferred placement areas. This reaches 1,842 previously empty outdoor
  landblocks while keeping clear of towns and existing content. Group sizes,
  exact locations and three-minute regeneration are reconstruction policy.
- **Quests and services:** readable signs send native descriptions. Supported
  doors, containers, merchants, lifestones, portals and tutorial actions have
  server handlers. The inferred pack adds 88 quest profiles, 143 camp groups and
  702 accepted placements, including 26 noncombat NPCs. These include counted
  hunts, boss hunts, three hunt elixirs and Gambling Debt; placing an NPC alone
  does not implement its dialogue, vendor stock or other quests.
- **Loot and equipment:** native trophy probabilities supplement observed corpse
  loot. The inferred treasure policy adds variable supported equipment/restorative
  drops and persistent armor ratings/tints. Pickup, equipment and rewards are
  journaled. Recovered apparel, including gloves and headwear, supports equipping,
  removing and replacing items while preserving native models and race restrictions;
  unknown potion effects and full retail affixes remain unsupported.
- **Combat and movement:** repeated basic attacks retain their target, missile
  damage follows projectile flight, and local/remote swimming use native motion
  modes. Ground contact resets fall tracking. Native damage appearances, temporary
  ground blood decals and corpse names are connected; damage formulas, generic
  effect triggers and full retail physics still have documented gaps.
- **Travel and environment:** portal space covers destination streaming before
  revealing the world. Server time drives the sky cycle, and dungeon furnishings
  use their native cell-space positions. A bounded shoreline repair in block
  `74A9` uses the adjacent lake's 96.7 m height consistently for rendering,
  swimming and server geometry. It is an explicit content repair, not a DAT edit.
  Water uses continuous native textures and world-aligned UVs across the full
  14-block (about 2.2 km) landscape radius, replacing the nearby flat-blue
  distance cutoff. Surfaces are batched by material, with no overlapping near
  water layer during streaming.
  Terrain atlas edges are clamped to prevent opposite-edge texture bleed, and
  adjoining landblocks share the retail triangle-normal calculation for
  continuous lighting and slope-based materials, including block `86B0`.

Recovery tools audit Pyreal SQL, historical emulator tables, packet captures,
quest text, loot and native generator properties. Compatibility and provenance
reports separate direct evidence from inferred placements and rules. Missing
generator rosters, exact loot weights, complete dungeon populations and arbitrary
NPC scripts are still unresolved. See [content recovery](Docs/ADMIN_CONTENT_RECOVERY.md),
[capture imports](Docs/CAPTURE_REFERENCE_IMPORT.md) and the
[content manifest guide](AC2E/emulator/content/README.md).

## Missing content records

`AC2E/data/world/legions-world.report.json` now lists the exact record IDs and DAT
archives missing from each excluded server placement. These are different from
texture-resolution problems: `highres.dat` cannot supply absent entity, visual, or
room definitions. Counts grow as additional capture and regional content is
audited; use the generated report for the current totals and exact records.
Several placements can share a missing definition. The original starter-area
audit found 22 excluded placements (13 entity, 8 visual and 5 room records),
but those numbers do not describe the expanded world manifest.

If you have another complete patched Legions installation or backup, keep it in a
separate folder and compare it without replacing any active DATs:

```powershell
python .\Network\prepare_content.py "Asheron's Call 2 - Legions" .\Saved\candidate-content.tsv --compare-dat "D:\AC2-backup"
```

The companion `.report.json` lists both recovered placements and currently working
placements that the candidate would lose. This is a record-presence check, not proof
of dependency/protocol compatibility. Keep each installation's DAT set together;
a higher-resolution texture archive alone does not repair missing world records.
Without a matching source record, leave the placement excluded or create a clearly
identified replacement after its identity/location is established. The local
`C:\Turbine\AC2` copy was checked and recovered none of the original 22
starter-area exclusions.

## First checkout

```powershell
git clone --recurse-submodules https://github.com/Thwargle/ac2e.git
cd ac2e
```

For an existing checkout, run `git submodule update --init --recursive`.
`References/AC2_Rusted` is pinned to the tested protocol revision; do not update
it to the latest upstream commit as part of a normal build.

Install Unreal Engine 5.8, Visual Studio's C++/Windows SDK and Unreal workloads
(see `.vsconfig`), Rust through rustup with `cargo` on PATH, and Python 3.10 or
newer. The verified Rust toolchain is 1.99.1. Windows is the supported client
build platform. No Unreal Engine binaries are included.

Supply your own retail Legions installation in `Asheron's Call 2 - Legions/`,
or pass `-DatDirectory` to the client/server scripts. Keep `portal.dat`,
`cell_1.dat`, `local_English.dat`, and the installation's high-resolution DATs
together. Game assets and decompiles are not included or required for compilation.

## Run

- Client: `Launch-AC2.bat`
- Unreal Editor: `Launch-AC2.bat -Mode Editor`
- Local server: `start-ac2e.bat`
- Generate Visual Studio files: `GenerateProjectFiles.bat`

The client requires Unreal Engine 5.8 and the retail AC2 DAT installation in
`Asheron's Call 2 - Legions/`. A different DAT directory can be supplied with
`Launch-AC2.bat -DatDirectory "D:\Games\AC2"`.

Run `Launch-AC2.bat` again to open additional game windows for multiplayer testing.
While a client or this project's editor is open, Play reuses the current build
without closing existing windows. Each game window writes a separate
`Unreal/Saved/Logs/AC2Play-*.log`. Sign in with a different account in each window
to test multiple players on the same server. Close this project's game/editor
windows before rebuilding to apply code changes; `-SkipBuild` also skips the
initial build when you already have current binaries.

The client opens its login screen before character selection and world loading.
Start `start-ac2e.bat`, then sign in with a new account name and a password of
12–128 characters. First sign-in creates the account; later sign-ins require the
same password. Normal server startup manages both the account service (loopback
HTTP 19400) and the game server (UDP 9000/9001).
Choose a new name; `local` and `admin` are reserved legacy/service names. After a
crash, the launcher can recover a stale lock when its recorded process is gone;
it preserves the old marker and never deletes characters or account records.
Game builds use `AC2E/target/game/release/ac2e.exe`, separately from the account
service executable. Stop the game server before rebuilding it; an existing account
service can stay running. Its executable is refreshed on a later build when stopped.
Use offline exploration or connect to an AC2E/Pyrealium server. See the guides for
account setup, hosting, supported gameplay, and remaining implementation limits.

The world starts with the DAT mouse pointer visible. **Left-click** selects a
world object; **double-click** uses it; **right-click** opens the native examination
panel with information supplied by the server. Hovering a selectable object shows
the DAT usage cursor and its name. Right-clicking inventory items inspects them;
double-clicking equips/uses them. **Hold right mouse** to look and steer;
**drag left mouse** to orbit without turning the character; **hold both** to run.
Releasing restores the pointer, and a right-click without dragging still inspects.
**Alt** and **middle-click** do not toggle persistent mouse capture. **Wheel** zooms
the camera, and **Tab** switches first/third person. The pointer returns to its
previous position when a held camera gesture ends. UI scrolling stays in
the hovered panel, and stationary camera orbit does not rotate the character.
Drag the character preview or inventory paperdoll to rotate it. Cursor artwork
and hotspots come from the installed DATs; map zoom and preview rotation retain
their native pointers. WASD movement and the existing combat hotkeys remain available.
Running is the default; hold either **Shift** key to walk. **Mouse4** (the first
thumb/back button) toggles autorun. Press it again, press
forward/backward, or press Escape to stop. Chat, focus loss, death and travel also
cancel autorun. The radar compass and contacts share the DAT radar center and one
normalized character heading; stationary camera orbit does not turn the radar.

Drag panel headers to move the main windows; positions persist. Skills and quests
open from the toolbar. The skill book supports training, raising and untraining;
learned active skills can be dragged onto any of ten shortcut pages, selected with
the page arrows. Bindings persist per character. The server executes recovered
core effects for 224 racial skill profiles, including Flame Bolt damage and burns,
healing, vigor restoration, regeneration and supported combat-rating buffs.
Train Flame Bolt and equip a stave before casting it on an enemy. Nine shared
mastery skills can also be trained and raised, and their ranks affect combat.
This is not full spell parity: specialized statuses, summons, area/multi-hit
behavior and most recalls remain unfinished. Exact coverage and source evidence
are recorded in [the skill content notes](AC2E/emulator/content/README.md#native-skill-effects).

Right-click your character to examine yourself. Other inspections include known
level, health, vigor, descriptions and equipment stats; retail chance-to-hit is
not yet calculated. Open examination panels refresh server state twice a second
without clearing their text or scroll position while an update is in flight.
Chat and inventory support wheel, arrows and thumb dragging.
Floating damage numbers can be toggled in options. Native nameplate templates are
used, but complete retail relationship/color policy remains unfinished. Names
and targeting frames use stable model bounds so flying/idle animations do not
make them bounce; actual movement, scaling and corpse posture still update them.

Double-click corpse items or use **Loot All** to collect them. Looting from combat
temporarily enters peace, performs the operation and restores combat stance without
resuming attacks on the corpse. **Transmute All** converts eligible corpse items
to gold using the current appraisal policy, leaving unsupported/quest items alone.

Open the native world map with **M** and double-click a point to travel there for
testing. Single-click zoom, dragging, map notes and radar targeting still work.
This works offline and with the default local `start-ac2e.bat` server. Local map
travel requires a living character out of combat and has a five-second cooldown.
Use `-DisableMapTravel` to turn it off; public binds never enable it. The old
F1 explorer help, F2 world/weather and F3 effect-browser overlays are removed,
as is the R test-spawn shortcut. Developer console commands remain available.

## Build and verify

```powershell
.\Launch-AC2.ps1 -Mode Build
.\Launch-AC2.ps1 -Mode Test -SkipBuild
.\Launch-AC2.ps1 -PortraitSmoke -SkipBuild
.\Launch-AC2.ps1 -EnvironmentSmoke -SkipBuild
.\Build-AC2Network.ps1 -Test
.\AC2E\Build-AC2E.ps1 -Test
python .\Network\test_environment_content.py
python -m unittest discover -s Network -p test_prepare_content.py
python .\AC2E\test_export_source.py
```

The launcher creates Unreal material assets from C++ on first play. Build output,
generated materials, accounts, and characters are ignored by Git. The server
creates its compatible placement file from the local DATs when it starts.

`-EnvironmentSmoke` captures sign faces, the Menhir Vault crystal, foliage,
character poses and cave furnishings. Results and images go under
`Unreal/Saved/Diagnostics` and `Unreal/Saved/Screenshots/AC2Environment-*`.
`test_environment_content.py` checks native portal-name decoding and overlapping
lifestone snapshots using an isolated UDP server and temporary account store.

`-PortraitSmoke` renders all ten character-creation avatars at their height/build
extremes in Body and Head views, checks rotation, framing and foot placement, and
saves screenshots under `Unreal/Saved/Screenshots/AC2Portrait-*`. It uses an isolated
offline test session and does not create server accounts or characters.

Network integration tests prepare their own content and isolated stores:

```powershell
# Optional when the DATs are outside this checkout:
$env:AC2_DAT_DIRECTORY = 'D:\Games\AC2'
python .\Network\test_network.py
python .\Network\test_auth.py
python .\Network\test_gameplay.py
python .\Network\test_armor.py
python .\Network\test_shelter.py
python .\Network\test_login_ui.py --gameplay
python .\Network\test_server_launcher.py
python .\Network\test_combat_ui.py --mouse
python .\Network\test_corpse_loot.py
python .\Network\test_outdoor_population.py
python .\Network\test_recovered_equipment.py
python .\Network\test_inferred_content.py
python .\Network\test_regional_population.py
python .\Network\test_world_clock.py
python .\Network\test_falling.py
python .\Network\test_map_travel.py
python .\Network\test_map_travel.py --disabled
python .\Network\test_map_travel.py --render
.\Launch-AC2.ps1 -MapSmoke -SkipBuild
```

Pass `-Python 'path\to\python.exe'` to the build/server scripts if Python is not
on PATH. Pass `-EngineRoot` to the client launcher for another UE 5.8 location.

## Project files

- [Client guide](Unreal/README.md) and [Unreal project](Unreal/AC2Unreal.uproject).
- [AC2E setup and hosting](AC2E/AC2E-README.md).
- [Networking guide](Network/README.md) and the native transport bridge in `Network/Bridge/`.
- [DAT and decompile investigation](Docs/AC2_CLIENT_FOUNDATION.md).
- [Native gameplay changes and validation](Docs/NATIVE_GAMEPLAY_PASS.md).
- [Content recovery and reconstruction policy](Docs/ADMIN_CONTENT_RECOVERY.md).
- [Retail visual/material comparison](Docs/RETAIL_VIDEO_COMPARISON.md).
- [Native audio bridge](Audio/README.md) and [retail reference probe](Tools/RetailProbe/README.md).
- `Tools/AC2/`: DAT inspection and reference preparation tools.
- `References/AC2_Rusted/`: pinned upstream transport dependency (Git submodule).
- `Unreal/Saved/Characters/`: local offline characters; AC2E owns its separate server stores.

Retail DATs, original client files, decompile archives, and generated runtime assets
remain local inputs. Keep the licenses and notices included with the reference and
server projects when using or distributing their code.

The server and native bridge retain their AGPL-3.0-only licenses and upstream
notices. See `AC2E/LICENSING.md` and `Network/Bridge/NOTICE.md` for component scope.
The repository includes no license grant for proprietary retail assets or Unreal
Engine. AC1 content, the old native-client launcher, machine-specific SDKs,
credentials, and prior build/capture output are not part of this source project.
