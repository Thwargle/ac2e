# AC2:Unreal — DAT client, controller and account frontend (0.11.2)

[Download the Windows installer and see the update guide](https://thwargle.com/ac2-unreal/).

Native Unreal Engine 5.8 client foundation for Asheron's Call 2. The active project
is `AC2Unreal.uproject`; it has no ACEClient dependency. It supports offline exploration
and native multiplayer through AC2E or Pyrealium. See [network setup](../Network/README.md)
and the [AC2E server project](../AC2E/AC2E-README.md) for capabilities and limits.

The login screen supports saved AC2E/Icefire servers, accounts per server, and
optional passwords protected by Windows Credential Manager. Each server has its
own DAT folder; connecting with another set reloads all DAT-backed assets before
login. Original and Icefire data can coexist. Icefire's first-connect DAT updates
are installed into separate verified snapshots; the frontend reloads and retries
login automatically, and the server profile remembers the updated folder. See [profiles and data versions](../Network/README.md#saved-servers-and-accounts).

The default graphics path is DX12/SM6 with virtual shadow maps. Legacy cascaded
shadows failed at Dereth's native coordinates despite working near UE's origin;
short contact shadows were the only visible result. VSM restores full cast
shadows following the native sun/moon direction, including off-screen casters.
CPU-animated meshes invalidate cached shadow pages when their vertices move.
`-Dx11` remains a diagnostic fallback and does not provide this shadow fix.
`-AC2EnvironmentSmoke -AC2OutdoorShadowSmoke -AC2Block=8eb6 -AC2Radius=1`
captures morning/evening views at Lost Wish and compares a caster enabled/disabled
before and after camera movement. Profile automation uses an isolated
`-AC2ProfilesSmoke -AC2LoginProfiles=<absolute path>/test-profiles.json` and verifies
both directions of an actual DAT reload; it requires both installations.

Tumerok Shelter uses the native animated bridges' and towers' separate physics
meshes, retained in group space. The earlier added spiral stair is no longer needed
and has been removed.
The movement safety guard also accepts the walking capsule's floor contact across
native floor seams, preventing the reported invisible backtracking wall.
`AC2.Data.TumerokPassageWalking` drives both sexes and size extremes through the
reported passage and back, checking actual movement and the runtime safety guard,
and walks the reported native ramp in both directions at both size extremes.
`AC2.Data.StarterClearance` checks forward and reverse routes in all three racial
Shelters against native collision. `AC2.Data.TumerokHeldWeapons` checks recovered
drums/javelins and both sexes at height/build extremes. Held weapons follow the
animated socket without inheriting body scale. `AC2.Data.HeldWeaponsFollowRunning`
checks the actual engine tick order during running; local attachments update after
the character's animation and movement. Verified native javelin clips
hide the held item after release and restore it on recovery or cancellation;
`AC2.Render.TumerokJavelin` captures the windup, release, recovery and running offscreen.

Material template version 20 routes world opaque/cutout/grass lighting through
UE's shadowed direct-light pass, retaining DAT texture/color stages, native ambient
fill and authored finish parameters. Native cell lights become movable point
lights on the dungeon lighting channel with stable shadow casting and whole-light
distance fades, removing the camera-dependent nearest-eight shadow switches.
World albedo normalizes native MODULATE2X/4X stages that consume DIFFUSE, so their
light amplification cannot clip texture detail before the shadowed lighting pass.
Sunlight uses the outdoor channel. UI previews retain native vertex lighting and
the original stage operations.
`AC2.Render.NativeMaterialFinish` checks preview color fidelity and measures both
sun and point-light shadows, including amplified diffuse-stage texture cases.
`-AC2EnvironmentSmoke -AC2LightingSmoke` captures the
reported native ramp, the dungeon bridge, and outdoor scenery.

To play on your local server, start `start-ac2e.bat`, then run
`Launch-AC2.ps1 -Online -Server 127.0.0.1 -Port 9000 -Account local`.
The client opens a sign-in page first. Connect to a server, or choose **Offline characters**.
Character selection/creation stays opaque until the chosen character and its destination
are ready; the world, sky and default avatar are not created behind the initial login page.
The server stores network characters separately from offline profiles. The controls
below describe offline mode; network targeting, attacks, item use and chat route to the server.

**Auto Target** in Options → Game uses retail's native text and defaults to on.
An incoming harmful skill selects its attacker only when nothing is selected;
misses count, while healing does not. Existing targets and selected corpses stay
selected. Apply changes the behavior and saves the native character option;
Revert and confirmed Defaults use the same settings flow. Selection does not
automatically enter combat. When the selected object disappears or becomes
unselectable, Auto Target picks the nearest eligible living monster within
75 metres outdoors or 50 indoors. Native XY range, vertical distance penalty
and unsigned-ID tie ordering are preserved. NPCs, players, pets and known
corpses are excluded by the current PvE filter. Targeted attack cancellation
keeps combat stance; selecting alone does not begin repeat attacks. Native
head/eye look-at, last-target commands and full PK/faction filters remain gaps.

Options → Game saves supported character preferences to the server: damage text
and third-party range, radar categories/coordinates, shortcut lock, number labels
instant bar scrolling and auto targeting. **Shortcut Bar Height** offers the
native one-to-five-row dropdown: choose a value and press Apply. Revert restores the applied value;
confirmed Defaults applies one row. Dragging the grip updates the selector and
saves the height immediately, preserving unrelated pending edits. Login
restores each character's profile without changing pending edits on ordinary
refreshes. Native flags not exposed in this UI are retained. Client graphics,
audio, chat fonts and other unmapped settings remain local; a character without
saved options uses the local defaults until an explicit preference change.

For crafting, open Skills → Craft and use **All professions** to choose a discipline.
Select a recipe, adjust its level with **−/+**, and fill its ingredients from your
backpack. Clicking an ingredient cycles matching items; quantities can span stacks.
Craft becomes available after server approval while alive and in peace mode.
The panel displays base success chance, cost, rank and XP; **Raise** spends profession
XP. Manufacturing/refining recipes require the matching nearby workshop.
Creation tools equipped in the main hand add a matching profession bonus; the
displayed base chance excludes that bonus. Inspect a tool for its rank requirement
and remaining durability. Gathered resources and tool wear persist atomically.
For gathering, create a Surveying map, examine its coordinates, use it near the
site, then double-click the deposit with the appropriate tool in your backpack.
For butchery, select a recent corpse from your kill and use a hunting knife.
Progress and tool/deposit charges survive restarts. Spellbinding extraction and
totem application, recipe discovery and the Arwic Mines sword remain unfinished;
see [coverage and reconstruction policies](../AC2E/emulator/content/README.md#profession-crafting-and-gathering).

Character previews retain the DAT scene lights and material colors, captured to an
explicit sRGB LDR target without tone mapping or exposure adaptation. The character
list keeps DAT font `42000024` and native row geometry; selected text uses a gold
outline-backed contrast adjustment instead of the original dark blue.

Combat actions use the native BehaviorTable layer indices and blend times. A single
action phase drives the body and attached parts; mode changes cannot select a different
branch midway through a clip. Death overrides other actions and holds its final pose.
Offline corpses live through their decoded clip; AC2E retains starter corpses for
3.5–5.5 seconds (the native clips last up to 4.375 seconds), with a 12-second fallback
for other profiles. These are presentation lifetimes, not retail corpse/loot rules.
Startup particle emitters are separate from appearance FX overrides. Cast_Attack uses
the native hand emitters, and racial impacts resolve their DAT banks. Weapon swing
cues start and stop ribbons on their authored holding locations. The blade model
supplies the ribbon edges, texture, sampling interval and fade. Some native sounds,
decals and elemental skill scripts still lack presentation support.

## Run

From the repository root, run `Launch-AC2.bat`. It builds the editor target and opens
a game window. Repeated launches reuse the current build while this project's game
or editor is open, so multiple clients can run together. Each game window has its
own `Saved/Logs/AC2Play-*.log`; use a separate server account per player. Close the
game/editor windows before rebuilding to apply code changes.
Windows PowerShell 5.1 is supported. Defaults are block **5667**,
radius **2** (25 detailed blocks), and noon. The launcher prepares twelve project-owned
shader templates when absent or outdated. DAT renderers read in place; original archives are never modified or bundled.
Icefire updates create separate snapshots as described above.

The default data directory is `Asheron's Call 2 - Legions/` under the repository root.
Required: `portal.dat`, `cell_1.dat`, `local_English.dat` (native UI and item names).
Optional high-resolution textures: `highres.dat`. The verified data identifies as Legions 7160;
the newer 0930 decompile is a comparison source, not evidence of matching DAT support.

```powershell
.\Launch-AC2.bat
.\Launch-AC2.bat -SkipBuild
.\Launch-AC2.bat -Block 5561 -Hour 18 -Radius 2
.\Launch-AC2.bat -Mode Editor
.\Launch-AC2.bat -Mode Build
.\Launch-AC2.bat -DatDirectory 'D:\AC2' -Dx11
```

## Controls

- W/S move forward and back, A/D strafe, and the arrow keys move or turn. Hold Shift to walk. Space jumps.
- Drag with the left mouse button to orbit freely. Releasing it while running brings the camera back behind the moving character. Drag with the right button to steer. Hold both buttons to move forward.
- Tab switches the camera view. F toggles flight where available. Q/E adjust height in flight or deep water. Holding Space swims up.
- T selects a target, E uses it, and Z switches combat stance. The number keys activate the current shortcut bar.
- M opens the world map, I opens inventory, C opens the character sheet, and Enter opens chat.
- Map: click the world map or wheel up to zoom; drag or click edge arrows to pan
  (Shift uses a smaller step). Right-click, wheel down, or the detail back button returns
  to the world map. Click your marker or check **Lock to Player** to follow your position.
- Click detail-map landmarks to display their native description and coordinates.
  The five continent buttons open draggable native difficulty maps.
- The radar stays in the upper-right corner, rotates with your character, and shows
  nearby actors. Hover for names or click to select a target.
  Its small blue button toggles the coordinate readout.
- Escape closes an open panel or opens Options. In **Core → Key Bindings**, select an action, press the new key or chord, then choose **Save**. The editor also supports clearing a binding and restoring defaults.
  The Core page also provides Leave World and Quit. The other native pages expose camera,
  mouse, display/quality, vital numbers and chat font settings, with Apply/Revert/Default
  and draggable sliders. Applied values persist in Unreal's GameUserSettings.ini.
- Home or `/recall`: request server lifestone recall (resurrection when dead).
  Bind by targeting a nearby lifestone and pressing 2. Server distance, combat and
  cooldown checks apply. Offline recall returns to the session start and restores vitals.
- F4 returns to character selection.
- Enter: open/submit local chat. Escape cancels entry and restores movement.
  Up/Down recalls sent messages and restores the unsent draft; mouse wheel over chat
  or Page Up/Down while typing scrolls the log. Commands: `/say`, `/s`, `/me`, `/emote`,
  `/help`, `/loc`, `/clear`. Connected sessions also support tells and server chat.
- Character selection/creation captures keyboard/mouse input. Escape from creation
  returns to selection; Escape from selection disconnects and returns to sign-in.
- With the cursor active, native toolbar buttons open inventory, examination, map
  and help. Shortcut keys activate the actions assigned to their slots.
  Click either vital meter to toggle its numeric value.

Tilde opens the console:

```text
AC2Block 5667             outdoor travel (hexadecimal)
AC2Block 5561             water test location
AC2Cell 58700100          small interior
AC2Cell 59600110          larger dungeon cell
AC2Time 18               native day fraction displayed as 0–24 hours
AC2Weather 0             choose a native day definition (0–2 here)
AC2Rain 1                force local rain; -1 restores native rain keys
AC2Spawn 1F000023         native visual ID for an offline NPC
AC2Spawn Shreth           spawn a named test species (case insensitive)
AC2Effect 39000003        native PSDesc particle preview
```

## Implemented passes

Terrain, textures, buildings, vegetation, native sky, water and interior meshes
come directly from the DAT archives. Detail tiles stream asynchronously with a
one-block retention ring; uploads use bounded game-thread batches. Movement waits
for an available floor. Travel also waits for nearby scenery and collision cooking,
then checks the entire player capsule and sweeps onto a supported floor. Blocked
destinations search nearby native floor triangles; the player stays held if none
is safe. Retired tiles release CPU data and GPU resources can be
reclaimed through periodic garbage collection. Worker jobs join on shutdown.

Texture selection compares valid surface candidates in `highres.dat` and the base
archives. Direct Surface IDs and RenderSurface mip lists share that resolver,
including highres-only records and portal fallback. The local highres archive has
4,353 surface records. The verified body selects 1024x1024 from highres, versus
512x512 from portal. Texture cache identity includes the archive directory, selected
surface, resolution and wrap mode. F1 reports actual highres use and grass counts.
Terrain still uses a baked atlas, now 2048x2048 per block with parallel CPU sampling;
it does not yet retain the full resolution of every native tiled terrain stage.
Full mip chains and texture-group anisotropic filtering are used for environment
textures. Native bump/detail/road blending remains future work.

A separate worker builds distant terrain out to 14 blocks (about 2.2 km on each
axis). It uses 20/40 metre samples, native material-derived vertex colours, slope
normals and skirts. It is a coarse horizon pass, not a second full terrain atlas.
Retained detail tiles are hidden once the matching distant mesh covers them.

Water uses the native `CWaterDesc` height and type maps. The shoreline mask
controls how much of the riverbed shows through. A mirrored camera captures
sky, terrain and scenery at the nearest visible lake elevation. The retail
bump texture distorts this reflection, and two scrolling foam samples animate
along the bank. A submerged camera sees the DAT underside material. Buoyancy
uses Unreal movement, and underwater lighting remains approximate.

Interior cells render their native meshes and static contents with triangle
collision. Cell travel selects a floor triangle and checks player capsule clearance,
slope and overhead space. Bounded floor probes avoid dropping through another storey.
Cell bounds retain the owning block while exploring connected rooms. This is a
conservative bounds approximation; native BSP traversal, portal visibility,
appearance overrides and dungeon light systems remain unfinished.

Native sky setups, layer ordering, alpha/additive textures, celestial transforms
and day/night keys are retained. Sky layers are projected beyond the world while
preserving angular size, fixing the camera-following red glow over terrain.
Native day definitions, rain/wind keys and fog colours now drive weather previews.
Near and distant terrain sample the same moving cloud-shadow texture in world
coordinates. The night horizon uses the coverage of the DAT sky matte to blend
the sky smoothly as the camera looks up.
The directional light follows the visible sun or moon and blends back to the
native day-key direction near the horizon. The sampled day key supplies direct
and ambient colour and intensity. Terrain and object materials apply their
authored ambient fill.
Leaving the world destroys its directional light, skylight and fog, so selecting another
character cannot accumulate lights. Foliage keeps its authored lighting normal on both
sides of a card; frills use the native placement's upward normal at every vertex.
A weighted native hash selects days at midnight. Fog is approximated with Unreal
height fog. Native stage-zero UV translate, rotate, scale and matrix controllers
now animate cloud/scenery/particle textures, including material-property overrides.
Rain streaks respect shelter and expire at traced surfaces, creating short local
splashes. Lens flares, thunder/audio, additional material stages and full native
fog/underwater composition remain incomplete.

The player uses native skeletons, skin weights, attachment sockets, locomotion graphs
and behavior keys for jump, landing, attack, damage, death and recall. Water selects the
native swim mode. The human behavior table contains 5,337 combinations; the previous
4,096-entry bound silently rejected it. Unscaled bone matrices now drive rigid parts
and attachment sockets separately from scaled skin deformation, fixing the detached
head during running. Partial bone weights retain the underlying pose.

The world view uses spatial FXAA and disables motion blur. CPU-deformed procedural meshes
currently lack previous-frame deformed vertex positions, so Unreal's default temporal
super-resolution leaves trails on moving mobs and limbs. FXAA avoids that history artifact;
thin foliage can look less smooth than temporal AA. Accurate deformation motion vectors
are still needed before enabling TAA/TSR for these meshes.

The controller reads MotionInterpDesc's 22 fields and terrain/global tables. Directional
speed, acceleration/braking, turning rate, jump impulse, gravity, terminal velocity,
buoyancy and drag feed a custom Unreal movement component. Water/sea-floor thresholds
come from the native descriptor. Native server mode, behavior, damage, death, impulse,
velocity/acceleration/jump scale, motion-descriptor and global-modifier events are
connected to the player. Zero speed/jump scales are preserved.

This is not complete retail controller/physics parity: Unreal still resolves capsule
contacts and floor support; original BSP collision, all terrain contacts/modifiers,
behavior layers/priorities, animation-triggered impulses/FX, all skills and weapon
attachment variants remain unfinished. The current action evaluator runs one action
over locomotion. The options window uses DAT layout/art/fonts but currently implements
13 functional settings, not every retail option. Account login is a new form using DAT
art/fonts; it is not a recovered original account-login layout.

Scenery now follows child attachments and partial inherited part definitions.
Water-relative placement, water-depth/slope scaling and bounded grass frills are
included. The native frill seed and nested frill groups are retained. Every vertex scene gets
a placement budget; grass meshes merge by material in 20-metre cells and cull at
80 metres without collision. Frills use a dedicated two-sided, depth-writing masked
material instead of sorting entire translucent patches. Individual pixels fade
between 60 and 80 metres using a stable world-space pattern; batch culling starts
at 120 metres, beyond the visible fade. Shader template version 4 also preserves native
world-space lighting normals on grass and cutout materials; the launcher updates existing templates.
The starting block 5667 resolves 2,022 grass placements
into 64 batches; the previous 256-frill scan-order cap left most of the block bare. Nearby authored idle
animation runs on up to 32 scenery part groups per block within 120 metres.
VisualDesc startup FX resolve inherited global and per-part groups, child overrides,
terrain-specific FX nodes and recursive DBFXTable entries, with setup holding
points/bone offsets and water-plane placement. Sources become available as soon as
the block terrain commits, without waiting for every scenery draw to upload.
These sources start and retire with streamed blocks and distance. Campfires now burn.

Alpha/additive scenery materials are recognized. Native alpha stage operations determine whether
a texture alpha channel affects opacity. The human skin template keeps diffuse
alpha opaque; treating its texture mask as transparency caused the 0.5.0 see-through
body regression. Constant diffuse alpha also controls untextured materials: native
zero-opacity dungeon helpers no longer appear as solid red patches. Shader template
version 2 applies the native alpha scale/bias and is regenerated by the launcher.
This is covered by synthetic and real-DAT material checks; interior structure collision is independent of visual blend
mode. Full retail parity remains unfinished: appearance atoms, animated scenery
collision and all attachment/motion graphs, exact clearance, instancing, all distance
degrades, roads, terrain alpha masks and detail/bump stages. Some non-visual placed
entries are still reported as skipped.

Offline NPCs wander, can be selected, retaliate with native attacks, receive local damage,
play hit/death reactions and despawn after a five-second corpse display
or travel. Each completed outdoor trip attempts five native test species; interior
trips attempt three. The templates' PhysObj references identify Drudge, Shreth,
Reedshark, Beetle and Banderling visuals. Capsules fit their decoded body bounds,
and spawn positions check the resulting capsule against scenery and other actors.
Wandering checks walls and floor edges. Native idle clips and velocity-driven
locomotion now play for all five species. The loader evaluates AnimMap locomotion
entries through AMHuman, AMSwitchME and AnimCube nodes, blending the native clips
at a shared cycle phase. Actual model-local velocity drives the forward/sideways
sliders, with visual scale and metres/second conversion; stopping blends back to idle.
The player uses the same graph path. Full behavior/mode transitions, turn-in-place,
animation-triggered FX and retail AI remain unfinished. Creatures draw within
180 metres, pause preview animation outside that range, and retire beyond 220 metres.

The available client data does not supply verified monster spawn positions:
20,784 outdoor placements and 151,868 interior placements all resolve to scenery
VisualDesc records; the native encounter descriptor 14000000 has zero rows and no
encounter map. Test creatures are explicitly labelled in the HUD and placed nearby,
not presented as retail populations. `Tools/AC2/population_probe.py` reproduces this
read-only audit and resolves 2,800 AI-enabled templates through their PhysObj properties
(including interactive scenery such as barriers and jump pads).
Five unrelated EntityDesc templates contain unsupported property payloads and are
reported as rejected; all audited placed entities resolve through supported templates.

The HUD now evaluates the installed LayoutDesc trees rather than positioning a selection
of native pictures inside custom windows. **2200002B** supplies StatusbarField and
its health/vigor meters; **22000001** supplies ToolbarField, ShortcutField,
ChatWindow1, LeftSidePanel/ExaminationField, RightSidePanel/InventoryField, equipment
slots and the paper-doll viewport. References, current/stateless attributes, child
states, inclusive coordinates, anchors, sibling order, clipping, tiling, text
margins, colours and fonts come from those records. The toolbar remains 798 pixels
wide and centered at the bottom; the inventory remains 316x414 and docked right.
At higher resolutions HUD pixels stay native size; they are not scaled as a page.

Health/vigor use the active local profile. C opens the native examination window
with the known local character values; selecting an item uses the same native text
row template for item details. Retail server vital/progression formulas are not
connected. Chat uses the native frame, log/entry bounds and Font 4200000C, with
512-character editing, local speech/emotes, 200 log messages, 50 sent-message history
entries, wrapping and scrolling. Typing captures movement/look and restores input
on submit/cancel. Inventory is a docked world panel, not a modal input screen.
There is no chat transport or simulated reply from other players/NPCs.

The weather/travel panel, help and FX browser remain sandbox tools. Skill, quest and
social toolbar buttons report that their gameplay data is unavailable. Target-selection
overlays, chat-window routing/options, general window dragging/resizing/persistence
and complete retail UI messages are unfinished.
The renderer uses Slate compositing; it does not yet implement every legacy surface
blend/mask operation, animation or UI transition.

### Native world map and radar

`AC2MapData` reads the installed region's game-map image, detail-map block lookup,
all 95 tile rows and their localized point-of-interest notes. `AC2MapUI` resolves the
actual NavigationField, RadarField, HeadingField and OverviewMap layout trees from
`local_English.dat`. Images use the existing DAT/highres texture loader; all labels,
coordinates and tooltips use DAT bitmap fonts. The old coordinate-grid overlay is removed.

The 128-land-cell detail window joins tiles across block boundaries, clips markers,
supports native 9-cell/Shift-3-cell pan steps, and retains player-follow until manually
panned. The difficulty buttons open Osteth, Omishan, Linvak, Arramora and Knorr's native
500-pixel maps. Indoor travel suppresses the outdoor marker and coordinates while radar
continues tracking nearby actors. Radar uses the verified 7160 ranges: 75 metres outdoors
and 50 indoors. Its right-edge placement adapts to viewport size without scaling the artwork.

Offline mobs and live server objects both appear on radar; selecting a server object sends
the native target command. Server contacts currently use a neutral gold marker because the
bridge does not yet evaluate each entity's RadarLook script. Quest/server-added map notes,
party-specific radar symbols, saved window placement and the server calendar remain future
work. The radar is the retail compass/object display, not a rendered terrain inset.

### Character creation, inventory and statistics

The first session opens character creation after a safe world landing. Later sessions
open character selection. There are eight local slots, validated unique names and
the five native races (Human, Lugian, Tumerok, Drudge, Empyrean), both sexes and
the physique and appearance choices read from `portal.dat` **CharGenMatrixData 70000390**.
The ten native physics entities render animated, rotatable previews. Entering
applies the selected model, proportions, face/skin/hair and worn starter clothing
to the player and checks capsule clearance for the new body size. World movement is disabled while a character screen is open.

The creator uses the authored 1024x768 composition and control rectangles from
**22000006/07/08**, with the native ten-avatar selection scene, race-specific
modification scenery, Body/Head/Clothing tabs, sliders, randomization and naming
modal. AppearanceTable material/mesh/part replacements apply face, hairstyle,
beard/crest, skin and clothing choices. Empyrean eye colour selects native PSDesc
systems at the animated eye sockets. Available controls vary by race and sex.
Race headings and descriptions now use the DAT states and localized strings.
The earlier invalid UTF-8 selection separator was removed. Preview
framing and floor anchoring stay fixed per race/sex while hair, garments, face and
body proportions change; rebuilds retain the current idle animation time. Explicit
head/body zoom still changes framing. Selection uses the native **22000002** window,
list rows and enter/create controls. Its preview shows the selected character; the
retail rotating multi-character arm scene, portal/torch effects and account controls
are still incomplete. Delete, options and credits are disabled.

All game-owned text now renders bitmap glyph coverage and advances from DAT Font
records (**0x42**). Creator controls resolve their fonts through LayoutDesc references;
the name entry uses **42000024**. HUD, inventory, stats, selection screens and item
tooltips also use DAT fonts. The custom name editor uses those same glyph widths
for its caret, selection, scrolling and mouse hit testing, with copy/cut/paste and
the retail 16-character limit. There is no system-font fallback in these screens.
Unreal's own developer console and engine diagnostics retain their engine fonts.

Native Surface artwork supplies frame and button states. Slate hosts the controls;
the retail UI scripting VM, every window constraint, scene light/fog/music and
selection transition effects are not reproduced. Portrait lighting is an Unreal
approximation. Native layout evaluation supplies the window geometry; local C++
bindings supply the supported interactions and offline values.

Each new character receives the native race's starter inventory (26 distinct
templates across five races). Offline inventory preserves four 24-slot pages;
AC2E uses its authoritative 78-slot bag (42 main slots and three 12-slot packs).
Slots use the native six-column list and equipment artwork.
Click an item to inspect it; double-click to equip/unequip. Click an empty slot on
another pack to move the selected item. Mouse-wheel over the pack reveals the last
rows. Existing offline 96-slot saves are preserved. Replacement equipment returns displaced items to
the backpacks. Full bags, conflicting slots and known race/level restrictions
reject the operation without losing items. Equipment and pack positions persist.

Online inventory supports dragging bag items between positions or onto compatible
equipment slots, and dragging worn items back into the bag. Double-click replaces
occupied equipment in one server transaction; the outgoing item takes the incoming
item's bag position, including when the bag is full. Item selection follows its
network identity across object recreation. Consumption and quest turn-ins remove
the item and compact bag positions; reordering applies the server's final ordinal.
VisualDesc/appearance icon layers supply colored armor and weapon artwork, with
the native qualities descriptor as the fallback for potions and quest items.
Tooltips render above the panels, icons and paperdoll. The current authored loot
rules grant rewards directly to the backpack; corpse containers and dropping items
into the world are still unfinished.

Network scenery with solid opaque geometry now blocks the character and camera;
translucent effects do not form walls. Native locomotion graphs distinguish moving
creatures from animated stationary props such as lifestones. Grounded placed NPCs
align their native foot plane to streamed floor collision without changing their
authoritative network position or grounding flying creatures. Portal meshes and
the invisible native interaction hulls of effect-only portals participate in mouse
picking; solid objects continue to ignore halos. Interaction hulls stay nonblocking.

The native examination panel shows local level/XP/vitals and known item armor.
Native qualities descriptors and entity overrides provide localized names,
descriptions, fallback icons and known bonuses. Unresolved script defaults retain
source-derived labels, and unknown weapon damage in the offline sandbox uses the
existing 25-damage sandbox strike. Retail progression/skills/vital formulas are
not connected. Native StartInvData equips starter clothing on creation. Equipping
or removing those garments updates the preview and player; selected garment colours
persist. Held-weapon visuals and appearance for arbitrary looted equipment remain
unfinished.

Local data is stored in `Saved/Characters/characters.json`, schema version 2.
Version-1 saves migrate with native default appearance choices.
Writes use a temporary file and replacement. Invalid/unsupported saves are left
untouched and saving is disabled with a visible error. Native DAT files are never
modified. Automated UI tests use separate GUID-named saves under `Saved/Diagnostics`.

Native PSDesc systems now execute all active emitters with timed births, emitter/system
particle limits, start/duration windows, point/square/circle/sphere/swept-line emission,
spread, independent colour/scale/mass keys, spin, streaks, constrained billboards and
alpha/additive material textures. Physics honors gravity/wind flags. Waveform and
point-file controllers run locally; interpolated point motion uses an approximation.

The shared runtime cap is 4,096 particles and 64 systems, with the nearest 48 scenery
sources selected within 220 metres and 64 cached presets. Unused preset materials can
be reclaimed. Previews stop emission within 15 seconds; authored finite-lived systems
are not restarted repeatedly while resident. Static source identity remains stable
while a block stays loaded. Noise uses Unreal's deterministic Perlin function, not
Turbine's permutation/global dice. Distance-triggered emission, exact fast-forward and
physics timestep semantics, repulsors, world-rotation/snap details, general animated FX
attachments beyond appearance-selected particles, sound/light/decal FX and complete controller parity are still pending.
Rain and melee sparks remain project-local effects.

## Verification

```powershell
.\Launch-AC2.bat -Mode Test -SkipBuild
.\Launch-AC2.bat -Mode Inspect -SkipBuild
.\Launch-AC2.bat -Smoke -SkipBuild
.\Launch-AC2.bat -UISmoke -SkipBuild -Block C654
.\Launch-AC2.ps1 -ControllerSmoke -SkipBuild
```

`Test` runs 30 automation suites covering archive bounds, terrain fields
and holes, texture alpha/native material opacity, placement hashes, residency/seams, sky interpolation,
water record bounds, native animation interpolation and locomotion graph selection/blending,
waveform values, independent
particle channels and bounded UI layout/media decoding. Capsule landing tests cover
trees, nearby obstructions, ceiling height, floor support and stacked floors.
Sky lighting checks cover cardinal/zenith light direction, the installed day keys and
light/fog cleanup over repeated world entry. The launcher checks the engine exit and exported test counts.
The character tests also read the installed DAT matrix and localized item overrides,
exercise inventory replacement/full-pack failures, and round-trip saves while
checking invalid-save preservation, appearance migration and invalid selector rejection.
Font tests decode every installed font, verify glyph coverage/metrics and malformed
record rejection, and check referenced creator fonts and name-entry validation.
Chat tests cover parsing, offline channels, history/draft restoration, message limits,
printable DAT glyph validation and selection-label coverage.
Native layout tests resolve installed DAT templates, strings, state artwork and
Z order, and check anchored rectangles at 1024x768, 1600x900 and 1280x720.

`UISmoke` checks all ten animated avatar previews and first/last appearance choices,
asserts an unchanged camera during appearance/proportion changes, creates an isolated
character, equips an item through the UI action, opens inventory/statistics, reloads
saved appearance/equipment and verifies restored world input. It also verifies chat
keyboard focus, submit/cancel/history and focus handoff to inventory, with partial
health/vigor and game-window captures at 1600x900 and 1280x720. It writes
`Saved/Diagnostics/AC2UI-smoke.txt`, a dedicated log and screenshots
`AC2UI-creation-{lineup,body,head,clothing,name}.png`, `AC2UI-inventory.png`,
`AC2UI-stats.png`, `AC2UI-empyrean-eyes-{first,last}.png` and `AC2UI-world-*.png`.
The final capture is `AC2UI-character-selection.png`.

`ControllerSmoke` uses the regular frontend with an isolated offline character store:
it checks the absence of world/default-avatar loading before sign-in, selection/name,
entry, movement/attachments, jump, hit/attack/death, native Escape pages and slider
interaction, logout/re-entry, and zero/restored server motion scales. It also verifies
FXAA/motion-blur settings, one active environment after reentry, and the light's actual
direction against native morning, zenith and night keys, with rendered captures at each time.
Captures are
`Saved/Screenshots/AC2Controller-*.png`; its report is `Saved/Diagnostics/AC2Controller-smoke.txt`.
`Network/test_login_ui.py` starts isolated account/game services and verifies password
login through the real in-client form, named selection before world entry, lifestone
binding and native recall, server combat animations, damage, death, XP and loot.

`Inspect` validates the archive indexes and all 65,025 terrain records.

`Launch-AC2.ps1 -MapSmoke -SkipBuild -Dx11` verifies native maps, tile loading, marker
positioning, player-follow, drag/arrow panning, landmark selection, radar target selection,
continent-map dragging/closing, coordinate toggling, interior travel and restored input.
It uses an isolated character profile and writes `Saved/Diagnostics/AC2Map-smoke.txt`
and `Saved/Screenshots/AC2Map-{world,detail-radar,overview,interior}.png`.
The 27 `AC2.Data` tests include map-coordinate inverses, cardinal radar directions,
world edges, native map records/strings, button artwork and radar placement at three sizes.
`Network/test_map_ui.py` starts an isolated AC2E server, creates a test character and runs
the network render smoke; it selects a server mob through the radar before combat/loot checks.
For the native FX/UI audit, run the editor commandlet with `-run=AC2Inspect -NativeAssets`.
The 7160 audit decodes **1,729 particle descriptors, 3,067 emitters, 11,799 keys and
14 layouts**, with zero rejected records. One descriptor (390006AD) contains
out-of-order colour keys; stable sorting makes it safe to interpolate.
`-run=AC2Inspect -Environment` checks five real terrain blocks for complete frill
resolution/batching, active highres selection, opaque body/head materials,
the zero-opacity dungeon helper material 3100034F, and both
a direct highres-only Surface and a portal-only fallback. Results are written to the
inspection JSON. The audited blocks are 5667, CB4D, F18F, 7F7F and C654.

`-run=AC2Inspect -Locomotion` resolves the five creature locomotion graphs and the
human player graph from the installed DATs. It requires changing bone poses across
the cycle and records the selected clip IDs and period in the inspection JSON.

`Smoke` runs the real DATs offscreen and requires a unique completion marker.
It exercises uploaded grass, native player/NPC rendering, target damage, particles, walking/jumping,
continuous streaming, distant travel, water buoyancy, small and large interiors,
three grounded dungeon creatures, dungeon walking, changing locomotion poses while
all five preview species move,
depth-writing grass and before/after captures of a 2 cm camera translation,
distant LOD, garbage collection, day/night capture, native HUD art/action dispatch,
C654 scenery FX, a multi-emitter preview, outdoor rain and indoor rain suppression.
Captures are in
`Saved/Screenshots/AC2Player.png`, `AC2World.png`, `AC2WorldNight.png`, `AC2Water.png`,
`AC2Interior.png`, `AC2Dungeon.png`, `AC2SceneryFX.png` and `AC2NativeFXUI.png`.
Creature and grass regression captures are `AC2Creatures.png`, `AC2CreaturesMoving.png`,
`AC2GrassBefore.png` and `AC2GrassAfter.png`.
The selected routes do not certify every
scenery asset, dungeon or interaction in the world.

This workspace contains AC2 only. The copied AC1 project and its generated content
have been removed; the separate `C:\dev\ACE` project is unchanged.
