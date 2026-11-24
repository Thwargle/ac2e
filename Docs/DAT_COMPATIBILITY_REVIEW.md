# DAT compatibility and gameplay readiness

Reviewed November 13–14, 2026 using the installed Legions archives, the locally patched
Icefire snapshot, the client runtime decoders, AC2E content validators, and the
available retail decompile. Neither the archives nor player saves were modified.
No live Icefire connection was made for this review.

**The client and AC2E are not yet complete retail implementations.** Every indexed
archive record was readable; all animation graphs now load, including the eight
previously unsupported procedural-controller graphs.
The server loads its implemented content successfully; that does not establish
complete skill execution, quest progression, population, or multiplayer behavior.

## Measured archive support

The stock set is `Asheron's Call 2 - Legions/`. The Icefire set is the saved
server-data snapshot `revision-1794449780219139600`, including the previously
downloaded portal revisions 138–139 and English revisions 10–11. Counts below
include records in each archive separately, including high-resolution overrides.

- **Stock:** 287,445 records across five archives, all readable; 39,762 texture
  surfaces decoded to pixels without rejection; 65,025 terrain records decoded.
- **Icefire:** 281,198 records across five archives, all readable; 39,940 texture
  surfaces decoded to pixels without rejection; 65,024 terrain records decoded.
- Both sets decode all 14 native UI layouts. Stock decodes 1,729 particle
  descriptors; Icefire decodes 1,560. The sampled environment, hidden-helper,
  body-texture and high-resolution fallback checks pass for both sets.
- Stock decodes 2,465 setups, 4,501 geometry records, 6,504 visual descriptions,
  and 1,136 appearance tables. Icefire decodes 2,473 setups, 4,302 geometry
  records, 6,615 visual descriptions, and 1,186 appearance tables.
- Stock decodes 130 material templates and 2,401 instances. Its projected
  framebuffer template `2B000065` and instance `31000323` sample the opaque
  scene through the authored camera-space UV transform and second-stage
  opacity mask. Icefire decodes 122 templates and 2,348 instances. Every
  runtime-selected material layer in both sets has a supported rendering path.
  Both profiles use the high-detail anisotropic lookup from `2B000051` for
  five material instances. Stock also includes this technique in `2B000083`.
  These passes generate the two lookup coordinates at each vertex from the
  surface normal, view direction and directional light, then interpolate them
  across the surface. A material which ignores the sun uses the view direction.
  Stock still contains 35 alternate pixel-shader 1.1 passes and Icefire has 34.
  Retail class-2 hardware enables 28 stock and 27 Icefire passes on high
  detail, and seven in each profile on medium detail. None applies on low.
  The Unreal selector uses a supported authored technique for these materials
  independently of Unreal graphics quality. The remaining passes require the
  native pixel program and its vertex outputs. Common bump-mapped programs
  also require native tangent frames that the procedural mesh path discards.
- Texture loading preserves the supplied DAT mip levels, including levels from
  different archives in the same chain. Missing levels are generated between or
  below them. The stock distribution contains 4,353 multilevel texture records
  and Icefire contains 4,299. Material instances inherit the template's texture,
  tint, alpha, UV transforms and addressing defaults before applying overrides.
- All 22 stock and 23 Icefire FX scripts decode after the pre-roll fix below.

These are parser and focused runtime checks. They do not establish correct
appearance for every asset, complete cross-reference resolution, correct
collision along every route, or native behavior for every decoded UI control.
Asset prefixes are interpreted within their dataset: `41xxxxxx` in a cell
archive is a location, not a texture surface.

## Decompiled-client comparison, 2026-11-13

The available `decompiled-admin7148.zip`, `decompiled-legions7160.zip` and
`decompiled-client0930.zip` bundles were compared with the bridge and Unreal
handlers. Bundle names identify the sources; they do not certify compatibility
with every version of a remote server. The read-only AC2_Rusted reference also
supplies recovered protocol structures. The GUN wrapper is available, but the
original GUN DLL and Icefire server source are not.

This pass corrected these concrete differences:

- `ECM_Physics::RecvEvt_DoFX_Private` / `CPhysicsObj::DoFX_Private`: the native
  event has no object address and plays each supplied FX on the current player
  at scalar 1. The bridge previously discarded the event. It now forwards it,
  and Unreal resolves its player target while still rejecting stale addressed
  events.
- `CBaseQualities::UnPack`: PlayerDesc now forwards all ten decoded quality
  tables through the same conversion as subsequent updates. Previously only
  integers, floats and long integers survived the initial snapshot. Shared
  skill-recovery timestamps now arrive at login, private object updates resolve
  to the local player, 64-bit IDs remain exact, and non-finite numeric values
  are rejected. This is transport coverage, not implementation of every property.
- `ModeTracker::ReleaseBehavior` and `BehaviorTracker::StopLayer`: releasing an
  existing action is distinct from starting its optional replacement. The
  bridge preserves that order and the original behavior ID. Unreal releases
  the existing hold without restarting it and stops only the addressed native
  layer. Both operations validate incarnation stamps and preserve corpse poses.
- `BehaviorTracker::DoBehavior`, `BehaviorTrack::Update` and
  `ReleaseBehavior`: the global character template selects hold behaviors.
  The body motion map supplies the clip's normalized loop bounds. Playback
  crosses the intro once, repeats that interval and finishes from the current
  phase when released. The skill presentation signals completion after the
  requested repeat count while the track continues to play. The stock and
  Icefire starter matrices both use a 0.01–0.99 loop for Human, Lugian and
  Tumerok Recall charges and a 0–0.99 loop for Drudge and Empyrean.
- `SkyObject::StreamPack` / `CDayDesc::GetSky`: `obj_name` is a local replacement
  key, not a global sun/moon enum. Across all inspected day records the stock
  sun/moon keys are 6/5, while Icefire uses 5/7 and adds other sky objects.
  Lighting now resolves their role from the base celestial graphics
  `04000023`/`040000B6`, preserving replacements and ignoring fully transparent
  objects. Unrecognized sky content retains the authored light-key direction.
  Quarter-hour checks cover the entire day against the rendered mesh centers.
- AC2E GUN presence updates now retain the latest game-world snapshot while the
  social service is busy, instead of silently losing login/logout changes when
  its state lock is held. The service retries delivery; the TCP integration test
  deliberately holds that lock during both presence transitions.

The comparison also confirmed unimplemented paths that must remain open:

- Decoded object actions including `DoSlider`, `SetHoverHeight`, targeted
  impulses, terrain-modifier changes and server-directed `MoveTo` are not yet
  connected completely to the Unreal runtime. Snapshot sliders also need their
  native ownership and constant-rate interpolation rules.
- Behavior parameters carrying attached FX/scripts, script callbacks and
  ramp-down cleanup still need a complete lifecycle. Some transition effects
  also remain unimplemented.
- PlayerDesc package IDs and embedded weenie metadata are not fully applied;
  forwarding the quality tables does not close that gap. Unrecognized scripted
  method calls remain diagnostics rather than executable gameplay.
- The Icefire profile still has unresolved automation failures. Several tests
  pin stock-only resource IDs or layout dimensions, but the remaining failures
  have not all been shown to be fixture-only. They cannot be waived as a group.

No live Icefire session was used in this pass, as requested. Its previously
reported interaction/loot server-error GUIDs cannot be declared resolved by
offline testing. AC2E quest/loot/skill implementation is a separate question
from client packet and DAT compatibility: the decompiled clients do not supply
all authoritative server rules.

## Procedural animation implementation

Stock now loads all 5,123 animation graphs; Icefire loads all 5,045. The recovered
`Animator::Create` and controller implementations supplied the missing behavior:

- `0500002A`: `AMLook` (`40000005`), the procedural look controller.
- `050002FD`: `AMAngle` (`4000000A`), the angle controller.
- `05000275` and `050003F3`: `AMRandom` (`40000007`).
- `050000FF`, `0500010D`, `050003E1`, and `050003F0`: dependent graphs that now
  resolve their random controllers.

Random controllers retain per-object slider state, duplicate-child probability
weights and interpolated transitions. Retail resets the idle delay after a ramp
completes and evaluates its variation on inactive updates. Repeated pose, period
and FX queries at one timestamp cannot reroll the choice. Look uses native bone
roles and axis-angle weights; ground angle uses authored bone weights, limits and
the recovered transition. Initial procedural behaviors now run alongside locomotion.
Replicated look-at/direction events preserve instance stamps and target IDs.

Tests sample the eight affected graphs and exercise actual meshes using movement
maps `03000023` / `0300004D`, as well as Human/Tumerok rigs at different scales.
The strict render-record audit now passes. Camera-driven outgoing look updates
now use the native half-second reporting interval and change threshold. A target
behind the player or more than ten metres away is released after more than two
seconds without clearing the combat selection. Head tracking uses three quarters
of a creature's scaled height and the midpoint of other objects. Camera tracking
uses the orbital displacement in metres, the asymmetric side-view remap and
per-axis body scale from `LookAtManager::Update` (0930, `0x005073A0`). Target
tracking reproduces the acquisition and release weights and the pitch division
in `LookAtLogic` (`0x0050732B`). Camera sliders take 0.1 or 0.2 seconds per axis,
and replicated direction sliders take 0.5 seconds.

## Fixes made during this review

- **Compound skill effects:** rating bundles preserve every component and its
  own expiry, including both caster and target ratings when targeting self.
  Health/vigor periodic components preserve independent cadence and duration;
  refreshing a bundle replaces its prior components together. Capacity checks
  reserve queued effects before persistence. Recognized separate health/vigor
  regeneration records with the same recipient and timer are combined rather
  than rejected. Unknown effect components still reject the cast rather than
  charging vigor for a partial implementation.
- **Native stat operations and recipients:** the v3 importer follows compound
  children and keeps each stat operation, including explicit zero values. Both
  builds use stat magnitude field 36; build 7230's vital magnitude uses field 37.
  Supported rating multipliers/additions and regeneration multipliers execute;
  unsupported override/stack operations remain rejected. Instant health/vigor
  restoration can affect the caster and another recipient without double-applying
  when both are the same player. The native 7230 archive was separately validated,
  including its record hashes, rather than substituting Icefire's different DAT.
- **Status FX replication:** newly visible objects carry active, unexpired FX in
  their native PhysicsDesc snapshots. Entering visibility no longer requires a
  fresh effect-start event to display an existing status. Multi-recipient status
  admission is atomic, and expired effects are excluded. Tests round-trip the
  snapshots through the network bridge for ordinary, parented and full-appearance
  objects. This does not add cross-session persistence of temporary effects.
- **Particle lighting and lifetime:** emitters with the native lighting flag use
  ambient and the first five local lights with the recovered attenuation. Source
  FX lights participate in that sample. Unlimited emitters remain until StopFX;
  unshared emitters stop producing particles but retain existing particles for
  their authored lifespans. Shared emitters retain their distinct release rule.
  The old unconditional 15-second cutoff is removed. In particular, `41000031`
  is ongoing vigor-damage status FX, not a one-shot Lightning Bolt impact.
- **Particle motion and geometry.** The PSDesc renderer applies emitter fades,
  three-axis spin, authored world rotation and the native streak time to triangle
  and quad particles. Portal and particle waveforms share the retail seeded
  gradient-noise implementation. Each installed profile contains six presets
  using the repulsion controller. FX table `1E000057` supplies six attraction
  or repulsion fields, including a 3.5 metre field gated by movement. Model FX
  and streamed scenery register their authored fields. Controller 3 evaluates
  their range, velocity threshold, constrained axes, damping and return spring
  at the preset's simulation step.
- **Weapon trails.** Model startup FX and appearance overrides provide the two
  endpoints, material, fade duration and sampling thresholds of each blade ribbon.
  Avatar swing cues in shared FX table `1E000057` activate or release the
  corresponding held attachment. The renderer samples both edges in world space,
  advances texture U by distance travelled and fades the strip after the swing.
  The archive scan found 1,240 trail definitions in Legions and 1,211 in the
  Icefire distribution, including weapons with two independent strips.
- **FX scalar ramps:** audio and appearance scalar updates follow authored ramp
  time, including retargeting and removal. The recovered particle scalar callback
  is a no-op, so it does not justify inventing particle-count or brightness scaling.
- **Blood-decal profile lookup:** the ordinary-hit splatter presentation now
  resolves the native Decapitate decal variants from the installed FX tables.
  Stock uses table `1E000076`; Icefire uses `1E000077` and a different green-blood
  material. Gore/hue gates, ground projection, fade and expiry remain intact. The
  underlying Decapitate asset is native; its reuse on ordinary hits is AC2E policy.
- **Startup and FX audio:** placed scenery and model startup sounds now use
  their native origin, scalar, day/night/player gates and interior visibility.
  Streaming updates do not restart finished one-shots; preview models do not
  duplicate world audio. Looping FX volume changes retain their playback context
  and follow authored ramp times. MusicInfo flags route Music and Player Music
  separately. Offline PCM tests and installed scenery tests cover both DAT sets.
- **Icefire FX script `5700000C`:** its first cue has authored phase `-0.01`.
  The decoder previously rejected the entire script. Retail `FxScript::GetTriggers`
  accepts cues already due at the first update. The client now retains pre-roll
  cues and dispatches them once on the first advancing update. Tests cover the
  installed record's event fields, the subsequent cue, and non-repetition.
- **Appearance opacity atoms:** the appearance reader previously skipped these
  records. It now resolves priorities, equal-priority replacement, and the retail
  `-999` applied-scalar sentinel. Appearance FX update opacity on existing meshes;
  zero hides a section, removing the effect restores its base appearance, and
  fractional values use a translucent material. Native material templates are
  version 24. Neither installed DAT contains these atoms, so this is recovered
  format support, not evidence that it fixes an existing asset in these sets.
  Unit tests and rendered 1.0 / 0.5 / 0.0 opacity checks cover both native texture
  stages and the fallback path in world and preview materials.
- **Profile-aware server coverage:** `Runtime::coverage()` now reads the same
  stock or 7230 skill table as the runtime. Previously a 7230 runtime could report
  stock IDs. A separate integration test validates every reported 7230 skill,
  record ID and race against that profile's table.
- **Repeatable diagnostics:** the inspector now supports all-record reads,
  surface-pixel decoding, and a scan through the actual render decoders. The new
  `dat_readiness` example uses the server's bootstrap and content validators
  offline without opening a socket or player store. The environment fixture now
  uses a body material valid in both profiles, rather than assuming the same
  numeric ID has the same meaning in Icefire.

## AC2E gameplay readiness

The stock server audit reports client version 7148, the `legions` content
profile, available geometry, and **11 passing validation groups**: bootstrap,
skills, skill effects, skill presentation, emotes, starter gear, Shelter quest
items, quest definitions, potion recipes, professions, and loot.

The loaded catalogs contain 26 crafting disciplines, 364 recovered production
recipes, 855 additional item profiles, 146 observed creature loot tables with
222 compatible items, 69 treasure-weapon profiles, 333 native trophy rules, and
312 native loot-quantity rules. No entries were withheld by those loot loaders
in this run. These counts describe implemented catalogs, not all retail content.
Retail random affixes and the complete treasure-weight formulas remain
unrecovered; observed bundles alone cannot establish the complete loot system.

The skill audit classifies 1,927 definitions:

- 579 have executable core effects; 235 are core profiles for the three currently
  playable races. This category does not promise all specialized behavior of a skill.
- 431 need specialized effect handlers.
- 29 need recall destinations or entitlement rules.
- 878 have no executable active effect decoded; this includes passive definitions
  and must not be interpreted as 878 missing active spells.
- Nine have implemented passive mastery behavior; one uses bound-lifestone recall.

The separately validated 7230 profile reports 586 executable core definitions
(238 for the three playable races), 473 specialized definitions, 29 pending travel
definitions and 854 without a decoded active effect, again including passives.
Neither coverage count certifies area, multi-hit, combo or summon behavior.

There are 217 known NPC dialogue profiles and 88 recovered quest profiles.
Dialogue text and definition validation are not proof that every NPC starts a
quest, that each objective advances, or that all rewards and repeatability rules
work. See [content recovery](ADMIN_CONTENT_RECOVERY.md) and the
[gameplay implementation record](NATIVE_GAMEPLAY_PASS.md) for the implemented
flows and their documented limits.

The work still needed before calling this a fully functional game includes:

1. Verify the full native locomotion/action transition set in gameplay, including
   visual comparisons of camera-driven head poses across playable rigs.
2. Complete specialized skills: area and multi-hit targeting/timing, combos,
   summons, stacking categories, additional status effects, and travel rules.
3. Connect remaining NPC scripts to executable quest states and verify complete
   starter-to-overworld and later-region journeys, with valid placement,
   collision, respawns, quest drops, turn-ins and persistent rewards.
4. Recover and implement the remaining loot formulas and item modifiers rather
   than treating observed drops as complete retail treasure tables.
5. Complete expansion player races and their creation, equipment, skill and
   starter content. Their preview models do not establish server support.
6. Continue protocol/UI integration checks for Icefire inventory, interaction,
   chat, targeting and late updates. This offline review does not certify those
   reported live problems as fixed.
7. Verify remaining presentation paths: alternate native shaders, water and shore
   rendering, light/shadow stability, remaining FX callbacks, and audio
   event coverage. Some referenced audio data is absent from both installed sets;
   for example, the quest-completion chain references missing wave `0C00088F`.
   See [audio limitations](../Audio/README.md).

## Icefire interaction and missile follow-up (2026-11-13)

- The bridge previously discarded `PhysicsStory` missile preparation/fire hooks
  (kinds 2/3). Retail `StoryManager::CreateAndParentMissiles` / `FireMissiles`
  creates local visuals without requiring a network `CreateObject`. The bridge
  now preserves these hooks and their animation/attack-hook indices. The client
  reads missile definitions from the selected DAT, launches at native AnimInfo
  story triggers (`40000002`), follows the target, and destroys the transient
  visual on arrival. Stop/cancel removes queued releases; replacing a story
  flushes its remaining releases, following `FinishStory`.
- Javelin `316` and arrow `CF` definitions resolve in both installed sets, with
  native speeds of 25 and 35 m/s. This implementation covers the reviewed base,
  linear and parabolic attack-hook layouts; flight currently uses the existing
  target-following integrator, not full native parabolic/MissileParameters
  behavior. Kind-2 preparation uses the already equipped visual. Neither this
  work nor the test below certifies every missile/skill variant.
- Icefire's training Menhir Glyph is `47001AED` (Empyrean `47001AEC`), whereas
  stock uses `47001582`. `gmEntity::IsMultiItemUse` reads integer quality `2D0`.
  Item parsing and instance updates now retain that quality, and one-target
  items send the selected object's ID. A glyph without a selected target sends
  no request. Multiple-target collection still needs its native UI workflow.
  The tutorial journal instructs players to double-click the glyph, then click
  the Menhir Portal. The backpack now waits for that click and uses its exact
  target. Escape or a short right-click in the world cancels targeting. The
  native usage constructor with a one-element target list also runs in the
  offline probe. The tool hand-in uses a backpack drag onto the Shelter Scout
  or her visible name, including her selected name during a drag. These checks
  exercise client input and outgoing requests. Icefire owns quest advancement.
- Ordinary world hover keeps the native selection cursor. Local target and
  stance notices are removed, and generic UsageDone string `25000014/4E1AEC40`
  is quiet. Custom reply text, dialogue and errors are retained.
  AC2E also stops sending generic stance, pickup, drop and loot-success notices;
  inventory/animation events, quest replies and transaction amounts are preserved.
- All six `AC2.Network` tests pass on each DAT set (stock: two warnings; Icefire:
  none), including native equipment/glyph targeting, stale instances, death
  poses, missile launch/cancel/replacement and private player events. Bridge
  tests pass: 79 main plus five capture tests. Five additional opt-in DAT-oracle
  tests pass for Icefire selection/use, stance, individual transfer and bulk
  inventory descriptors.
- `AC2.Render.StoryMissileFlight` exercises the native Tumerok throw hook and
  captures a separate projectile at three positions with a graphics device.
  Stock and Icefire reports and frame evidence are under
  `Saved/IcefireInteraction/render-story-*` and
  `Unreal/Saved/Screenshots/StoryMissiles/` (ignored by Git).
- **Unresolved:** the reported Icefire corpse GUID errors and Shelter Scout
  missing replies have not been reproduced or fixed by this offline pass.
  Requests match the installed DAT constructors; that does not establish
  correct state/timing or server behavior. Future client GUID-error logs now
  include the last twelve whitelisted gameplay requests with timestamps and
  numeric object IDs, excluding credentials, names and chat. No live Icefire
  login or player message was performed in this pass.

## Verification and reproduction

- Unreal Editor and game targets, network bridge release and isolated server
  release builds: passed. The already running server was left untouched.
- Stock `AC2.Data`: 101 passed, including 16 with warnings; zero failures with
  the DAT directory explicitly configured. Warnings remain visible in the report.
- Focused opacity, FX-script and graphics tests: three passed, zero failures.
  The graphics test checks native RGB, direct-light shadows, base color and fades.
- Server suite: 358 passed across 26 suites, four ignored, zero failures. Fresh
  isolated UDP scenarios also pass corpse rename/inspection, combat-to-loot stance
  restoration, conversion, ownership/range checks, replay protection, inventory
  persistence, fellowship lifecycle, allegiance consent/reconnect and local,
  General, tell and membership-restricted chat delivery. No public chat was sent.
- Network bridge: 82 passed (77 main and five capture tests), seven ignored,
  zero failures. Tests cover initial qualities, private FX, ordered behavior
  release, object identity and existing inventory/chat/transport behavior.
- Nine focused Unreal tests pass on each DAT set: all six `AC2.Network` tests,
  full-day celestial lighting, interaction audio and scenery startup audio. Stock
  has three warnings; Icefire has none. These tests use the real DAT resources
  without connecting to a remote server.
- `AC2.Render.ServerTimeSkyCycle` also passes on both DAT sets with D3D12/SM6,
  capturing 06:00, 12:00, 18:00 and 23:00. It verifies visible, changing sky
  imagery and clock behavior, not complete sky-material or shadow parity.
- Audio bridge: six offline PCM/rendering tests passed; this does not test the
  physical output device or every gameplay audio event.
- Comprehensive DAT audits: both stock and Icefire pass every archive, pixel,
  terrain and strict render-record decoder check, with zero rejected records.
  The subsequent framebuffer change covers the last two stock selected-layer
  fallbacks. Unsupported alternate shader layers are counted independently.
- The combined Icefire `AC2.Data` + `AC2.Network` run completed 107 tests:
  90 passed (one warning) and 17 failed. `ConcurrentWindows` was rerun with its
  required Icefire crafting fixture and passed. The original invocation omitted
  `-AC2CraftFixture`. Sixteen groups were still failing at the end of that run.
  Earlier audio failures from a `-nosound` invocation passed with audio enabled.

On November 14, focused runs against both installed sets decoded every graph
and checked every motion-map binding (5,123 graphs and 11,659 bindings for
Legions, 5,045 and 11,752 for Icefire). All ten race/sex models passed the
selection and pickup/putdown checks. Tests for equipped weapons during running,
mounted and swimming clips, casts, combat, death, animation FX triggers,
native controller tables and looping FX timing passed on both profiles.
The projectile script test checks a different effect count and duration on
each profile. The portal test streamed a dungeon arrival and outdoor
destination across 1,603 frames on Legions and 1,745 on Icefire. These are
offline client results. A two-client AC2E test exercised the native Enter_World
effect for the arriving player and an observer. Both installed DAT profiles
also rendered the effect, drained its particles after StopFX, and snapped a
nearby avatar to a new teleport stamp before resuming movement interpolation.

Four retail portal captures place the owner in portal space, lock movement and
play Portal_Use and Enter_World on the traveler, followed by Activate on the
portal. AC2E implements that sequence for ordinary routes and the Menhir
Glyph. A three-client UDP test measured 1.50 seconds to transfer and verified
effect order for the owner, a nearby observer, and an observer arriving during
travel. Movement resumed after the owner acknowledged the destination. Recall
uses a separate native sequence, with a release at eight seconds and transfer
at ten. Both DAT profiles resolve the avatar and portal effects to particles.

NativeWindowLayouts, ProfilePanels, NativeCursors, ConcurrentWindows and
LiveExamination also passed on both DAT sets. A local AC2E server and two
clients exercised live monster vitals and the equipped weapon's chance to hit,
item inspection before and after loot transfer, and rolled item stats after a
restart. An observer examining another player's equipped staff received the
same 115 damage, treasure level 56 and level 1 requirement as its owner.
Examination and combat now use the same damage lookup for rolled weapons and
the Arwic and Esper quest weapons. The hit percentage comes from the combat
resolver and includes active
attack and defense modifiers. It describes the examining character's normal
attack against that living target.

On November 14, `NativeCharacterMatrix` and `SkillEquipmentFeedback` passed on
both profiles. The character importer reads slot and armor qualities from the
active DAT and matches both the original entity and qualities records before
using recovered pile/worn apparel metadata. Three Icefire Linvak scrolls reuse
Legions Guardian armor template numbers. The regression checks that they have
no armor or equip slot, verifies seven actual wearables on both profiles, and
exercises six starter weapon types with their native hotbar artwork.
`NativeEquipmentLifecycle` also passed on both profiles. Dressed and undressed
Tumerok rigs at the shortest/widest and tallest/thinnest scales had a maximum
tail seam gap of 0.00095 cm across the sampled animations. D3D12/SM6 captures
of both sexes and scale extremes passed against both DAT sets.

Both vault-arrival walk-away routes and the retail radar tests passed on both
DATs. The Icefire fixture had loaded stock entity and visual IDs. The vault
fixture now resolves the shard, scout and exit in the active archive, and the
client identifies portal markers using that archive's radar category. A stock
portal ID also names a Thunderstorm pet in Icefire.

Open groups from the November 13 Icefire run include the following:

- Academy scenery and stair repair expectations (`AcademyStairCollision`).
- Fonts, map resources and numbered/item shortcut frames (`NativeFonts`,
  `NativeMapResources`, `ItemShortcuts`, `TextShortcuts`).
- Prop icons and material culling fixtures (`WorldPropsAndInventoryIcons`,
  `NativeMaterialCulling`).

Several failures pin stock-only resource IDs or dimensions. They have not all
been independently classified as fixture-only and are not waived. The strict
decoder audit and offline regressions cannot certify live NPC interactions,
looting, visuals or remote server behavior.

Full generated reports and logs are local under `Saved/DatReview/` and
`Saved/Tests/compat-*` (ignored by Git). Rust output is in
`Saved/test-compat-{server,bridge}.log`; the fresh local scenarios are recorded in
`Saved/test-compat-{corpse-loot,social}.log`. Reproduce from the repository root,
using the locally configured Unreal and Rust toolchains:

```powershell
$editor = 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$project = Join-Path $PWD 'Unreal/AC2Unreal.uproject'
$data = Join-Path $PWD "Asheron's Call 2 - Legions"
& $editor $project -run=AC2Inspect "-AC2DatDir=$data" `
    -FullRecords -SurfacePixels -FullTerrain -NativeAssets -Environment -RenderRecords `
    "-Output=$PWD/Saved/DatReview/ac2e-audit.json" -NullRHI -unattended

# Repeat the inspector with the Icefire snapshot as $data and a separate output.
# Both recorded strict inspector runs now exit 0 with no rejected records.

$env:PYREALIUM_UNREAL_LEGIONS_DATA = '1'
cargo run --manifest-path AC2E/emulator/Cargo.toml --example dat_readiness -- `
    $data Saved/DatReview/server.json
cargo run --manifest-path AC2E/emulator/Cargo.toml --example skill_coverage
cargo run --manifest-path AC2E/emulator/Cargo.toml --example skill_coverage -- --7230
cargo test --manifest-path AC2E/emulator/Cargo.toml
```

Use stock `$data` for `dat_readiness`: Icefire is a client compatibility target,
not an AC2E content profile. A successful catalog check has the limited scope
recorded beside it; `fully_functional_game_verified` remains false. Likewise,
read Unreal automation's JSON test results: process exit zero alone does not
mean every automation test passed.

## Icefire giving and ground drops (2026-11-13)

The inventory drag handler previously sent a ground drop even when released over
an NPC. It now resolves the release target and sends a separate give request for
a living creature/player. Invalid, stale or dead recipients cancel the transfer.
Equipped items must first be moved into the backpack.

The admin 7148, Legions 7160 and client 0930 decompiles show
`gm3DItemsUI::FinishFind3DTarget` using a null IID for ground and the picked IID for
a recipient. `FinishItemDrop` passes target slot zero. The installed DAT's
`InvMoveDesc::3F` preserves unsigned IDs and ordinals; its
`CInventorySystem::DirectiveMoveItem` branches assign move type 33 for
creature-to-world and 36 for creature-to-creature. Icefire requests now follow
those descriptor types and destinations. AC2E's existing bounded ground-drop
protocol is preserved; NPC giving on AC2E is not implemented by this pass.

Confirmed whole-item outgoing replies release local ownership once. Rejections
leave the item intact; split replies leave the original stack for authoritative
quantity updates. There is no optimistic inventory deletion.

Validation: 80 bridge tests and five capture tests passed; four independent
constructor/setter oracle checks passed (give/drop on each DAT); all seven
`AC2.Network` tests passed with stock and patched Icefire data, including actual
Slate inventory dragging and transfer lifecycle cases. Client, editor and release
bridge builds succeeded. Ignored logs/oracles are in `Saved/IcefireInteraction/`.

Limits: the UI transfers whole stacks and does not yet show retail's quantity
picker. The oracle executes the descriptor constructor and move-type setter, not
the full stateful native UI transaction. No live Icefire connection was made;
server acceptance, NPC rewards/dialogue and previously reported corpse error GUIDs
remain unverified by this pass.

## Dropped-item visibility and examination traffic (2026-11-13)

Inventory objects retain their visual description while contained, but a native
world-position notice did not recreate that model. The client now restores it
at the server's location when the item becomes a world object. Position before
inventory removal, removal before position, and full world creates overriding
stale bag ownership are covered. Removal alone does not invent a placement.
Object incarnation checks and wrapping position stamps prevent delayed movement
from resurrecting an item after pickup; container recreation clears its old
world placement.

The recent Icefire log contains server error
`0cc7fea5-036e-47d3-9dd7-51587c024832`, preceded by twelve consecutive examination
requests for one object at approximately half-second intervals. The client had
polled every visible examination panel even when nothing changed. It now
refreshes on received quality, death, visual and inventory changes, retaining
one outstanding request and coalescing changes that arrive during it. Explicit
examination still works; closed or removed targets do not poll. The server error
remains visible and is not treated as a successful operation.

Validation: the client and editor builds succeeded. All seven `AC2.Network`
tests and `AC2.Data.LiveExamination` passed on stock and patched Icefire data.
Drop tests instantiate actual DAT geometry and verify position, both packet
orders, pickup, duplicate/stale packets, instance zero and stamp wraparound.
Examination tests cover changing health/vigor, equipment, corpse transitions,
pending replies, closed panels and timeouts. Reports are under the ignored
`Saved/IcefireDropReview/` directory. Stock fixtures reported missing resource
`2A0008BE`, and the synthetic equipment fixture reported `00000000`; neither
caused a failed assertion.

Limits: no live Icefire connection was made. The GUID correlation does not prove
that examination caused the server exception, nor that every Drudge death or
loot transaction is fixed. Icefire's exception trace for that GUID is still
needed to determine the underlying server failure.
