# AC2 client investigation and first implementation

Investigation date: October 26, 2026. This document describes the initial native
Unreal client milestone, its evidence, and the remaining implementation work.

The 0.9.0 networking milestone adds the separate `AC2E` server project and a native
UDP bridge for AC2E/Pyrealium. See `Network/README.md` and `AC2E/AC2E-README.md` for
the tested multiplayer/combat/loot loop and the remaining emulation limits. Earlier
offline-only statements below describe those earlier milestones.

## What was examined

`C:/dev/ACE/Unreal` provides the working AC1 project structure, procedural terrain
approach, build conventions and DAT-reader comparison. The AC1 copy originally
present at the workspace root was removed when this directory became AC2-only.
The AC2 client lives in `Unreal/` and uses its own DAT reader and plugins.
No files in the separate ACE project were changed.

The three supplied ZIPs were extracted locally under `Saved/AC2Research`:

- `admin7148`: symbol-rich admin decompile, useful for types and function identity.
- `legions7160`: retail Legions decompile, matched to the supplied installation.
- `client0930`: later relaunch client. Its accompanying README identifies the
  executable as compiled in September 2013 and distributed in the 2017 archive.
  Its version numbering restarted; 0930 is newer than 7160. Its source matching
  metadata is evidence with varying confidence, not original source.

`Asheron's Call 2 - Legions/version.ini` identifies the provided client as
`2.0.20.7160`. The sizes and tested archive metadata match the separately found
`C:/Turbine/AC2` Legions installation. Validation runs target the user-specified
workspace folder. There is no verified relaunch DAT set in this result, so this
milestone does not claim compatibility with every newer asset schema.

The Rust reference is [CrimsonMage/AC2_Rusted](https://github.com/CrimsonMage/AC2_Rusted),
pinned locally at `bcf67b4b9ac3ee9eb429d68ba6766cfce47c5a25` under
`References/AC2_Rusted`. Its source has dedicated archive, asset, terrain,
animation, physics, networking, scripting, UI, audio and client crates. It is a
substantial behavioral reference, not just a window skeleton. This investigation
inspected relevant source but did not build or certify the Rust client. It is
AGPL-3.0-only with its original LICENSE and NOTICE retained in the clone; it is
not linked into the Unreal target. The Unreal implementation was written against
the supplied decompile and measured archive layouts, cross-checked with that source.

Reference documents and their embedded working rules were treated as reference
material, not as requests to change this project's scope or run their workflows.

## Storage findings that affect the port

The actual files have the `0x5442` container magic at byte 300. The inherited AC1
reader starts at byte 320. AC2 stores 16-byte directory entries (ID, offset,
length, date), where the AC1 reader expects 24 bytes. Both use 62 child slots
and up to 61 entries; the AC2 directory payload is 1,228 bytes. Copying the
AC1 parser unchanged misreads both the header and directory.

The archive stores sector chains, with a four-byte next pointer at the front
of each sector. `CLBlockAllocator::Load_Data` rejects free-sector high bits
and requires the chain to terminate exactly at the declared payload size.
The new reader also rejects cycles, invalid alignment, out-of-range offsets,
unsorted entries, oversized allocations, short reads and wrong dataset roles.
Records are read on demand; only the inspection command enumerates whole trees.
The world viewer now prepares block assets on a worker with dedicated handles,
uploads Unreal resources incrementally, and releases distant blocks during travel.

The supplied portal contains `LandscapeDefs` at DID `0x36000000`: a 20 metre
cell, eight cells per block, two terrain subdivisions per cell, and a 256-entry
height table. This yields 160 metre blocks with 17×17 height samples spaced
10 metres apart, rather than reusing the AC1 landscape lattice.

`CLandBlockData::StreamPack` stores its DID, landblock-info DID, path-map DID,
a counted byte-height array and a counted 32-bit cell-info array. The second
array's count is unaligned at byte 305; only the end is aligned. Tested records
are 1,468 bytes. Heights use the portal lookup table; they are not direct metres.
Cell-info terrain type `0x1E` marks holes. The mesh builder omits quads touching
those vertices and uses the deterministic global-coordinate diagonal from
`CLandBlockData::CachePackValues`. Normals currently come from the generated
triangles; retail neighbor-aware normal/lighting parity is not implemented.

Current decoding deliberately requires the observed lattice and record sizes.
Unknown variants produce a diagnostic instead of guessed terrain. The container
header exposes engine/game pack versions for future versioned decoders.

## Decompile evidence map

All paths below are relative to `Saved/AC2Research/<build>/source_tree`.
Run `Tools/AC2/Prepare-References.ps1` to recreate the extracted trees from the
original ZIPs. Decompile bodies are not compiled or copied into the source tree.

- `portal/shared/src/diskcon/diskdev.cpp.c`: `DiskFileInfo_t::operator=` and
  `DiskHeaderBlock_t` establish header field order and physical header layout.
- `portal/shared/src/diskcon/btree.cpp.c`: `BTree::LoadTree` uses `0x4cc` node
  payload bytes; `BTEntry`, `HasEntry`, and `ExtractEntryShift` establish the
  16-byte entries. The newest client's load function is at `0x006B0B30`.
- `portal/shared/src/diskcon/blkalloc.cpp.c`: `CLBlockAllocator::Load_Data`,
  admin `0x0064E930`, newest `0x006B28B0`, establishes chain traversal and failure
  conditions. The newest function is marked as a strong reference match.
- `portal/shared/src/geometry/landscapedefs.cpp.c`: `LandscapeDefs::UnPack`,
  admin `0x00629D60`, establishes the lattice and height table.
- `portal/shared/src/cell/landblockdata.cpp.c`: `CLandBlockData::StreamPack`,
  admin `0x0048F310`, Legions `0x00488130`, newest `0x004916A0`; these
  serialize the same fields.
  `CachePackValues`, admin `0x0048F980`, establishes polygon diagonals. The
  corresponding newest body remains named `FUN_00491d50`; its identity is
  inferred from placement and body, not claimed as a recovered symbol.
- `portal/client/src/clinet/clientnet.cpp.c`: the newest client's logon path
  constructs a `CLogonHeader` with version string `1802` (near line 3155).
  The newest executable's defined strings independently contain `NetVersion`
  and `1802`. Do not send `0930` as a guessed network version.
- `portal/shared/src/mesh/basemesh.cpp.c`, `datamesh.cpp.c`, and
  `portal/client/src/render/material/`: next sources for AC2 render-asset
  decoding. These types are not the inherited AC1 Setup/GfxObj format.
- `portal/shared/src/weenie/interp/wlib.cpp.c`, `wstate.cpp.c`, and the
  `interp/glue` trees: scripting is a significant part of AC2 UI/game behavior;
  reusing AC1 menus and packet handlers would leave that behavior absent.

## Implemented and measured

The independent project is `Unreal/AC2Unreal.uproject`, module `AC2Client`, with
separate editor/game targets and no dependencies on the copied AC1 plugins.
It builds against the locally installed Unreal 5.8 toolchain. The viewer streams
outdoor blocks around a walking/flying character, with collision, native sky and
a diagnostic HUD. It never opens a gameplay network connection.

Inspection of the supplied folder found:

- `portal.dat`: 144,890 entries; 1,024-byte sectors; pack versions 104/3.
- `cell_1.dat`: 127,400 entries; 256-byte sectors; pack versions 17/0.
- `cell_2.dat`: one entry; 256-byte sectors; pack versions 17/0.
- `local_English.dat`: 10,800 entries; 1,024-byte sectors; pack versions 104/3.
- `highres.dat`: 4,354 entries; 1,024-byte sectors; pack versions 104/3.

Every one of the 65,025 terrain records passed the new native decoder. The
sample `0x9882FFFF` has 289 vertices, 512 triangles, and heights from 2 to
405 metres. These counts are measured, not estimates. Non-terrain payloads
were indexed but have not all been decoded or validated.

Nine automation suites now cover archive/terrain, asset-reader bounds, surface
pixels/alpha, procedural scenery hashes, world coordinates/residency, sky record
bounds and native day-cycle interpolation. Both Win64 Development targets compile.
Reports and captures are under `Unreal/Saved`; the launcher guide gives commands.

The initial terrain capture was `Unreal/Saved/Screenshots/AC2Terrain.png`.
Visual verification caught and corrected the Unreal face-winding convention;
the terrain suite now explicitly checks clockwise front faces and upward normals.

## Texture and scenery milestone (2026-10-26)

`Launch-AC2.bat` was reproduced failing under Windows PowerShell 5.1 because
`$PSScriptRoot` was used in a parameter default. Resolving it in the script body
fixes the exact batch invocation. Failed double-clicks retain the error. Smoke
mode now requires a fresh completion marker after capture: Unreal's graceful
exit can return zero even when `RequestExitWithStatus(false, 1)` was requested.
The negative `-Block FFFF -Radius 0 -Smoke` case correctly returns launcher exit 1.

The C++ renderer now decodes TerrainDesc, SurfaceDesc, RenderMaterial,
MaterialInstance/Modifier, RenderTexture and RenderSurface. Highres surfaces
override portal surfaces; localized texture IDs use local_English. Supported
pixel layouts are BGR24, BGRA32, A8, JPEG and DXT1–5, including DXT2/4 alpha
unpremultiplication. Runtime texture mips are generated in linear colour space.
No retail bytes are written to Content: only two original Unreal material
templates are saved there.

Ground vertices select slope bands and weighted material IDs using retail
integer hashes. The preview bakes a 1024×1024 atlas per block with authored tile
widths and bilinear corner blending. The first vertex-colour tint is used;
authored colour variation, alpha masks, detail/bump layers and roads still need
their own rendering pass.

Placed scenery decodes MasterProperty-typed entity records, visual descriptions
(including safely skipping inline FX), setups/rest poses, FVF vertex streams and
highest-detail mesh fragments. Material texture overrides, localized signs and
cutout alpha resolve through the same texture pipeline. Nonvisual or unsupported
entities are counted. FX, appearance-table changes, attached part groups and
animated/skinned rendering remain incomplete.

The dry-land scene generator reads terrain scene indices, CSceneDesc and nested
SceneFile/SceneGroup records. It uses authored frequency, position, rotation,
scale, slope and height limits with deterministic hashes. Terrain triangle planes
provide placement height. Clearance uses conservative rendered-mesh bounds,
road-marked quads and eight slope-radius samples. These are preview approximations
to the retail clearance volumes and all-triangle tests. Water-dependent/special
scale groups are counted and omitted; grass frills are decoded but not drawn.

Measured default fixture: block **5667**, radius **2**, 25 blocks, **56 highres
textures**, **113 placed entities**, **404 generated objects**, **1,037 mesh
sections**, **112,377 scenery triangles**, **3 skipped placements**, **0 missing
materials**, and **0 deferred water/special-scale groups** in this patch. The
three skipped entries resolve to data ID `0x0000017A`, outside the visual/setup
types supported here. Capture: `Unreal/Saved/Screenshots/AC2-5667-scenery.png`.

Additional source evidence checked against the Rust behavior reference:

- Legions `CSurfaceDesc::StreamPack` at `0x0050C820` and
  `MaterialGroup::StreamPack` at `0x00655070`: table order, slope bands, lists.
- `RenderSurface::StreamPack` at `0x0057D6D0`: pixel metadata and payload framing.
  Some transferred member names are misleading; record bounds and actual DAT
  bytes establish the layout.
- `EntityDesc::StreamPack` at `0x005D47C0`: gated frame, scale and property fields.
- `SceneGroup::StreamPack` at `0x00653790`, `GetScale` at `0x00652210`, and
  `CheckFreq` at `0x00652190`: placement record order, scale rules and hash.
  CheckFreq's transferred match has lower confidence than StreamPack/GetScale;
  the integer expression is also checked against fixed regression values.
- Rust reference modules: `ac2-assets/{ac2_texture,ac2_material,ac2_entity,
  ac2_visual_desc,ac2_mesh}`, `ac2-anim/setup`, `ac2-db/property`, and
  `ac2-world/{surface_desc,ac2_scene}` at the pinned revision recorded above.

## Streaming and native sky milestone (2026-10-26)

The world now uses a 255×255 outdoor block lattice in continuous Unreal coordinates
(metres converted to centimetres). `AC2WorldStream` requests a camera-centered
radius, prioritizes the nearest tiles and retains a one-block hysteresis ring.
Each worker owns its asset library and archive handles. CPU terrain atlases,
mesh decoding, scenery generation and mip construction run off the game thread;
Unreal objects upload on the game thread in bounded mesh batches. Terrain gets
synchronous collision before entry, with scenery collision cooked asynchronously.
Old jobs are discarded after travel, and shutdown joins the worker before releasing
its data. Uploaded tiles discard CPU image/geometry caches. GPU textures are shared
through weak references; periodic collection reclaims retired Unreal objects.

`AC2Explorer` replaces the spectator with walking, jumping, sprinting and flight.
Movement cannot enter unready terrain or leave the outdoor lattice. Teleportation
selects a real terrain triangle rather than assuming the block center is solid.
Opaque scenery collides; cutout leaf cards do not. Collision uses Unreal triangle
meshes and character movement, not native AC2 physics volumes. Interior transitions
and water/swimming remain unsupported; flight can traverse outdoor holes.

Sky resolution follows region `0F000000` → sky `15000000` → highest-frequency
offline day `1A000002`. Actual records contain 13 lighting/colour keys, eight sky
object definitions and a 1.0 terminal midnight key. The latter exposed a parser
validation error during the first smoke run and now has explicit regression
coverage. `GameTime` supplies a day length of approximately 7,347 seconds.

Original sky setups, mesh fragments and textures are drawn with additive/alpha
materials in native object order. Material lighting flags decide whether vertex
diffuse colours participate; applying vertex colours unconditionally made clouds
incorrectly opaque/dark in the first render. Each celestial object has independent
material tint, and graphic variants are cached across time changes. Frames follow
the viewer and apply native heading/pitch sweeps. Day keys drive directional and
uniform ambient Unreal lights. A neutral cubemap provides uniform ambient light
without replacing the native sky artwork with SkyAtmosphere.

This is an initial native sky port. Native weather/day selection, lens flares,
cloud UV animation, sky/world fog composition and underwater effects are pending.
Intensity and colour-space conversion are rendering approximations. Far terrain
LOD is also pending; visible world distance is governed by the streaming radius.

Additional source evidence:

- Legions `portal/shared/src/region/sky/daydesc.cpp.c`: `CDayDesc::StreamPack`
  at `0x0050EAA0`, `GetTimeRatio` at `0x0050E7B0`, angle/colour interpolation.
- `portal/client/src/smartbox/worldrender/lscapesky.cpp.c`: `CalcFrame` at
  `0x00554EE0`, `SetViewerPosition` at `0x00554FA0`, ordered sky draws.
- Rust `ac2-world/src/region/{ac2_region,day_desc,sky_objects}.rs`,
  `ac2-render/src/sky.rs` and `ac2-render/src/object/pass.rs`: cross-checks for
  record framing, day spans, transforms and material lighting flags.

The real-DAT smoke run at radius 2 checks floor support, ground movement, jump and
landing, 352+ metres of continuous flight, then travel to 9882 and back to 5667.
It observes 35 blocks loaded / 5 unloaded after crossing, 60 / 35 at the distant
destination, and 85 / 60 after return. It exercises garbage collection before
day/night captures and requires a completion marker after the final screenshot.
Captures: `Unreal/Saved/Screenshots/AC2World.png` and `AC2WorldNight.png`.
These tests cover the selected travel route; they do not certify every scenery
asset throughout the 65,025-block world.

## World sandbox milestone (2026-10-26, 0.4.0)

This milestone supersedes the earlier unsupported-feature lists above.

- Water reads CWaterDesc, the two native block maps and authored surface materials.
  Height uses the LandscapeDefs lookup plus the native 0.95 m offset. Scrolling
  textures and depth-faded shores render the surface; Unreal buoyancy provides an
  initial swim mode. Below-water materials and full water lighting remain pending.
- CEnvCell records instantiate 0x01 meshes and embedded entities. Cell travel checks
  floor triangles and capsule clearance. Conservative bounds retain the owning
  block. BSP/portal culling and appearance/lighting remain unfinished. The tested
  larger dungeon contains 69 cells.
- Distant terrain covers a 14-block horizon with 20/40 m samples, skirts, slope
  normals and material-derived colours. Retained detailed tiles are hidden once
  the distant mesh covers them. Distant texture detail is approximate.
- The red sky smear came from a glow mesh (04000729) whose nearest native vertices
  were only about 1.08 m from the origin before scale. Each sky graphic is now
  projected beyond 100 km, preserving angular size and allowing world occlusion.
- Native rain/wind keys and day selection feed bounded rain and approximate Unreal
  fog. Native cloud UV controllers, flares, thunder/audio and fog composition remain.
- Setup frames, sockets, animation tracks and four native skin influences animate
  the player. Human visual 1F000023 has a 41-bone body (04000029), plus attachments.
  Verified forward locomotion clips are 0500004F and 05000050. Motion graphs,
  customization, equipment, swimming/combat animations and native physics remain.
- Scenery now follows child attachments and partial inherited parts. Native water
  height/depth rules, slope scaling and bounded noncolliding grass frills are included.
  Grass culls at 35 m. Instancing, appearance atoms, all degrades and exact clearance
  are not complete; some non-visual placed entries are still skipped and reported.
- Local humanoid NPCs wander, can be targeted/damaged, and despawn on death or travel.
  They are not a verified native monster population or server simulation. HUD actions,
  target health and a local grid map exercise the offline systems.
- PSDesc previews use the native material, first emitter's constant waveform terms
  and lifetime colour/size keys. A short emission interval is collapsed into a burst.
  Full waveforms, other emitters/controllers and automatic world FX remain pending.

Additional source evidence, treated as reference data:

- Legions sky/skytimeofday.cpp.c: rain amount and X/Y wind after replacement objects.
- Legions scene/scenegroup.cpp.c: FinishPlaceAndCheck, ScaleByWaterDepth,
  ScaleByLandSlope and dry/water height bands.
- Legions geometry/cellportal.cpp.c: compact portal records and aligned stab lists;
  cell/envcell.cpp.c for interior cells.
- Pinned Rust modules ac2-world/{ac2_env_cell,ac2_scene}, ac2-anim/setup,
  ac2-assets/{ac2_mesh,locomotion_tests,ac2_particle_desc,ac2_material}, and
  ac2-render/particles/keyframes cross-check framing and transforms. Rust code
  is not linked into the Unreal client.

Build and test instructions are maintained in Unreal/README.md. Twelve synthetic
suites and the real-DAT smoke route cover selected water, interiors, skeleton,
effects, movement, LOD, sky, garbage collection and streaming behavior. This does
not certify every asset, dungeon or interaction throughout the installation.

## Remaining implementation work

1. Terrain masks/roads, layered material controllers, appearance atoms, instancing,
   scenery degrades and an asset coverage audit across regions.
2. Native BSP/portal traversal, dungeon lighting, collision volumes, complete water
   and weather composition, and more detailed distant terrain.
3. Motion-controller graphs, equipment/customization, swimming/combat animation,
   complete FX emitter/controllers and sound.
4. Retail UI resources/state models, inventory/quests and AC2 transport/session
   integration validated against a reachable server.

The client remains an offline foundation. Online play and VR are not implemented;
local combat is a rendering/input harness.


## Scenery, weather/FX and UI milestone (2026-10-26, 0.5.0)

This replaces the first-emitter preview described in 0.4 with a bounded native PSDesc
runtime and connects static world startup effects. The current feature/limitation
list and controls are in `Unreal/README.md`.

- VisualDesc global startup FX merge along the parent chain, with first inherited
  overrides and last nonzero FX table. DBFXTable default particle nodes and sub-tables
  resolve to PSDesc attachments. Holding points, named bones, object transforms,
  particle scale and water-relative placement are applied while preparing a block.
  Grass frills deliberately do not create native physics-object startup FX.
- Native emitters retain their shape, timing, limits, waveform fields, motion keys
  and point lists. All active emitters run within shared particle/system budgets.
  Controller approximations and unimplemented FX node kinds are listed in the guide;
  successful decoding is not a claim of pixel-identical simulation.
- Material stage-zero UV controllers and their instance-property overrides now
  update dynamic material parameters for clouds, scenery and particles. Scenery
  alpha/additive blends and particle additive blends have separate shader templates.
- A regression check found that interior template 2B00005B uses SRCALPHA/INVSRCALPHA
  on a solid floor. Blend state alone cannot decide collision or transparency. The
  renderer now checks actual texture alpha and preserves structural cell collision.
- Authored idle animations can run on nearby scenery; CPU skinning is bounded by
  32 animated part groups per block and a 120 m update radius.
- Native LayoutDesc/ElementDesc/StateDesc media decode into an art catalogue. In-game
  layout 22000001 supplies 14 selected direct Surface images for the new responsive
  HUD. The Canvas HUD provides clickable hotbar, target bar, map, log, weather/time,
  travel and FX panels. This is a functional offline interface using native artwork,
  not the retail script interpreter or a server-backed inventory/quest implementation.

Evidence cross-checks used the Legions decompile's `PSDesc`, `PEKeyframeDesc`,
`ParticleEmitter`, `FXNode::StreamPack`, `DBFXTable::StreamPack`,
`PHYSICSOBJREP::ApplyStartupFX`, `LM_UV*::ApplyTextureTransform`, `MaterialProperty::Apply`
and `LayoutDesc`/`ElementDesc`/`StateDesc` serializers. The corresponding Rust
`ac2-assets/{ac2_particle_desc,ac2_visual_particles,ac2_fx,ac2_material_instance}`,
`ac2-render/particles`, `ac2-world/ac2_particle_sources`, `ac2-math/waveform` and
`ac2-ui/{layout,layout_media}` provided format/behavior comparisons. No Rust library
was linked into the Unreal project.

The new native-asset audit report is
`Unreal/Saved/Diagnostics/AC2NativeAssets-05.json`: 1,729 PSDesc records, 3,067 emitters,
11,799 lifetime keys, and all 14 local layouts decode with zero rejections. PSDesc
390006AD has non-monotonic authored colour keys; the runtime stable-sorts them.
Synthetic coverage increased to 14 suites, including independent key channels,
waveform phase conventions and malformed UI media boundaries. The real-DAT render
route now includes C654's campfire, HUD art/action dispatch and multiple live emitters,
in addition to movement, streaming, water, interiors, sky and distant terrain.

Remaining scenery work includes appearance atoms, all material stages and degrades,
instancing and native collision/clearance. Remaining FX/weather work includes exact
noise/controller semantics, moving attachments, repulsors, lights, decals, audio and
full sky/fog parity. Remaining UI work includes retail state/anchor/script behavior
and data supplied by a real session. These remain explicitly incomplete.


## 0.5.1 environment and character opacity repair (2026-10-26)

The native human body material uses SELECTARG2 diffuse/current alpha through its
texture stages. Sampling texture alpha as opacity incorrectly exposed backfaces in
0.5.0. Alpha-stage decoding and opacity classification now keep the body/head solid
while retaining authored leaf cutouts and effect blending. Native mesh winding was
checked against vertex normals and did not require reversing again.

Grass generation now uses the native +0x03804309 frill seed, follows nested frill
groups and counts nested scene emissions toward group limits. A per-vertex budget
replaces the 256-placement scan-order cap. Native meshes are merged by material
inside 20 m cells, with no collision and an 80 m cutoff. Real-DAT probes resolved
every generated frill in five blocks: 5667 (2,022), CB4D (351), F18F (795), 7F7F
(47), and C654 (6,249). Block 5667 takes 64 grass batches.

Direct Surface and RenderSurface records now share highest-valid-resolution
selection across highres and portal/local archives. The highres-only 4100034C
human body surface loads at 1024x1024; portal-only 4100034D remains a valid
512x512 fallback. Cache identity includes source directory, surface, dimensions
and wrap mode. All texture references in the five audited blocks selected highres
records. The original highres.dat contains 4,353 Surface records. Terrain atlases
are 2048x2048 and use parallel sampling/mip generation; full native terrain detail,
road and bump composition remains pending. F1 reports highres selection and grass.

World FX no longer wait for scenery uploads to finish. Terrain-specific FX nodes,
per-part startup FX and inherited part FX fields are retained; child overrides
take precedence. Holding-point offsets are preserved. Source and particle caps
remain in place. This does not claim complete retail FX parity.

Both Development targets build. All 15 synthetic suites pass. The real-DAT
environment audit passes, including opaque body material assertions and direct
highres/portal surface access. The radius-2 smoke passes walking/jumping, uploaded
grass, continuous and distant streaming, water, interiors, GC, sky, weather,
scenery FX, UI and multi-emitter preview. Reviewed the resulting player/grass image.
Evidence: `Unreal/Saved/Diagnostics/AC2Environment.json`,
`AC2Environment-0.5.1-validation.json`, `AC2Environment-0.5.1-smoke.log`, and
`Unreal/Saved/Screenshots/AC2Environment-0.5.1.png`.

## 0.5.2 travel clearance and dungeon repair (2026-10-26)

Travel now waits for nearby scenery uploads and collision cooking, then searches
native floor triangles for a supported, unblocked player capsule. A bounded floor
probe, full capsule overlap and downward sweep account for capsule height, slopes,
trees and low ceilings without falling through unrelated storeys. The last safe
position updates only after a successful landing. An unavailable safe landing
keeps the player held and reports the condition instead of releasing inside geometry.

Completed travel resets the local population and attempts three nearby spawns,
including in interiors. Dungeon spawning uses native floor points from the owning
block and checks clearance against the player, scenery and existing NPCs. Local
wandering checks walls and floor edges. These are offline human scouts; retail
monster encounter tables and server population remain unimplemented.

The red slab in cell 59600110 came from mesh 01000036's material 3100034F. Its
30000519 modifier supplies red diffuse RGB with zero alpha and no texture.
The runtime previously ignored constant alpha for untextured materials. Native
alpha classification and shader scale/bias now preserve that zero opacity.
This is a material fix, not a colour blacklist or mesh-winding change. Structural
collision is retained. The launcher upgrades stale alpha/cutout shader templates
using template version 2.

Both Development targets build. All 16 automation suites pass without warnings,
including capsule landing cases and constant-alpha material coverage. The real-DAT
environment audit passes highres selection, body opacity and the invisible dungeon
helper check. The default radius-2 smoke passes the complete streaming route,
three grounded dungeon NPCs, dungeon walking, water, weather, scenery FX and UI.
The reviewed dungeon capture shows populated rooms without opaque red helpers.
Evidence: `Unreal/Saved/Diagnostics/AC2TravelEnvironment.json`,
`AC2Travel-0.5.2-validation.json`, `AC2Travel-0.5.2-smoke.log`, and
`Unreal/Saved/Screenshots/AC2Dungeon-0.5.2.png`.

## 0.5.3 stable frills and native creature previews (2026-10-26)

The grass pop was whole-patch translucency sorting: the real C654 materials
31000012, 3100034E, 310002DF and 31000773 selected the translucent scenery pass.
Frill draws now have a separate material cache variant and use a two-sided masked
shader that writes depth. Native alpha scale/bias is retained. A stable world-space
coverage pattern fades individual pixels from 60 to 80 m; coarse batch culling is
delayed to 120 m. The five-block audit checks every generated frill material uses
the masked pass. Existing particle and scenery transparency remains separate.
The launcher creates the twelfth shader template and records template version 3.

Drudge (1F000036), Shreth (1F00061B), Reedshark (1F0004A9), Beetle (1F000031) and
Banderling (1F000075) are available through R, the F2 spawn button, or named
`AC2Spawn` commands. Their IDs come from the native NPC templates' PhysObj property
chains, recorded in AC2CreatureCatalog.h. Outdoor travel attempts all five and
interior travel attempts the first three. Body bounds determine capsule dimensions
and foot offset, then the existing safe-floor search validates placement.
These creatures use native skeletons, skinning and idle clips. Their movement is
still local wandering; complete locomotion/combat graphs and retail AI are pending.
They render within 180 m and retire beyond 220 m, with a 12-creature preview limit.

The read-only population probe decoded 15,024 EntityDesc templates, including
6,713 weenie templates. It resolved 2,800 AI-enabled templates to visuals; that
catalogue includes interactive scenery as well as creatures. Five templates have
unsupported property payloads and are explicitly reported. All 20,784 outdoor
placements across 3,348 block-info records and all 151,868 interior placements
across 57,013 cells resolve through supported templates to static VisualDescs.
There are no placed weenie/generator entities in those records. The native
CEncounterDesc 14000000 is a 16-byte empty record: no map and zero rows. Thus no
verified retail monster coordinates were available to populate automatically.
Authored static placements continue to stream as scenery; nearby test populations
are labelled as tests. No server encounter locations have been fabricated.

Both Development targets and all 16 automation suites pass. The radius-2 smoke
checks all five native species, depth-writing frills, dungeon floor support,
walking, water, streaming, sky/weather, native FX and HUD actions. It captures
grass before/after a 2 cm camera translation and the mixed creature population;
visual review found no whole-clump sorting changes in that comparison.
Evidence: `Unreal/Saved/Diagnostics/AC2GrassEnvironment.json`,
`AC2Population-source.json`, `AC2GrassCreatures-0.5.3-validation.json`,
`AC2GrassCreatures-0.5.3-smoke.log`, and versioned captures in
`Unreal/Saved/Screenshots/`.

## 0.5.4 native creature locomotion (2026-10-26)

The preview creatures slid because the old loader could only reach direct clips
or the first switch child. Their AnimMap movement entry 80000001 leads through
AMHuman and AnimCube graphs, so every creature loaded idle but no walking clip.
AC2Animator now resolves the map and evaluates bounded AnimAsset, AnimCube,
AMHuman and AMSwitchME trees. It rejects cycles, truncated records and malformed
lattices. Speed axes use native first-axis-fastest ordering; clips blend at a
shared normalized cycle phase with authored periods and per-bone weight keys.

Creature and player animation receive actual velocity transformed into model
space, converted from centimetres to metres and adjusted for visual scale.
Forward and sideways velocity select the gait; short input smoothing and an
idle/movement blend prevent abrupt changes when starting or stopping. The player
no longer needs hardcoded human walk/run clip IDs. CharacterMovement still owns
world displacement and collision. This adds ordinary locomotion, not the complete
retail behavior state machine, turn-in-place, combat clips or animation FX triggers.

Both Development targets build. All 17 automation suites pass, including graph
switching, reverse movement, speed clamping, different-period clip blending,
cycle wrap, truncated lattices and recursive-node rejection. The real-DAT
`-Locomotion` audit finds 24 moving bones for Drudge, 20 for Shreth, 27 for
Reedshark, 24 for Beetle and 21 for Banderling; the human has 23. The radius-2
world smoke passes and requires changing movement poses while each of the five
creatures is actually moving (230–244 updates per creature in the recorded run).
Successive rendered frames also show changing creature limb poses.

Evidence: `Unreal/Saved/Diagnostics/AC2Locomotion.json`,
`AC2Locomotion-0.5.4-validation.json`, `AC2Locomotion-0.5.4-smoke.log`,
`Unreal/Saved/Screenshots/AC2Creatures-0.5.4.png` and
`AC2CreaturesMoving-0.5.4.png`.

## 0.6.0 native-art character interface (2026-10-26)

Added character selection/creation, an animated portrait, inventory/equipment and
statistics screens. Slate provides keyboard focus, text entry, clicks and scalable
layout; Surface artwork from 22000007/22000001 supplies the frame and the four button
states. This is a functional local implementation using native assets, not the
retail UI/script runtime. I opens inventory, C statistics, and F4 character selection.

The character catalogue is decoded at runtime from portal WState **70000390**,
CharGenMatrixData/package 912. The bounded heap graph preserves native hashes/lists,
gmRaceSexInfo, physique AppearanceProfile values and StartInvData entries. It yields
ten race/sex physics entities and 26 distinct starter inventory templates across
Human, Lugian, Tumerok, Drudge and Empyrean. Preview and player models both use
these entity IDs. Height/build choices are limited to values present in the DAT.
Changing body size fits the capsule and checks the available floor/clearance before
returning world input. Streaming clearance also uses the current capsule dimensions.

Reference paths: AC2_Rusted's ac2-script/src/host/chargen.rs (CharacterGenSystem
calls), ac2-ui's avatar_selection/avatar_modification/confirmation screens and
inventory controller, plus native LayoutDesc records 22000006/07/08 and 22000001.
Tools/AC2/ui_probe.py reproduces the layout/script/matrix investigation and writes
Saved/Diagnostics/AC2UI-source.json. No script bytecode is executed by this new path.

MasterProperty 34000000 types the starter EntityDesc overrides. StringTable nouns
are decrypted/localized from local_English.dat and their grammar suffixes removed.
Explicit item slots, preclusions, race/level restrictions, icons, armor and weapon
damage are read when available. Older starter templates with script-only defaults
retain source-derived labels and starter placement conventions; they are not claimed
to have complete server item definitions. Missing/generic icons display initials.

Local profiles have eight slots and four 24-slot backpacks. Equip/unequip operations
are transactional: conflicting gear returns to free cells, and a full pack rejects
the operation without losing items. Known weapon damage drives the local strike;
unknown defaults retain the existing sandbox 25 damage. Level, XP and vitals are
explicit local sandbox values. Saves use a versioned JSON schema, temporary-file
replacement, invariant checking and duplicate-name checks. Failed loads preserve
the original file and disable writes. Normal saves live in Saved/Characters;
automation uses separate GUID-named files in Saved/Diagnostics.

Both Development targets build. All 20 automation suites pass, including native
matrix/localization checks, truncation rejection, equipment replacement/full-pack
failure and persistence/corrupt-save preservation. The UI smoke checks all ten
previews, creation, equipment, inventory/statistics rendering, reload and restored
world input. The radius-2 world smoke also passes. Screenshots were inspected and
corrected for clipped pack labels and localization annotations. Evidence is under
Saved/Diagnostics/AC2UI-0.6.0-validation.json, the AC2UI smoke log and
Saved/Screenshots/AC2UI-creation.png, AC2UI-inventory.png and AC2UI-stats.png.

Still pending: native face/hair/skin and clothing AppearanceFile application,
equipped clothing/held-weapon meshes, all item script defaults/icons, retail
progression/vital formulas, skills, quests, original creation scenes and complete
native layout/script behavior. Equipment currently updates local state and known
combat stats; it does not dress the base model.


## 0.7.0 retail creator composition, appearance and DAT text (2026-10-26)

This pass supersedes the 0.6.0 creator/appearance limitations above. The avatar
selection uses UIScene 2100000B and HoldingLocation mapper 2300001A to place all
ten race/sex previews. Modification uses the race's scene from LayoutDesc 22000007,
its native Body/Head/Clothing controls, image states and rectangles. The naming
modal uses 22000008, a 16-character field and native button/input artwork.

CharGenMatrixData's full AppearanceProfile lists are now decoded, with localized
labels from the Chargen enum table (41000003 -> 250007C9). AppearanceTable 0x20
base inheritance and intensity thresholds select material modifiers, mesh swaps
and part-group replacements. Modifiers apply to cloned materials, preserving
shared source resources. Starter garment key 0x10 enables the garment mesh before
its colour modifier. StartInvData controls initial equipped state. Selection
indices persist in schema 2; schema 1 receives native defaults. Save validation
rejects unknown/out-of-range appearance indices without overwriting the file.

Empyrean EyeColor (physique 4096) is not a face material override: table 20000489,
key 4100002E selects PSDesc 390008AF through 390008B6 for holding locations 7/8.
The renderer now retains appearance FX atoms, attaches their sources to animated
setup sockets and updates local-space particles when those attachments move.
Preview captures include the particle actor; world avatars use the same path.

All game-owned HUD/Slate text uses DAT Font 0x42 records directly. Font::StreamPack
provides glyph widths, Unicode mappings, strip offsets and line height, with
8-bit coverage or MSB-first packed monochrome pixels. Runtime atlases preserve
coverage and add transparent gutters; text measurement, wrapping, native '?' fallback
and input caret/selection use authored advances. Layout font references resolve
through template children. Creator title/button/label fonts and NameEntry 42000024
are DAT identities, not substituted TrueType families. Inventory/statistics,
character selection, item tooltips and the world HUD share this renderer.

References: Legions 7160 pregame generation decompiles, Font::StreamPack/
GenerateCharacterMap/GetCharDesc/GetStringWidthInternal, and AC2_Rusted's
ac2-assets/src/font.rs, ac2_appearance.rs; ac2-ui/src/pregame; and native UIScene/
HoldingLocation data. Reference repositories and original DATs are unchanged.

Remaining: complete retail UI scripting/window behavior, native scene lights/fog/
music and transitions, general appearance opacity/FX semantics, held weapons,
arbitrary equipment templates and online character creation. The current local
character creator is not a server account/character service. Engine developer
console text remains engine-owned. Validation artifacts for this pass are listed
in Saved/Diagnostics/AC2UI-0.7.0-validation.json.

## 0.8.0 DAT layout evaluation and stable previews (2026-10-26)

The first UI pass selected native art but arranged it in custom windows. That
composition was rejected during review and has been replaced. FAC2UILayout now
retains root indices, element type/default state/z/anchors, every state's attributes,
child indices and media. FAC2NativeUI evaluates reference templates with cycle/depth
limits, current/stateless attribute precedence, state propagation, authored parent
sizes, inclusive edge anchors, clipping and sibling order. Native strings, text
attributes and fonts are used directly. Original images tile unless their DAT
attribute explicitly requests stretching. Game bindings supply values and visibility.
Viewports participate in the native root Z order, so the selection background is
painted behind its 3D preview rather than covering it.

The creator evaluates 22000006/07/08. This restores the authored race/sex heading
at the top and description beneath the lineup, the original tab labels, slider
thumbs/tracks and nonadminclient naming dialog. The extra handwritten title, zoom
button and substitute naming dialog were removed. Race-specific model viewports
use their authored rectangles. 22000002 supplies character selection, its list row
template and create/enter controls. Selection still previews one local character;
the retail rotating arm scene and portal/torch effects are not implemented. Account
options, credits and deletion remain disabled. Selection and creation support the
existing eight local profiles and appearance/persistence behavior.

The old invalid byte 0x97 in a selection label caused DAT '?' fallback. That string
is no longer drawn; selection now uses native string-table entries. C++ sources are
UTF-8. Portrait bounds are cached per native physics entity, so appearance and body
changes do not move the camera/floor. Rebuilds sample the existing idle time.
Intentional head/body zoom and rotation are preserved.

The game evaluates StatusbarField in 2200002B and ToolbarField, ShortcutField,
ChatWindow1, LeftSidePanel/ExaminationField, RightSidePanel/InventoryField and
PaperDollViewport in 22000001. HUD coordinates are native pixels, with DAT anchors
against the physical viewport: centered 798-pixel toolbar and right-bottom 316x414
inventory. The creator retains the authored 1024x768 composition fitted to the
viewport. Local vitals populate the native meters and numeric fields. C displays
local character values using ExaminationTextTemplate; inventory items use the same
native examination window for details. First two shortcuts bind local strike/FX.
As a local usability constraint, chat is moved above the toolbar when a short
display would otherwise overlap its entry; its DAT size and child layout are retained.

Inventory uses native equipment slots and six-column backpack list artwork. A click
selects/inspects, double-click toggles equipment, and clicking an empty pack cell
moves a selected unequipped item. Pack buttons and mouse-wheel scrolling retain all
96 existing local cells without discarding save data. Retail capacities differ and
are not enforced. Native UI geometry does not imply retail progression/server rules.
The previous custom inventory/stats composition has been removed.

Chat uses the native frame, log/entry bounds and Font 4200000C. Its generalized DAT
editor supports printable covered glyphs, selection, caret navigation, clipboard,
512-character limit, 50-message history with draft restoration, and a bounded
200-message wrapped log. /say, /s, /me, /emote, /help, /loc and /clear are local.
Server channels report unavailable. Typing captures input; submit/cancel and opening
inventory restore the appropriate world/cursor state. No chat messages are sent.

Reference checks: Legions 7160 UIElement::{SetState,Update_Internal,
UpdateSizeAndPosition}, gmToolbarUI/gmVitalsUI/gmExaminationUI, avatar modification
and selection/confirmation decompiles; AC2_Rusted ac2-ui/src/{element,place,meter,
scrollbar,toolbar,paper_doll,chat_window,examination,pregame}. References are read-only.

Validation includes installed-DAT rectangle/anchor checks at 1024x768, 1600x900 and
1280x720, template and rollover art resolution, localized selection strings, native
entry fonts, ten rendered avatar/camera checks, equipment persistence, chat focus
and input restoration. See Unreal/Saved/Diagnostics/AC2UI-0.8.0-validation.json for
final run reports and screenshots.

Remaining: server gameplay/chat, native radar and target overlays, chat routing and
options, draggable/resizable/persisted windows, complete legacy blend/alpha-mask
composition, timed media/transitions, UI script VM, retail progression formulas,
quests/skills/social data, selection arm scene, native scene lights/music and held
weapons. Window placement now comes from the DAT trees; this is not a claim of
complete retail UI behavior or pixel-identical renderer parity.
