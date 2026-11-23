# Development notes

Historical implementation and validation notes. For the current download, requirements and setup, see [the project README](README.md).


AC2E combines an Unreal Engine 5.8 client for Asheron's Call 2 with a persistent
Rust server derived from Pyrealium. The client is in `Unreal/`, and the server is
in `AC2E/`. This is an experimental implementation, with offline exploration and
native multiplayer; it is not yet complete retail emulation.

The [DAT compatibility review](Docs/DAT_COMPATIBILITY_REVIEW.md) records an offline
scan of both stock and patched Icefire data: all 568,643 archive records are
readable, and all texture surfaces and terrain records tested decode. It also
tracks the remaining server skill, quest, loot and race gaps. The eight previously
rejected animation graphs now load and execute their native random-idle, look and
ground-angle controllers, including scaled characters and replicated look updates.
Scenery and model startup sounds now play with their authored gates and volume;
looping FX volume ramps update without restarting playback, and music respects its
native channel. Compound skill ratings and periodic vitals retain independent
timers; native stat operations and caster/target restoration are preserved.
Active status FX survive scene snapshots, and particle lighting, scalar ramps
and emitter lifetimes follow the recovered native rules. Blood decals resolve
from each DAT profile's FX table. Both strict DAT decoder audits pass, as do
101 stock data tests, six network-presentation tests on each DAT set, and 358
server tests. The Icefire data suite still has 16 failing test groups requiring
profile-aware verification; some pin stock resources, while others test routes
and behavior that remain unresolved.
These results do not establish complete gameplay compatibility;
specialized skills, remaining quest flows, loot formulas and expansion races are
still incomplete. The review records those limits and reproducible checks.

The latest comparison against the admin 7148, Legions 7160 and client 0930
decompiles restores private player FX and all ten initial quality tables,
including shared skill-recovery timestamps. Animation release and stop-layer
messages preserve their original action identity and reject stale incarnations.
Sun/moon lighting now resolves celestial graphics within each DAT instead of
assuming the same sky-object keys across installations. AC2E's GUN adapter
retains pending presence updates while busy. Rebuilt client, bridge and server
targets and isolated loot/social scenarios pass. This pass used offline Icefire
data; its previously reported live server-error GUIDs remain unverified.

Ground drops now restore the cached item model when the server supplies its world
position, including when that position precedes the inventory removal. Stale
position updates cannot resurrect picked-up items. Examination refreshes after
received state changes instead of polling unchanged objects twice a second.
All eight targeted inventory/network/examination checks pass on both DAT sets.
The recent Icefire Drudge error GUID still needs a server exception trace to
establish its cause; these fixes were verified offline.

The development training mode has been removed. `serve --game-data PATH`
starts normal gameplay; `serve-training` and `--training-kit` are no longer accepted.
Login grants only the racial starter equipment and basic skills, without the
extra supply pack, training-only attack grant, or automatic practice-quest enrollment.
Shelter Drudges give needed quest tools/glyphs and otherwise use normal corpse
loot, rather than automatic equipment/potion gifts. Crafting, vendors, travel,
and optional NPC quests remain available. Historical training grant records
still replay so existing characters retain their earned and already-granted items.

The sign-in screen now saves server profiles and accounts per server. Choose
**AC2E** for the account-service connection or **Icefire** for direct GLS login,
and assign each server its own DAT folder. Remembered passwords are stored in
Windows Credential Manager; the profile JSON contains only connection settings
and usernames. Switching DAT sets reloads the frontend and all asset caches.
See [connection setup](Network/README.md#saved-servers-and-accounts) for details.
The supplied Icefire Dropbox archives load separately from the original AC2E
installation. Icefire profiles download missing revisions from the game server
on first connect, verify a new snapshot under `Unreal/Saved/ServerData/`, then
reload and reconnect automatically. The saved server profile selects that
snapshot for later logins; the original AC2E and downloaded archives stay intact.
Live verification installed portal revisions 138–139 and English revisions 10–11.
Icefire world entry now distinguishes ordinary position echoes from teleport and
force-position updates, preventing repeated portal transitions and restarted portal
audio. Initial inventory and crafting profiles decode through a reviewed patch
applied by `Build-AC2Network.ps1` to the pinned protocol dependency. Examination
requests now use the selected client build: sending the admin layout to Icefire
was producing GUID-style server errors. The client reads Legions examination
replies, handles server-directed examination and item-use feedback, and accepts
Icefire's extra quest word without losing the login inventory. Combat stance now
sends the required native target argument. Native equipment completion and
parent/contain events preserve bag items and their weapon attachments. Live checks
covered world entry, self/stone examination, training-stone use, stance changes,
weapon switching/restoration and attacks against a training creature without
server errors; full Icefire gameplay compatibility remains unverified.

The latest offline Icefire pass preserves confirmed loot destination slots and
cached item metadata, handles transfer replies arriving before item descriptions,
and prevents repeated equip replies from shifting unrelated bag cells. Combat
entry includes the selected target and the UI follows server weapon-mode changes.
Loot-all and container conversion resolve their descriptor classes from the selected
DAT: Icefire's IDs differ from stock Legions. Both outgoing descriptors match
offline execution of their installed constructors; replies retain partial results.
Confirmed requested pickups play the native reach when no server pickup behavior
arrived. Late physics updates cannot recreate deleted enemies, and old-instance
health/appearance updates cannot overwrite their replacements. DAT audio now feeds
Unreal's output device, including Windows device changes, instead of retaining a
separate stream on the previous device. These changes have offline protocol and
Unreal regression coverage; they have not been retested against live Icefire, and
the reported server GUID exceptions are not all explained by the available logs.
The animation-continuity regression uses a stock monster visual (`1f0004e0`)
that resolves to a static model in Icefire's DAT. That stock test passes with
Legions; the dedicated Tumerok pickup/audio integration passes with Icefire.

Outgoing Icefire private tells now echo locally after submission; AC2E keeps its
existing server echo. Incoming speech/tells retain their native message types,
quoted wording and DAT chat colors instead of sharing one generic color.
Server StringInfo text now renders through the selected DAT's numeric, nested
string and grammar rules, restoring missing values in examination and dialogue.
The unused examination tab strip no longer crosses the item's icon/name row.
Corpse visual refreshes preserve the death pose and reject late living-idle modes.
Network missile activation restores a detached world visual and uses the native
absolute target position, retaining the last destination if a target disappears.
Focused stock/Icefire tests cover these paths, including a rendered examination
panel. They are offline checks: live looting GUID errors and story-created missile
clones still require investigation; full Icefire gameplay is not yet verified.

Icefire inventory drags now distinguish giving an item to an NPC/player from
dropping it on empty ground. Give/drop requests preserve the native descriptor
types, recipient and move direction; inventory changes wait for server confirmation.
Invalid recipients never turn a refused gift into an accidental ground drop.
These actions currently transfer the whole unequipped stack. The descriptor
checks and seven network tests pass with both stock and patched Icefire DATs;
live Icefire acceptance remains unverified. AC2E keeps its existing drop format.

Icefire's standalone DAT window roots now open and move correctly, including
inventory, skills, examination, quests, social, options and the map. The compass
uses artwork from the selected DAT layout. Idle position updates no longer
cancel native item-use movement; real movement still interrupts it. A bounded
background output queue keeps the network bridge responsive during asset loads.
Live chat checks exercised Enter submission, a private self-tell and Icefire's
`.help` command. Private messages use `/tell Character Name, message`.
`/cg message` now sends to the server-assigned General room using native Turbine
chat on game queue 4; incoming room messages show their channel and sender.
Icefire's live room assignment was verified without posting public test messages;
isolated protocol round trips and UI Enter-submission tests cover sending and receiving.
AC2E also supplies General chat on that native transport. `/f` (or `/g`) uses
the server-assigned fellowship room; `/a` (or `/gu`) uses the allegiance room.
Membership is checked by the server on each send and delivery. Leaving or
splitting a group revokes its channel. Local `/say`, `/me`, direct tells and
sender acknowledgements remain available. Three isolated clients exercise
delivery, exclusion of unrelated players, and channel revocation.
AC2E delivers GUN friend-list requests and presence events over the authenticated
game connection.

The November 13 window pass repairs the hotbar's paint order, crafting's
overlapping profession/footer controls, and content anchors when resizing.
Restored window sizes fit the current viewport before child controls are laid
out, so training and crafting buttons cannot remain below the screen after a
resolution change. Inventory and other panels stay open independently. Native
frame edges expose resize cursors. Both installed DAT versions pass the same
window, crafting, live-examination and missile-presentation regressions.
Automatic item examination also keeps the inventory in front while choosing or
equipping an item; explicitly opening Examine still raises that panel.

The fellowship loot menu now selects personal, shared, rotating, rotating-group
or random allocation before creation. AC2E enforces the allocation on corpse
items; quest items stay with their eligible recipient. Experience remains
individual. Allegiance supports native profiles, Swear/Accept/Decline/Break,
consent checks, cycle prevention and private chat. Patron links are persisted in
`allegiances.journal` under the world's exclusive lock; deleting a character
splits their links without silently reassigning vassals. Rank progression and
allegiance XP pass-up are not implemented. On AC2E, the Friends tab lists the
account's saved friends and updates their presence as they enter and leave.

Missile initialization can arrive before its object creation. The client now
retains bounded, instance-checked pending activations and starts the detached
visual when creation arrives. Tests cover that ordering, expiration, movement
and corpse pose retention. This does not establish that every live Icefire
story-created projectile works, or resolve the GUID exceptions from its server.
An isolated connected gameplay run also verifies visible moving missiles,
corpse looting, weapon swaps, inventory scrolling and pickup/drop animations.

The latest presentation pass restores native water texture stages and their
independent motion on consistent 40 m tiles, with matching near/distant surfaces.
Water retains native vertex lighting and composites the shore mask, water and
reflection in display RGB. The DAT shoreline mask controls transmission, and
the two scrolling foam samples use the retail signed-add/multiply alpha stages
and separate white diffuse lighting. Shore widths follow the intersecting
terrain normals. Foam coordinates retain precision far from the world origin.
Submerged terrain is sampled from the opaque scene before water is drawn;
it remains visible through the surface rather than being replaced by reflected
scenery. GPU checks use a two-color riverbed to verify transmission at the edge,
inside the shoreline band and in open water, with and without the sky pass.
The nearest visible lake elevation reflects sky, terrain and scenery from a
mirrored camera. The retail signed bump map distorts that reflection while the
riverbed shows through. Joined shoreline ribbons preserve contour distances,
normals and 12 m slope-scaled widths. Local phase variation breaks up wave
crests along the bank and stays continuous across streamed block boundaries.
Shore contours are now resolved across the DAT independently of the streaming
window. The ribbon and open-water sheet partition the surface, so coplanar
SceneColor composites cannot erase each other's foam. A separate, back-facing
surface uses the native below-water material and texture; camera depth selects
and interpolates the DAT water-lighting keys. Entering water with the camera
therefore changes the lighting even while the character is still above it.
See [the water review](Docs/RETAIL_VIDEO_COMPARISON.md#water-shoreline-and-submerged-camera-review-2026-11-13)
for the recovered render passes, regression checks and remaining differences.
The combined near/distant surface now advances its DAT ripple waveforms on the
world clock; rendered checks cover animation on both sides of a block transition.
Grass and ground-cover shrubs bend around the player and up to seven nearby
creatures, other players or moving world objects. Bend weights use each plant's
height, keep roots fixed and cap tip displacement at 75 cm. The contact area is
wider so passing through small shrubs produces a clearer response. A short movement
trail lets plants recover smoothly; body height keeps entities on another floor
from bending them. A shared shader parameter collection updates all batches
without per-material position uploads. This cosmetic addition preserves authored
placement, colors and texture coordinates; materials that need the spare
coordinate channel retain their original behavior.
Only nearby bending foliage invalidates cached shadows; distant plants retain
their cache. GPU tests cover local/remote contacts, recovery, swept movement,
vertical separation and water blending. The offline `-AC2EnvironmentSmoke
-AC2FoliageContactSmoke -AC2Block=77B4` capture checks actual outdoor plant
batches through the normal player update path.
Scenery without a physics mesh now uses its DAT setup cylinders or spheres,
matching retail's collision fallback. The reported tree in block 8FBC blocks
pawn movement while the space beside its trunk remains clear; foliage cards
do not acquire broad bounding-box collision.
The `/source` command displays the AC2E credits, license and source download.
Overhead labels use the DAT font with pixel-aligned glyphs and authoritative
object colors, including corpse changes. Chat and inventory can reach the actual
bottom edge; window titles reserve space for native close/help buttons.

Examination profiles preserve each native text span, appearance and layered icon.
Item names sit beside their icons; bold values, colored names/effect headings,
16-pixel indents and explicit section spacing follow the retail layout. Native
font colors are converted from display RGB before Slate rendering, avoiding
washed-out cream, blue and orange. AC2E also supplies separate bold numeric
values. Bare EffectID nodes still require the retail script-driven subprofile
expansion; only effect details already supplied as profile nodes are displayed.

Quest updates now trigger their native receipt, advance, completion and failure
effects while suppressing login snapshots and duplicate updates. The completion
effect's SoundInfo/DBWave is absent from both installed DAT profiles, so that cue
still needs its original assets. Native loot/conversion button sounds fire on
press even between frames; single-item loot plays the loot cue after successful
acknowledgement, including ground-item acquisition notices. Ambient music can
resume after leaving and re-entering a region,
and an empty environment lets existing audio fade instead of abruptly stopping.

Tumerok Shelter now retains native collision on animated bridges and towers.
The original route works in both directions; the previously reconstructed spiral
stair has been removed. The reported ramp supports actual walking at both
Tumerok height/build extremes.
Using the portal before obtaining its glyph explains the current missing quest
step. Rain Drum, Fury Drum and recovered javelins equip with their native holding
points and weapon stances. Held weapons keep their own scale when the character's
height/build changes; verified Tumerok throws use native release cues and hide
the held javelin during recovery. Local attachments update after movement and
animation, so running no longer leaves weapons a frame behind the hand.
Empty Drudge quest corpses explain that the tool or glyph is already in the
backpack and identify the next step. Existing characters and quest progress are retained.

World surfaces now receive shadowed sunlight and native dungeon point lights.
DAT textures, colors and material finishes remain in use; native ambient light
fills shadowed areas, and dungeon surfaces are isolated from outdoor sunlight.
Dungeon lights keep their shadows as the camera moves; complete lights fade at
their draw limit instead of switching shadow ownership between the nearest eight.
World material conversion separates native diffuse-light amplification from the
texture, preventing MODULATE2X/4X stages from clipping albedo before lighting.
UI previews retain their separate native lighting path.
Outdoor shadows now follow the visible DAT sun or moon, retaining native light
colors and intensity. The default renderer uses DX12/SM6 virtual shadow maps:
legacy cascaded shadows disappeared at Dereth's native 20–40 km coordinates,
leaving only short screen-space contact shadows. Animated procedural meshes
invalidate their shadow pages as their vertices move. Authored FX lights illuminate nearby surfaces with their
native range, intensity waves and lifetime fades. Lightning Bolt's cast, release
and impact cues are verified against the DAT, including its animated impact mesh.
Stationary native-server NPCs retain their ground idle when a short floor probe
misses streamed collision; the Lost Wish Explorer is included in that check.

Local-server travel now advances the native teleport counter, ensuring glyphs,
portals and recalls load the destination under portal space. Rejected cooldown
attempts do not extend the five-second delay. The Tumerok Menhir gateway is now
centered on its DAT pedestal in cell `03FD0130`, matching the reported location.

Options → Game now includes **Auto Target**, enabled by default as in retail.
With nothing selected, an incoming harmful skill selects its attacker, including
on a miss. Existing selections, including corpses being looted, stay selected.
The setting uses Apply/Revert/Defaults and saves per character through the native
options profile. Healing and falling damage do not select an enemy, and selection
alone does not switch stance or start attacking.
If the selected object disappears or becomes unselectable, Auto Target now
chooses a nearby living monster using retail's distance ordering and radar
range. A dead creature stays selected for looting until it is removed. Targeted
attack cancellation preserves combat stance; changing selection only continues
attacks when an ordinary repeat attack was already running.

Options → Game now includes retail's **Shortcut Bar Height** dropdown, with
the native menu artwork and one-to-five-row choices. Selecting a height edits
a draft; Apply resizes the bar and saves it for the character. Revert restores
the applied height, and confirmed Defaults returns to one row. Dragging the
bar's grip updates the selector immediately without discarding other option
edits or changing its page and bindings.

Character options now save through retail's native game-options message and
restore from the server on login: damage numbers, third-party damage and range,
radar filters and coordinates, shortcut locking, number labels, instant scrolling,
hotbar height and auto targeting. Apply/Revert/Defaults retain their existing
behavior; radar and shortcut-number commands apply immediately. Unexposed native preferences
are preserved. Characters without saved options start from local defaults.

Returning to character selection now waits for reliable packet capacity instead
of losing logout replies during a busy scene load. World exit is acknowledged
before the fresh character list is sent, and selection controls stay closed
until that list arrives. Leaving also cancels unfinished item actions and combat.

Hotbar bindings now save to the server through the native shortcut protocol and
restore on login. Skills, item instances, crafting recipes and text/alias buttons
retain their slots across reconnects and server restarts. Clearing every slot
stays cleared. Existing local bars import once, with their original settings
retained as a backup; later logins use the server profile. The local cache is
scoped by server, account and character. Named alias definitions, the selected
page and display preferences outside the supported native options remain local.

Named aliases now support retail's `/alias` add, remove, list, clear/clearlist,
and shortcut commands. For example, `/alias ;ready /say Ready at ;loc!` defines
an alias and `/alias shortcut ;ready 9` puts it on the current hotbar. Definitions
save per character; editing an alias updates its existing shortcuts. Names are
case-insensitive, nested aliases expand in chat, and protected `;target` and
`;loc` resolve the current selection and location at activation time. Cycles
stop at the unresolved token. Expansion is bounded to prevent runaway chains.
Use `/help alias` for the original DAT command reference.

**See Numbers on Shortcuts** now works in Options > Game and through
`/shortcut numbers on` or `/shortcut numbers off`. It controls the native small
empty-slot labels and page number, including expanded and scrolling bars.
The large first-row digits belong to the original frame artwork and remain.
Commands apply immediately; checkbox edits use Apply, Revert and Defaults.
Both routes share the saved character option without changing hotbar bindings.

Text shortcuts now use retail's native icon and single-line label. Enter
`/shortcut text 9 /say Ready!` to bind a message or command to slot 9 of the
currently visible hotbar. Clicking or pressing its key follows normal chat
submission; Shift-drag moves or removes it under the existing shortcut lock.
Bindings persist per character, including punctuation and Unicode. Named alias
definitions remain local; their hotbar references now save to the server.

Accepted animated healing and support spells now enter the equipped weapon's
combat stance before their native cast story plays, including when cast from
peace mode. Both the caster and nearby players see that ordering. Rejected
casts leave stance unchanged; support casting preserves the selected target
and does not start auto-attacking it.

Hotbar skills now move from retail's requested-attack marker to the native
active highlight when their server story starts playing. The highlight follows
that exact animation instance and clears on completion, replacement or cancellation;
it does not use the cooldown as an animation timer. Duplicate bindings and other
pages share the state, and a rejected repeat cannot clear an earlier active cast.
Pending and active shortcuts cannot be dragged, even with Shift or shortcut
locking disabled. Server toggle notices drive the native toggled artwork.

Hotbar skills now display timed cooldowns with retail's moving crosshatched
meter. Accepted casts carry native skill and shared recovery timestamps from
the server; the client uses synchronized time and the DAT reset duration.
Duplicate bindings and other pages show the same countdown, and skill-list
updates or reconnects retain it. Rejected casts do not restart it. Equipment
shading takes priority while the underlying timer continues to expire.

Learned hotbar skills now use retail's native equipment shade when the current
weapon or off-hand cannot satisfy their DAT requirements. Skill-book tooltips,
hotbar tooltips and skill examination show the required implements and update
when equipment changes. Pending inventory data stays unverified instead of
being mistaken for an empty hand. Binding, training and server casting rules
remain independent of this feedback.

Options → Game now includes **Instantly Swap Shortcut Bars**, enabled by default
as in retail. Turning it off and applying the change scrolls hotbar contents
within the fixed frame over the DAT's quarter-second transition. Partially
visible rows keep clipped hit targets: clicking, examining and dragging use
the icon on screen, while keyboard activation and the page label follow the
top visible row. Reversing a transition continues from its current position;
turning instant scrolling back on settles it immediately.

Hotbar editing now follows the retail shortcut lock: **Lock Shortcuts** defaults
on in Options → Game. Hold **Shift** while starting a drag to move or remove an
existing shortcut, or turn the lock off and Apply. Clicking to activate and
right-clicking to examine still work while locked. New skill, item and recipe
assignments remain allowed. Double-clicking a learned active skill in the book
adds it to the first empty slot on the current hotbar page; a full page keeps
its bindings. Use the assigned hotbar shortcut to cast the skill.

Character Options now includes retail radar filters for monsters, NPCs, players,
portals, lifestones and items, plus the coordinate display. Apply/Revert/Defaults
control these settings, and the radar's coordinate button saves the same character
preference. Contacts use native category colors and pixel symbols instead of
uniform yellow squares. Classification follows installed script inheritance and
DAT overrides, including nonselectable and hidden blips. Filtering also removes
radar hit targets without clearing the selected world object. Items are hidden
by default, as in retail; faction and relative-level color variants remain work.

Options now use native row spacing and a working scrollbar: wheel, arrows and
thumb dragging stay synchronized through the full list. Apply and Revert enable
only when the current tab has changes, and applying one tab preserves edits on
other tabs. Native confirmation dialogs handle Revert, Defaults, Chat Apply and
closing with pending changes; their mouse and keyboard input cannot reach the
world behind them. Settings remain a supported subset of retail's options.

The hotbar now expands to one through five rows by dragging its top edge. It
keeps the native slot size and bottom position, supports skill/item/recipe
shortcuts on every visible row, and preserves all 100 bindings when collapsed.
Page arrows advance one row at a time; extra rows after page ten are disabled
padding, as in retail. The installed retail keymaps now supply hotbar controls:
**1–0** activates the first row, **Shift+1–0** activates the second visible row,
**Ctrl+1–0** selects pages, and **minus / Shift+minus** moves to the next / previous
page. Hidden rows and end padding cannot activate; typing in chat or fellowship
name entry cannot cast or change pages. Row count and page are saved locally per
character. See the
[native UI audit](Docs/NATIVE_GAMEPLAY_PASS.md) for evidence and remaining gaps.

Spawn clearance now uses each creature's native body radius, height and scale.
The placement pass corrects 204 outdoor anchors with verified floor support and
body separation, preserving their IDs and spawner slots. Roaming/pursuit checks
the full footprint and reserves space between living creatures. Client ground
correction also handles creatures without humanoid feet meshes and clears stale
floor offsets. Respawns wait when another living creature, ready player or known
grounded NPC occupies their anchor, including children scheduled together; they
retain their original spawner and home. Player clearance follows the native race,
sex, build and height. NPC additions, replacements and removals update these checks
through the live editor. This extends spawn admission; player collision and crowd
navigation remain separate work.
Movement packets use the simulation sample time, keeping native creature speeds
consistent even when collision checks take longer on one tick.
The follow-up review fixes 22 of the 26 flagged anchors using native collision,
body dimensions and connected floor checks, plus four shallow penetrations
exposed by a corrected contact-ray check. The remaining four Energy Flame Constructs
in `4DA2` are now verified at their original captured positions: native two-sphere
collision fits their platforms, while the wider flat-footprint check falsely
rejected them. Each stays supported for ten seconds of native physics, and tests
reject open-air, buried and missing-platform controls. All 26 flagged anchors are
reviewed (22 corrections, four verified originals);
see [placement clearance](AC2E/emulator/content/README.md#placement-clearance).

World entry now sends nearby objects, equipment, saved UI and crafting data before
the client's scene-ready acknowledgement.
Loading players receive live scene updates but remain hidden and protected from
combat until the client's final portal hold ends. Activation preserves already
loaded models instead of sending them a second time. A delayed-acknowledgement UDP test verifies both protection
during loading and native aggression after entry.

Named spells now send the DAT's separate caster and release effects and launch
their native missile when defined. Healing and buffs show their application
effect on the recipient, and periodic damage no longer replays an unrelated
magic impact. Flame Bolt's fire cast and mesh missile, Fireball's particle trail,
and healing cues are covered by client particle and multiplayer tests. Some
legacy impact IDs have no asset binding in the installed DAT; see the
[spell presentation audit](AC2E/emulator/content/README.md#native-spell-presentation).

Human, Tumerok and Lugian now have separate Shelter quest routes through their
native dungeons and Menhir vaults to the overworld. Captured travel anchors,
reconstructed NPC/mob placements, native room collision and a documented Tumerok
descent repair are covered by geometry and network tests. The Lugian dungeon
start replaces the old outdoor fallback. Restart the server and client for these
changes; see [the starter audit](Docs/NATIVE_GAMEPLAY_PASS.md#racial-shelter-routes-and-dungeon-traversal--2026-11-02)
for evidence, route details and remaining limitations.

The Tumerok backtracking passage now uses the walking capsule's valid floor
contact in the movement safety check. Narrow native floor seams no longer cause
the client to repeatedly reset the player as if blocked by an invisible wall.
An actual movement regression covers the passage in both directions for both
sexes and character-size extremes, including rejection of stale floor contacts.

Multipart character rendering now preserves the native attachment matrices
before height/build scaling, closing the Tumerok tail-root gap. Regression checks
cover both sexes, size extremes, movement, combat and item-handling animations.

Academy practice Idols now remain passive until attacked and use level-1 loot
instead of the shared outdoor template's level-24 tier. Generic treasure filters
also exclude fixed equipment above the encounter's stat budget. Static scenery
loads the DAT's separate physics polygons, restoring the Tumerok academy's spiral
stair collision. Narrow, documented placement repairs align the reported brazier
effects and ground two mushroom clusters in that academy; see
[`Docs/NATIVE_GAMEPLAY_PASS.md`](Docs/NATIVE_GAMEPLAY_PASS.md).

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

Character selection uses the retail blue-on-gray selected row and DAT font,
with full-row clicks and picking against the visible carousel models. A native
tooltip identifies the character under the pointer without changing selection;
double-click selects that character before entering. Long names fit the list,
and delete confirmation blocks selection changes behind it.
Character creation also picks the nearest animated model under the pointer,
so overlapping hit rectangles no longer select a Tumerok when hovering the
Empyrean behind her. Native hover and selection animations are preserved.

The starter Warder route now starts and completes **Find a Warder in Cragstone**,
**Find the Warder Corporal**, and **Find the Warder Scout** through actual NPC
conversations, with saved progress, deadlines and repeat restrictions. The quest
journal uses native expandable rows, localized details, intended levels, timers,
read indicators and a working scrollbar. Chat border resizing reflows the text
and scrollbar and saves its size within native limits. Other fixed-size panels
retain their authored dimensions. At level 10, the Warder Scout near Molwirth
now starts **The Warder Scout's Notebook**: defeat the Drudge Slinker Bandit,
take the notebook and return it for 800 XP and three native reward items. This
starts **Seek the Warder Captain**, worth 2,500 XP on arrival. Both tasks retain
their deadlines, journal stages and rewards across restarts. Potion Raid and the
subsequent Dantry referral remain unfinished; NPC information pages may describe
other unavailable tasks. See [quest and UI scope](Docs/NATIVE_GAMEPLAY_PASS.md).

The flag opens the native fellowship panel: create a named group, recruit a
selected player, accept or decline invitations, view member health/vigor, dismiss
members, leave, or disband. Groups support eight players for the current session;
XP remains personal and the creator chooses one of five loot allocation methods.
The Allegiance tab supports membership and private chat. On AC2E, open Social →
Friends to add a selected player, remove a friend or change your availability.
Characters on the same account share one friends list.
Inventory, skills, quests, fellowship, examination and options now remain open
independently. Click a window to bring it forward, drag its title to move it, or
drag an edge/corner to resize it. Positions and sizes persist locally; scrollbars,
inventory columns, wrapped text and header controls follow the resized window.
Covered controls no longer consume clicks through another window.

Crafting reads the connected server's recipe, profession, ingredient, cost and
difficulty data from the selected DAT, including Icefire's different identities.
The ingredient window stays open beside inventory, supports dragging materials
into individual slots, and waits for native server validation before enabling
Craft. Auto-fill handles explicit material lists; specialized ingredient predicates
remain server validated. The native level spinner updates recipe costs and required
amounts. Native output actions supply known result previews. Local AC2E crafting,
profession advancement, gathering and persistence are covered by isolated UDP tests;
Icefire's DAT layouts and recipe metadata are checked offline, with live crafting
still requiring verification against that server.

Skill selection opens detailed inspection, and skill-tree/crafting scrollbars
now move with their contents. Training, raising, untraining, cancellation, active
skill use and hotbar assignment retain server eligibility checks. Item inspection
no longer stops at twelve lines: it includes known instance stats, requirements,
descriptions, gathering/crafting information and implemented quest-item uses,
with a working draggable scrollbar. Unrecovered properties are not invented.

Shared melee/missile/magic masteries now train and advance through the skill UI.
Disabled controls explain their level, credit, XP, prerequisite, incompatibility
or untraining restriction; maximum rank and passive skills are labeled correctly.
Clicking a nearby fellowship member selects them for helpful skills, including
Ease Suffering, which previously always healed the caster. Selecting yourself or
leaving scenery selected still permits self-healing. Cast range, visibility,
equipment and effect support remain server-authoritative.

NPCs retain their native grounded idle while collision is unavailable, rather
than selecting an empty airborne pose. Player stance and ground/air transitions
preserve animation phase, and the first grounded physics step after a jump uses
ground movement limits. Remote movement interpolates over the packet interval
instead of repeatedly easing to a stop; creature size is included in gait speed.
The offline mob sandbox now uses each body's native speed and acceleration, as
the server already does.

Ground-item pickup/drop and corpse-item pickup now broadcast the native reach
animation to the player and observers before transferring ownership. Movement,
death or travel interrupts a pending reach; inventory/range checks run again at
contact. Combat looting temporarily enters peace and restores the prior stance.
Action duration follows the body clip, including separate Lugian/Tumerok timing.
The contact point is currently authored at mid-clip, not a recovered retail
inventory hook. See [animation verification](Docs/NATIVE_GAMEPLAY_PASS.md).

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
  A second wilderness pack adds **2,553 creatures in 1,555 managed groups**,
  including all five Knorr regions and Arramora, plus gaps in those original
  21 regions. It reaches 1,167 more outdoor landblocks and introduces 180
  previously unplaced native types. All 563 eligible types matched from the
  wiki regional rosters now have at least one placement in the combined world.
  This does not cover every dungeon, rare encounter or template variant; the
  [coverage audit](AC2E/emulator/content/wilderness-coverage-v1.json) accounts
  for the remaining native records and withheld sources.
- **Quests and services:** readable signs send native descriptions. Supported
  doors, containers, merchants, lifestones, portals and tutorial actions have
  server handlers. The inferred pack adds 88 quest profiles, 143 camp groups and
  702 accepted placements, including 26 noncombat NPCs. These include counted
  hunts, boss hunts, three hunt elixirs and Gambling Debt; placing an NPC alone
  does not implement its dialogue, vendor stock or other quests.
  The wiki NPC pass adds **217 native conversation profiles** with **326 pages**
  covering quest requirements, starting areas, timers and listed rewards, plus
  **27 missing NPC placements** checked against native ground/collision. Double-click
  an otherwise unsupported NPC to speak; select an NPC and type `@talk` to browse
  information without invoking its working quest or shop action. These are authored
  replies, not recovered retail dialogue or 229 newly playable quests. The
  [NPC coverage audit](AC2E/emulator/content/npc-dialogue-v1.audit.json) accounts for
  all 89 wiki category entries and additional town/quest NPCs, including 15 names
  without matching native identities and unresolved locations or actor variants.
  Four **Nature's Ally material exchanges** now work at existing NPC placements:
  Rabbit Meat → Ancient Monouga Bone (Feral Pet Lasher), Scented Blossom → Wasp Egg
  Cluster (Outcast Wasp Queen), eggs → Succulent Truffle (Digger 'Dillo Mama), and
  truffle → eight Anthracite Gems (Grutt Gourmand). Double-click the NPC for
  guidance, then select it and double-click the input in your backpack while
  within four metres and out of combat. Trades preserve partial stacks and persist
  atomically; existing singleton loot remains accepted. The **Black Mother Earth
  blossom path** now feeds those trades: collect eight Withered Blossoms from
  existing Flowering Bushes, offer one at the shrine to learn Flower Garland,
  craft with seven blossoms and the native 100-gold cost, then offer the garland
  for Scented Blossom. Recipe knowledge and regrowth survive restarts. The four
  blossoms per harvest and five-minute per-player regrowth are reconstruction
  choices. Other Nature's Ally branches, dungeon access and final rewards remain unfinished.
  Single-item corpse looting now accepts every valid source slot, fixing quest
  rewards and other items that could not be picked up after the first loot cell.
  Arwic, Lost Wish and Esper Explorers now offer their **Obelisk Search** quests:
  visit each region's ruby, sapphire and emerald obelisks in order, then return
  for 80 XP and the **Find the Arwic Mines Overseer** referral. Nine obelisks
  are reconstructed near the documented ringway stops on verified native ground.
  Progress, deadlines, repeat cooldowns and rewards persist across restarts.
  The Overseer's **Arwic Mines and Crafting Quest** now continues that referral:
  recover a flat hammer from a mine Outcast, receive Iron and a learned sword
  recipe, then return the crafted sword for 150 XP, a glyph and a readable guide.
  Four Outcasts and two Edge Beetles use validated mine-room spawners and native
  combat/AI fields. Their room assignments and guaranteed eligible hammer drop
  are reconstructed. Temporary obelisk buffs remain unfinished.
  The starter route now continues through **Kehan Windmill Quest**: speak again
  to the Overseer for the Miller referral, use the Miller's native map to find
  a Stone Quarry, harvest with a crafted Rock Hammer, and explicitly give one
  Stone to the Miller. Take her Massive Gear to the Windmill north of Kehan,
  then return for 200 XP, 150 quest points, a Lost Wish Glyph, three hours of
  Renewed Focus (+40 maximum vigor) and the Esper Warder referral.
  The map, quarry charges, quest phases, 24-hour deadline and 20-hour repeat
  timer survive restarts. NPC placements are reused; the missing windmill gear
  receiver has one documented, ground-checked placement. The repaired
  windmill animation remains unfinished.
  The Esper Warder now offers **Drudge Outcast Camp Quest**: gather Bone from
  vermin, craft a native Cadre Slayer weapon, defeat a Cadre at the captured
  island camp, and return its quest bone for **320 XP and an Esper Glyph**.
  The three camp placements respawn; Slayer triples direct weapon hits on
  Cadres. Quest timers, recipe access and rewards survive restarts. Sound Heart
  and the separate vault progression remain unfinished.
  See the [starter route instructions](AC2E/emulator/content/README.md#kehan-windmill-and-the-esper-referral).
  Thirty-five NPC hunting quests now support acceptance, counted kills,
  return-to-NPC rewards, prerequisites, deadlines and repeat timers. These
  include thirteen Ikeras hunts, fifteen Arramora hunts offered in Linvak Tukal,
  and seven Foundry hunts near Jethric's Outpost. Twenty-three native NPCs
  provide conversations; four missing speakers have inferred, ground-verified
  town placements. Three guide-listed Arramora camps add six creatures in
  managed two-child groups. Wiki rewards
  and native identities are retained; missing retail quest IDs and dialogue
  scripts are explicitly reconstructed. Quest changes send only modified entries,
  preventing large completed journals from filling the reliable network queue.
  See [hunting quest expansion](Docs/ADMIN_CONTENT_RECOVERY.md#hunting-quest-expansion-v2-2026-11-01).
- **Profession crafting:** Skills → Craft → All professions exposes 26 disciplines
  and 364 supported DAT-backed recipes: armor, weapons, tools, refining, dyeing, surveying,
  practice and the two starter potions. Recipe levels, ingredient choices and
  quantities come from the installed DAT; materials can span backpack stacks.
  Crafting XP, rank increases, costs, finished item stats and gathering progress
  are saved atomically. Survey maps locate dry native terrain; tools harvest
  deposits and eligible owned corpses. Equipped creation tools boost matching
  professions; item-bound tool durability persists through transfers and restarts.
  Supplier purchases and refinery outputs merge compatible material stacks.
  The six towns in the AC2Hero workshop guide each offer crafting, refining
  and spellbinding. This includes the captured Cavendo refinery. Arwic keeps
  its three supplemental stations.
  This is **not complete retail crafting**: enchantment extraction/application,
  general recipe discovery, exact critical/stat formulas and equipment wear
  remain open. Most recipe grants, some yields
  and output stat scaling are explicitly reconstructed. See
  [crafting coverage and controls](AC2E/emulator/content/README.md#profession-crafting-and-gathering).
- **Loot and equipment:** native trophy probabilities supplement observed corpse
  loot; 312 recovered template rules now bound generic treasure quantities.
  The inferred treasure policy adds variable supported equipment/restorative
  drops and persistent armor ratings/tints. Sixty-nine observed weapon profiles
  now participate with durable damage rolls, native minimum-level checks and
  damage used by basic attacks and supported weapon-based skills. Weapon roll
  scaling is reconstructed; exact retail mutations remain unrecovered.
  Pickup, equipment and rewards are
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

The [AC2Hero world atlas](Docs/Atlas/index.html) maps the prepared world on retail
terrain and connects creatures, items, NPCs, quests and dungeon floor plans to
their sources. Its [catalog guide](Docs/Atlas/README.md) explains placement
evidence and how to rebuild the map for a local world.

The world builder now supports creature stats, AI, weighted loot, patrol routes
and population groups in an overlay plus a `NAME.rules.tsv` sidecar. Its optional
loopback admin endpoint applies edits to a running server and saves them for
restart. Rejected edits preserve the current world; stale revisions stop sync
until the operator reconciles the files. Group overrides preserve the original
regional rosters, and live creatures use native movement descriptors. These are
operator-authored rules, not newly recovered retail data. See the
[world-builder guide](AC2E/emulator/docs/content-editor.md) for setup and limits.

The server recognizes the original 7230 DAT profile and selects its own skill,
advancement and effect tables before loading persisted characters. Those tables
are validated against the archived 7230 portal records, including Flame Bolt;
Legions keeps its separate tables. A matching native 7230 client's full gameplay
acceptance is still unverified. See the [7230 profile notes](AC2E/emulator/content/7230/README.md).

The AC2E Friends tab works through the regular game login. The server stores
account rosters in `gun-users.json` and pushes changes to connected clients.
The optional TCP/XML adapter shares those rosters and provides its own ignore
lists and tells. Operators can enable the adapter with
`--gun-adapter-bind 127.0.0.1:12310`.

The original client uses a separate DirectPlay 8 connection on UDP port 2310.
Stock `GunDll.dll` login and Icefire friend-list synchronization remain open.
The offline replay checks native GUN XML against archived retail messages.
[GUN protocol and compatibility details](AC2E/emulator/docs/gun-protocol.md)

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

The game starts fullscreen. Type `/fps` in chat to toggle the frame rate meter.
Drag the meter to position it. The client remembers its location and visibility on relog.

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
crash, the launcher can recover a stale lock when its recorded process is gone
or Windows has reused its PID for a process that started after the marker was
written. It preserves the old marker and never deletes characters or account records.
Game builds use `AC2E/target/game/release/ac2e.exe`, separately from the account
service executable. Stop the game server before rebuilding it; an existing account
service can stay running. Its executable is refreshed on a later build when stopped.
Use offline exploration or connect to an AC2E/Pyrealium server. See the guides for
account setup, hosting, supported gameplay, and remaining implementation limits.

The world starts with the DAT mouse pointer visible. **Left-click** selects a
world object; **double-click** uses it; **right-click** opens the native examination
panel with information supplied by the server. Hovering a selectable object keeps
the native selection pointer and shows its name. Right-clicking inventory items inspects them;
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
Routine target, stance and generic item-use success notices no longer fill chat;
NPC dialogue, quest messages and errors remain visible. Targeted item use reads
the selected DAT's `UseTargets` quality rather than a stock glyph ID. On Icefire,
select the Menhir portal and then use the Menhir Glyph from the backpack; simply
using the portal is not the glyph action. The server still validates quest state.
Native combat stories now launch transient missile visuals at animation release
hooks, including stories without a separately replicated projectile object.
These paths have offline coverage with both DAT sets; outstanding Icefire GUID
errors and missing NPC replies are tracked in [the compatibility review](Docs/DAT_COMPATIBILITY_REVIEW.md).
Running is the default; hold either **Shift** key to walk. **Mouse4** (the first
thumb/back button) toggles autorun. Press it again, press
forward/backward, or press Escape to stop. Chat, focus loss, death and travel also
cancel autorun. The radar compass and contacts share the DAT radar center and one
normalized character heading; stationary camera orbit does not turn the radar.

Drag panel headers to move the main windows; positions persist. Skills and quests
open from the toolbar. The skill book supports training, raising and untraining;
learned active skills can be dragged onto any of ten shortcut pages, selected with
the page arrows or Ctrl+1–0. Minus advances a page; Shift+minus goes back. Number
keys activate the first row, and Shift+1–0 activates the second row when visible.
Inventory items can also be dragged onto the hotbar: click or use
the slot's key to use/equip that exact item, and right-click an item or skill
shortcut to examine it. Moving a shortcut replaces the destination; dragging it
into the world removes the binding without dropping the item. Bindings persist
per character, and consumed, sold or dropped items clear their own shortcuts.
Crafting recipes can also be dragged from the Craft tab onto the hotbar. Their
shortcuts open the ingredient window; right-click a recipe or its shortcut to
inspect its profession, level, cost, ingredients and possible results. Reusing
an open recipe preserves its chosen level and cannot reset a pending craft.
The server executes recovered core effects for 235 racial skill profiles,
including Flame Bolt damage and burns,
Lightning Bolt vigor drain, healing, vigor restoration, regeneration and supported
combat-rating buffs. Live creature vigor updates reach observers and examination.
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
python .\Network\test_scene_loading.py
python .\Network\test_auth.py
python .\Network\test_gameplay.py
python .\Network\test_friends.py
python .\Network\test_armor.py
python .\Network\test_shelter.py
python .\Network\test_login_ui.py --gameplay
python .\Network\test_server_launcher.py
python .\Network\test_combat_ui.py --mouse
python .\Network\test_corpse_loot.py
python .\Network\test_outdoor_population.py
python .\Network\test_recovered_equipment.py
python .\Network\test_active_skills.py
python .\Network\test_live_world.py
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
