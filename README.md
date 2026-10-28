# Asheron's Call 2: Unreal client and AC2E server

AC2E combines an Unreal Engine 5.8 client for Asheron's Call 2 with a persistent
Rust server derived from Pyrealium. The client is in `Unreal/`, and the server is
in `AC2E/`. This is an experimental implementation, with offline exploration and
native multiplayer; it is not yet complete retail emulation.

The Shelter sequence now includes the digging-tool exchange, Scout potion rewards,
Menhir Glyph travel and an authored Vault completion step. Seven additional DAT
armor profiles join the racial starter clothes, with saved appearance replicated
to other players. The client reads native animation FX triggers, timed FX scripts,
temporary effect models and incoming missile flight. AC2E now sends native racial
missile and auto-magic projectiles, with damage after flight. Starter magic skills
use their direct cast/impact path. Full retail spell FX, ballistic physics and the
Menhir history/cinematic remain unfinished. See `AC2E/AC2E-README.md` for scope.

Scenery now applies native appearance overrides (including the town-sign labels),
and localized signs render readable on both faces. World materials honor the DAT
specular switch; the Menhir's independently moving second texture is supported.
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

## Missing content records

`AC2E/data/world/legions-world.report.json` now lists the exact record IDs and DAT
archives missing from each excluded server placement. These are different from
texture-resolution problems: `highres.dat` cannot supply absent entity, visual, or
room definitions. In the current local installation, 22 excluded placements refer
to 13 distinct entity records, 8 visual records, and 5 room records; several
placements share missing definitions.

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
`C:\Turbine\AC2` copy was checked and recovers none of these 22 placements.

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
Use offline exploration or connect to an AC2E/Pyrealium server. See the guides for
account setup, hosting, supported gameplay, and remaining implementation limits.

The world starts with the DAT mouse pointer visible. **Left-click** selects a
world object; **double-click** uses it; **right-click** opens the native examination
panel with information supplied by the server. Hovering a selectable object shows
the DAT usage cursor and its name. Right-clicking inventory items inspects them;
double-clicking equips/uses them. **Hold right mouse** to look and steer;
**drag left mouse** to orbit without turning the character; **hold both** to run.
Releasing restores the pointer, and a right-click without dragging still inspects.
**Middle-click** or **Alt** optionally toggles continuous mouse look,
**wheel** zooms the camera, and **Tab** switches first/third person. In mouse look,
the native center crosshair selects/uses/examines the object you aim at. The pointer
returns to its previous position when leaving mouse look. UI scrolling stays in
the hovered panel, and stationary camera orbit does not rotate the character.
Drag the character preview or inventory paperdoll to rotate it. Cursor artwork
and hotspots come from the installed DATs; map zoom and preview rotation retain
their native pointers. WASD movement and the existing combat hotkeys remain available.
Running is the default; hold either **Shift** key to walk. **Mouse4** (the first
thumb/back button) toggles autorun in either cursor mode. Press it again, press
forward/backward, or press Escape to stop. Chat, focus loss, death and travel also
cancel autorun. The radar compass and contacts share the DAT radar center and one
normalized character heading; stationary camera orbit does not turn the radar.

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
python .\Network\test_environment_content.py
python -m unittest discover -s Network -p test_prepare_content.py
python .\AC2E\test_export_source.py
```

The launcher creates Unreal material assets from C++ on first play. Build output,
generated materials, accounts, and characters are ignored by Git. The server
creates its compatible placement file from the local DATs when it starts.

`-EnvironmentSmoke` captures both faces of the native Arwic sign, the Menhir Vault
crystal, and foliage with textured distant terrain. Results and images go under
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
