# AC2 native networking (0.11.0)

The Unreal client connects to AC2E and Pyrealium using native AC2 UDP, not Unreal replication. The separate Rust bridge uses AC2_Rusted's checksummed, reliable native transport and codecs. Private anonymous pipes carry JSON between Unreal and the bridge; no IPC network port is opened. Object IDs remain decimal strings across JSON to preserve all 64 bits.

Character logout retains a bounded pending reply when the reliable send window
is full. The server sends ordered `CharExitGame` (`0x10027`) when capacity becomes
available, waits for its acknowledgment and preceding traffic, then sends the
pre-world `CharacterSet` (`0x10055`). Existing reliability handles retransmission;
there is no transport/cipher reset or fixed sleep. The bridge keeps the UI in
`leaving` until the fresh list arrives. This is an implementation ordering guard
using recovered native messages, not a recovered retail server scheduling algorithm.
`test_shortcuts.py` now includes 12 immediate alternating-character return/re-entry
cycles without the former scene-settle delay.

The inventory receiver keeps `Contain` distinct from destruction, adopts cached
metadata when a transfer is acknowledged, and uses the server's actual insertion
slot. Repeated equip/unequip replies do not compact the bag again. Corpse contents
retain their item ordinals separately from the optional pack list; browsing nested
pack contents remains unfinished. Transfer requests preserve `InvMoveDesc::3F`'s
native tags, checked with an offline execution of Icefire's installed constructor.
Bulk loot/conversion read package identities from the active DAT's WLib instead
of using stock class IDs on Icefire (`20ef`/`234b` versus `1b58`/`1b59`). Only the
two reviewed layouts are accepted. The constructor oracle runs independently for
both DAT sets; completion preserves taken/rejected item lists and leaves bag
ordinals to authoritative contents updates. Unsupported schemas report an error.
Combat entry sends the current selection; server mode notices update the UI.
Unknown incremental physics/visual updates are ignored until CreateObject, and
old-instance health/visual notices cannot corrupt a respawned target.
`AC2.Network.InventoryOrderingAndLifecycle` covers those ordering and lifecycle
cases without connecting to any server. Live Icefire retesting remains pending.

Inventory-to-world dragging resolves the object under the release position.
On Icefire, dropping on a living NPC/player sends `give_item`; empty ground sends
`drop_item`. Invalid or stale recipients cancel instead of falling back to a drop.
Both use native `WM_Inventory::36` with `InvMoveDesc::3F`'s unsigned IDs and ordinal
tags. Ground transfers have target IID/slot zero and move type 33; gifts have the
recipient IID, target slot zero and move type 36, following
`CInventorySystem::DirectiveMoveItem`. AC2E retains its existing landblock/-1 drop
descriptor; giving on that profile reports unsupported rather than dropping.
Only unequipped items can be transferred, currently as whole stacks; the retail
quantity picker remains unfinished. Successful outgoing transfer replies remove
ownership once, while refused gifts and split-transfer replies preserve the original
item. `AC2.Network.InventoryGiveAndDrop` covers the actual drag path and these
ownership cases. All seven network tests pass against both DAT sets; independent
drop/give constructor checks also pass on both. These are offline checks, not
confirmation that Icefire accepts every transaction or completes its NPC scripts.

The receiver also restores the cached model for a dropped inventory item when
Icefire sends a world position without a new visual. A full world create or a
newer position overrides stale bag ownership; either ordering of position and
inventory removal is supported. Containment removes the world model, and wrapped
position stamps and object incarnations reject delayed updates after pickup.
Live examination requests now follow received quality, death, visual and
inventory changes, coalescing changes while a request is pending. An unchanged
open panel no longer sends continuous requests. All eight targeted tests
(`AC2.Network` plus `AC2.Data.LiveExamination`) pass against both DAT sets.
The logged Icefire GUID following repeated examination requests is not a server
exception trace, so the Drudge error's cause remains unconfirmed.

The November 13 comparison with the admin 7148, Legions 7160 and client 0930
decompiles also restores `DoFX_Private`: its unaddressed effects target the local
player at native scalar 1. Initial PlayerDesc forwards all ten decoded quality
tables, including cooldown timestamps and exact 64-bit identities, through the
same path as incremental changes. `ReleaseBehavior` releases the original action
before starting an optional replacement; `StopLayer` stops only the addressed
native layer. Unreal validates object incarnations and preserves corpse poses.
The six network-presentation tests pass against both stock and patched Icefire
data. Native hold-loop ranges, some physics actions and behavior-attached FX
lifecycles remain incomplete; see the [compatibility review](../Docs/DAT_COMPATIBILITY_REVIEW.md).

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

The game opens its account/password form before loading any environment. In account-service mode, enter the account-service URL and password; the client obtains the server's one-use native ticket with Windows WinHTTP. HTTPS is required except on loopback, and redirects are disabled. Passwords and tickets are not put on command lines or in client logs/save files. Credentials go to the bridge through private stdin. Credentialless development login (blank URL and password) is restricted to loopback. With account-service login, host/port come from the trusted account endpoint. Explicit launcher `-Credential` automation remains supported through a child-process environment ticket.

### Saved servers and accounts

The sign-in screen has independent server and account lists. Choose **New** under
Servers, enter a name, select **AC2E** or **Icefire**, and set the host/IP, game
port, client version and DAT folder. AC2E also uses an account-service URL;
Icefire hides that field and uses direct GLS authentication. **Save** preserves
the server. Accounts listed on the right belong only to the selected server.
Enter a username/password, optionally select **Remember password**, then Save or
Connect. Remove deletes the local saved entry, not the account on the game server.

Metadata lives in `Unreal/Saved/Login/profiles.json`. Passwords are stored in the
current Windows user's Credential Manager, bound to server type, host, port,
account-service endpoint and username. Editing that identity clears autofilled
credentials. Without Remember password, only the username is saved. Explicit
launcher connection arguments override saved selection without autofilling a
password for the overridden destination.

Keep each server's DAT set in a separate directory. The selected profile's
absolute folder takes precedence over the launcher's default DAT directory.
Changing it reloads the map, fonts, texture caches and character catalog before
connecting. Connection credentials survive that reload only in memory, never
in travel URLs. The AC2E default uses `Asheron's Call 2 - Legions/`; the optional
Icefire default points to `Saved/IcefireClient/` when installed.

The administrator-supplied [Icefire client distribution](https://www.dropbox.com/scl/fo/vjq02rnze89gv8pbxb6hy/ANxKOXAyHJkdQzhcsL-DtjI?rlkey=rp451mxlpzzydh0243huqhbh1&e=1&dl=0)
was inspected without running its binaries or changing the original AC2E DATs.
Its portal, cell, high-resolution and English archives open, and its character
catalog loads. On 2026-11-11, live negotiation requested 760,070 bytes of updates:
portal iterations **138 and 139**, English iterations **10 and 11**. Icefire
profiles now receive these updates on first connect, as the original client does.
The bridge copies the installation into a new snapshot under
`Unreal/Saved/ServerData/<server identity>/`, applies additions/replacements/purges,
rebuilds the native DAT index, and verifies every replacement and revision history
with the production reader. Original archives are never edited. Allow disk space
for a complete additional copy of the mounted DAT set (about 3.5 GB here).

After verification, the client saves the snapshot path in the selected server
profile, reloads all asset readers and reconnects automatically. Credentials are
retained only in memory until the character list arrives. A failed or incomplete
update is not selected or acknowledged. AC2E profiles continue using their chosen
original data without server-driven updates.

Live checks with this updated distribution covered world entry, stance changes,
weapon swap/restoration, self/stone inspection and training-stone use without
server errors. A separate rendered UI test verified first-connect download,
automatic DAT reload, automatic login and saved-profile persistence. Full Icefire
gameplay and all content remain unverified.

### Direct GLS login (servers using `--glsticketdirect`)

Select server type **Icefire** on the sign-in form, then enter the server
hostname, game port, account and password. The account-service URL is hidden
and ignored. The account keeps its capitalization; the password is passed unchanged
as the native packed GLS string (auth type `0x40000002`). It does not need to be
an AC2E 64-character hex token. Direct mode accepts 1–128 password characters;
the remote server determines its own account/password rules. Switching modes clears
the password. Failed connections retain the non-secret form settings for retry.

For the connection instructions supplied by Icefire's administrators:

```powershell
.\Launch-AC2.ps1 -Online -DirectLogin -Server icefire.portalspace.net -Port 8610 -Account TestingAC2 -DatDirectory 'D:\Games\Icefire'
```

Enter the password in the client, not the command line. Direct mode defaults to
the recovered Legions DVD version `7160`. The **Client version** field and launcher
`-Protocol` option also accept `7148` and `7230`; use the build required by the
server. The supplied Icefire instructions did not specify a build. Selection
decoding accepts the complete recovered admin and Legions `CharacterSet` layouts
without ignoring extra bytes. `7230` remains an experimental login profile.

This implements Legions `gmClient::InitAuth` and the `glsticketdirect` argument
in `Turbine::Client::AddCmdLineArgs`: a nonempty direct string selects GLS auth,
separately from GUN-ticket auth `0x41000001`. `--rodat on` controls the original
client's DAT access. This client's renderers read archives without modifying them;
Icefire login updates are published as separate verified snapshots.
Icefire's original client uses a separate GUN connection on UDP port `2310`.
The bridge supports the Friends tab on AC2E through the game connection.
Icefire friend-list synchronization requires its original GUN session protocol.

Direct credentials travel in the legacy UDP logon with reversible string encoding,
not HTTPS or password encryption. The bridge does not yet provide retail RSA
session-key export for independent servers that require it. A matching GLS login
format alone does not establish full server/gameplay compatibility. Icefire login
and entry with an existing character were verified using version `7160` on
2026-11-11: the rendered dungeon remained out of portal space for 15 seconds,
with one arrival, no recurring movement hold, and decoded inventory/crafting
profiles. Character creation and general gameplay were not part of that test.

An additional interaction check on 2026-11-11 reproduced GUID-style server errors
from our admin-format examination request. Requests now follow the selected build:
7148 retains its layout, while 7160 uses the Legions object flags. The reply codec
also retains the trailing word in Legions `ExaminationProfile::StreamPack`.
`WM_Player::26` / `CPlayer::E9` now selects the server-specified object and opens
Examine, and `CUsageSystem::0A` outcomes show their message or error, respecting
the silent flag. The test account loaded its inventory, examined itself and a
training stone, used that stone, and followed the server's examination notice
without server or decode errors. The HUD path has a separate automation test;
this does not establish compatibility for every NPC, spell or combat action.

The NPC/world-object use investigation on 2026-11-12 reproduced the default
“Item use succeeded” reply at the Shelter Sentry from 1.3 metres away. No decoded
speech, dialogue or quest update followed that use. This was not a complete
packet audit: unknown top-level opcodes were silently discarded. These now
produce an `unsupported` bridge event containing only the opcode and total byte
count, without exposing payload contents. The
nearby Looting training stone returned both the default message and the native
`WM_Player::26` open-examination action. The `AC2.Data.LiveExamination` automation
test passed with Icefire's patched DAT, including opening the server-specified
object's panel and requesting its examination profile.

`Tools/RetailProbe --usage` now runs the installed `UsageDesc::37` constructor;
the bridge's opt-in `native_use_matches_installed_dat_constructor` test matched
its complete reference graph for both Legions and patched Icefire data.
`CUsageSystem::0A` displays the completion message/error and clears local usage
state; it does not execute an additional NPC conversation from a success reply.
Thus a success acknowledgment alone does not establish quest progress. The
user confirmed that the supplied original client shows the Sentry's dialogue.
The cause of the missing Unreal dialogue remains unresolved; constructor parity
alone does not establish interaction parity.

The subsequent offline `--use-selection` oracle executes CPlayer selection and
the actual `CallIFun_UseSelection` interface using installed DAT code. Icefire's
patched DAT sends an omitted combat-selection notification (`0x82040001`) alongside ordinary
selection (`0x82120016`). Direct-server sessions now send both, including clear,
and suppress unchanged selections as retail does. This does not start an attack.
AC2E's existing target message is unchanged. Examination remains requested by the
Unreal UI; its wire format is also compared against native selection's request.
The comparison passed for Icefire's Shelter Sentry and Looting stone, including
the complete use descriptor. Stock Legions emits only the ordinary selection
notification in this offline path; the extra target update is scoped to direct
login, not AC2E sessions. These are offline
class-default checks, not a replay of the player's live quest state. The new
targeting behavior has not been tested on the live server, at the user's request.
No dialogue is fabricated from the success acknowledgment. See
[the oracle instructions](../Tools/RetailProbe/README.md#world-object-use-request).

A subsequent live check traced combat-stance exceptions to an omitted argument:
Legions `WM_Combat::16` requires a LongInt target, including zero for draw-weapon
without a target. Peace mode (`WM_Combat::14`) takes no arguments. The bridge now
packs both correctly. Native `WM_Inventory::51` / `CPlayer::C8` /
`CInventorySystem::0C` completion reads the actual equipment location from
`InvEquipDesc` slot 15. Slot 0 is not an error status in that native profile;
AC2E's explicit status extension is retained separately. Native `Parent`,
`DeParent`, `Contain` and `LeaveWorld` remain distinct from destruction, preserving
inventory metadata and cached visuals across weapon switches. Instance and
position stamps prevent stale attachment events from restoring an old weapon.
The test account entered/left combat, attacked a training creature, switched
weapons and restored its original weapon without server errors. It also repeated
self/stone examination and stone use successfully. These are bounded interaction
checks, not verification of every server skill or quest.

The Icefire UI acceptance test (`-AC2DirectLoginSmoke -AC2DirectWorldSmoke
-AC2ServerUISmoke`, with the existing test-account password supplied privately)
uses real Slate typing, Enter and toolbar clicks. It verified a returned private
self-tell and opening/moving Inventory, Skills, Examine, Quests, Social and
Options, plus opening the map. `.help` was checked separately through the same
entry path. No messages were sent to other Icefire players. Use
`/tell Character Name, message` for private chat. The isolated UDP regression also checks private messages in
both directions between two separate local characters.

**General chat:** `/cg message` now uses the native Turbine chat service advertised
in `CharacterSet`, through `CChatEvent` (`0x100D3`) on game queue 4. It does not
use the separate legacy GUN port or turn global messages into nearby speech.
Legions `WM_Communication::0x1B` / `ChannelData 0x2023` and Icefire's retained
`WM_Communication::0x1E` / `ChannelData 0x1AF4` supply the channel-to-room table;
General's `0x80000` channel resolves to the room in slot 3. A read-only Icefire
login on 2026-11-12 confirmed Turbine chat enabled and General room 1. No room ID
is hardcoded. Logout clears assignments, missing rooms produce a useful error,
server rejections are displayed, and incoming room messages retain their sender
and a channel label. Trade and other room commands are not wired yet.
`room_chat::tests` verifies both table layouts, replacement/reset, malformed
input, Unicode, acknowledgements and rejections, and a real bridge command's
queue-4 round trip through the isolated Legions server double. Unreal
`AC2.Network.GeneralChat` exercises the chat entry's Enter handler, `/cg` routing,
received text, errors, and existing `/say` and `/tell` routing. Public message
delivery to another Icefire player has not been tested.

Window roots and compass artwork now follow the active DAT: Icefire has
standalone panel roots where the AC2E data has left/right wrapper panels.
Controls are kept clear of the bottom toolbar where the panel fits. Native
error 54/91 is shown as “Item use interrupted”; idle movement heartbeats no
longer set the cancel-moveto flag. A live training-stone use completed during
five seconds of idle heartbeats. Automatic walking to distant interaction
targets is not yet implemented, so approach an NPC before using it.

Bridge event output is ordered and bounded to 8 MiB, including an in-flight
write. A background writer prevents slow Unreal asset loading from blocking UDP
acknowledgements and heartbeats. Reader closure or exceeding the bound ends the
bridge rather than dropping protocol events or growing memory indefinitely.

Icefire's nonempty quest list includes an extra word per quest beyond the recovered
`gmQuestInfo::StreamPack` layout. A bounded, opt-in fallback for player/quest
profiles preserves this word without assigning it a meaning; this is observed
server compatibility, not a claim of recovered retail behavior. Both layouts
validate complete argument/reference boundaries. Native quest records with
spurious script heap fields are rejected rather than accepted as shifted data.
GUID-style server errors are also logged with the current target and examination
IDs, so future reports can be correlated without recording ordinary chat.

Native position, teleport and force-position stamps distinguish routine movement
echoes from travel. Ordinary self echoes preserve the locally driven body and
momentum; a fresh force-position stamp corrects location without restarting the
portal effect or changing facing. A fresh teleport stamp starts destination loading.
This follows `CPhysicsObj::RecvEvt_Position` / `SyncPositionSimple`, including
16-bit counter wrap and valid instance zero. Previously Icefire's echoed movement
repeatedly reopened portal space and restarted its sound even after the dungeon
had loaded. Profile decoding errors also no longer return an entering player to
character selection.

The bridge answers native queue-5 DAT interrogation (`0x10037`) with the installed
files' actual `0xFFFF0001` iteration histories and header dataset/subset IDs, in
retail portal/local/cell/high-resolution order. This follows
`CLCache::OnServerInterrogation`; English is language `0`. It acknowledges an
empty patch using `CCliDatEndDDDEvent`. Icefire profiles also accept native
`CCliDatDataEvent` records (raw or zlib), including records delivered before the
unordered BeginDDD message. Downloaded records must match the announced plan;
record/total sizes are bounded. The bridge sends EndDDD only after publishing a
verified snapshot. Missing records, decompression errors and failed disk writes
cannot advance installed revision histories. Installation runs off the network
polling thread so acknowledgements and keepalives continue during file copying.
Login has a timeout for stalled updates as well as a missing character list.

`python Network/test_direct_login.py --ui` checks the
mouse-driven sign-in form and actual bridge UDP packet on loopback with disposable
credentials; `Network/test_auth.py` covers existing AC2E ticket authentication.

## Network controls and authority

Incoming attacker selection follows native `PhysicsStory` type-1 target hooks.
The bridge preserves explicit attacker, skill and story-owner instance in its
private `story_target` event; the client reads the DAT harmful-skill flag before
applying Auto Target. The server now supplies native melee skill `6` for creature
attacks, including misses. A miss has a target-only hook without fabricated health
loss, hit result or blood. Potions and falling damage carry no invented skill.
The auto-target preference is native game-options bit `0x20`; it never replaces
an existing selection on incoming hits or starts an attack. No new UDP protocol was introduced.

Removal/unselectability uses the native targeted `StopAttacking` request
(`0x82040025`, one tagged 64-bit IID), exposed internally as `stop_target`.
It stops that target's attack without leaving combat stance; a delayed stop for
another target is ignored. The existing `stop_attack` IPC remains explicit peace
mode. Changing selection continues an existing basic repeat chain, but does not
start attacks from idle combat stance or turn a pending skill into auto attack.
CreateObject now retains explicit selectability (descriptor mask `0x00800000`,
bitfield `2`) and pet summoner, with absent selectability falling back to the
installed entity definition. Bool quality `0x270` updates selection as well as
radar visibility; IID quality `0x1f9` updates the summoner. Stale instance notices
cannot remove or invalidate the current incarnation.

Game options use native `UploadGameOptions` (`0x8212000c`): tagged integer
bitfield, radar mask and shortcut height, then a tagged float damage-text range.
Height can be applied from the native dropdown in Options → Game or changed
immediately using the hotbar grip; both share the same character profile field.
The bridge reads profile fields `0x10a8` from the login LOAD; partial profiles
merge only present fields and inventory-only LOAD messages do not reset options.
Unreal restores the supported damage, radar, hotbar and auto-target preferences
per character. Unknown bits, radar flags and an unchanged native range (including `-1`) survive
edits. Heights are restricted to 1–5. The existing server `o1` journal persists
these values across restart; no store migration is needed.

Hotbar edits use native `WM_Player::SetShortcut` (`0x82120018`) with a
`ShortcutInfo` native graph and slot ordinal. Login restores the shortcut array
from `GameplayOptionsProfile` in `0x82120027`; an omitted field does not clear
bindings. Skills, 64-bit item IDs, recipes and text commands round-trip. Existing
local bars import once per connection/character, then the server becomes the
source for subsequent logins. Named alias definitions and the selected page remain
local. Text is limited to 512 UTF-16 units per slot and 16 KiB for the full native
bar; oversized edits are rejected before a partial upload.

After shortcut or game-options writes, logout waits for an ordered native self-examination reply
before sending the unordered control-channel logoff. This is an adapter ordering
guard, not a recovered retail logout algorithm or a new wire message. Ordinary
self-inspections are tracked separately, and the probe respects examination
pacing. A missing reply cancels logout instead of reporting the preferences as
saved. The full acceptance check is `python Network/test_shortcuts.py`; it uses
a disposable world and stops its own server.

WASD/Shift/Space move. Enter opens server chat (`/say`, `/me`, `/tell Name, message`, `/cg message` for General). T cycles nearby server objects; Z toggles combat/peace; 1 requests repeated attacks. The equipped weapon selects the native melee, missile or magic stance. 2 uses the racial starter magic skill and 3 uses the missile skill; equip a matching weapon first. `/cast <hex skill ID>` sends a learned active skill request; unsupported skills are rejected by the server. `/stop` ends combat. E or `/use` uses the selected world object, including mentors, portals and lifestone binding. Home or `/recall` requests native lifestone recall; a dead character requests resurrection. I opens the native inventory; double-click an item to use/equip it (unequip the current hand item before replacing it). C displays server stats. F4 or Escape → Leave World logs out to character selection. Escape on selection disconnects to sign-in. `/loot` sends the native loot-container request when supported by the connected server.

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

The bridge uses AC2_Rusted commit `bcf67b4b9ac3ee9eb429d68ba6766cfce47c5a25` in `References/AC2_Rusted`. `Cargo.lock` pins registry dependencies. Initialize it with `git submodule update --init --recursive`, or clone this repository with `--recurse-submodules`. Before a direct Cargo build on a fresh checkout, run `Build-AC2Network.ps1` to apply the reviewed [native login profile patch](Bridge/patches/native-login-profiles.patch) and [story scene codec patch](Bridge/patches/native-story-scenes.patch). The script recognizes previously applied patches and reports conflicting edits.

The patch adds `CNativeFactory`'s native `AppInfoHash` (`0x0f`) and `VisualDesc`
(`0x71`) codecs for initial inventory profiles. Visual descriptors use the existing
bounded decoder and preserve their validated wire bytes when repacked. Crafting
registries follow their transmitted root reference, including zero, and accept
Icefire's observed six-slot `0x1b4f` skill record alongside Legions' `0x345f`;
unknown record packages remain errors. No captured server data is distributed.

`Network/test_network.py` uses two native UDP clients and a unique temporary store to verify creation, visibility, movement, chat, mob death, loot, logout, re-entry and persistence across a full server restart. `Network/test_auth.py` verifies account login, wrong-password rejection, ticket replay rejection, anonymous rejection and the source archive. They use loopback ports 19270/19271 and 19280/19282/19283 respectively. Results are in `Saved/AC2NetworkTests`.

All integration scripts generate their compatible content in the temporary test
directory. Set `AC2_DAT_DIRECTORY` to use a retail installation outside this
checkout; no pre-existing `Saved` files are needed.

`Network/test_scene_loading.py` uses ports 19488/19489 and an isolated saved arrival near
a captured Edge Beetle. It withholds `SceneRenderingComplete` for five seconds,
checks that nearby objects, inventory, skills and crafting arrive without incoming
damage, then acknowledges readiness and verifies native aggression starts without
a second `CreateObject`.
Loading clients receive live object changes but their own avatars remain hidden
and unavailable for combat until activation.
The client acknowledges at the end of its final rendering hold, not immediately
after finding a landing position. `AC2.Render.PortalArrivalReadiness` checks that
initial login stays inactive throughout that hold.

`Network/test_outdoor_population.py --render` checks native creature speed,
pursuit, return paths, late observers, ground contact, combat animation and corpse
pickup. The disposable server uses a fixed loot RNG seed to exercise a nonempty
corpse; production drops may be empty. Position packets are stamped at their
simulation sample time so collision-processing delays do not distort movement.

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

`Network/test_cooldown_ui.py` runs an isolated server on port 19391 and verifies
native SkillInfo last-use timestamps and shared recovery qualities on accepted
casts, rejected repeats, skill advancement and world re-entry. It then runs ten
rendered client stages checking requested markers, active animation highlights,
accepted/rejected completion,
decreasing cooldown fill, duplicate shortcuts, page changes and expiry. A local
UDP proxy on ports 19394/19395 delays unchanged server datagrams by 500ms to
expose the pending state; both destination ports retain the same upstream peer.
No server events are fabricated. Skill resets travel in the existing native skill graph
(flag `0x20`, TIME slots 0–1); shared recovery uses float quality `0xbb8` and
timestamp quality `0x12f`. No timer is synthesized from a click or completion
notice. Local `skill_requested` bridge events identify the counter actually
sent on the wire. Successful skill PhysicsStories carry that counter in
`client_attack_context` and the maneuver in `skill`. The bridge's `story` event
preserves owner/instance identity; its matching behavior carries `story_request`.
The client follows the exact native action instance through start, completion,
failure and replacement. Accepted skills no longer send `EndSpecialAttack` at
animation start. Rejection/cancellation still uses that notice and its counter, while
`ToggleSpecialAttack` decodes the flag followed by the maneuver. The test stops
its disposable proxy and server when finished.

`Network/test_journal_ui.py` checks native skill/quest/fellowship controls and
window/scrollbar behavior in a rendered client on isolated port 19388. It also
tests item shortcut drag/examination/use, hotbar resizing to five rows and
disabled end padding. Stages 16–17 also verify native skill shading and requirement
text against acknowledged bow equip/unequip changes. Retail Ctrl+number and minus/Shift+minus page controls
route through Slate and the player controller; Shift+0 equips and unequips a bow
from the second row with server acknowledgements. Chat and fellowship name input
must keep keyboard shortcuts inactive while focused. It also scrolls native
options to the final row and verifies Revert/Default/close confirmation buttons,
discard behavior and dialog resources. Radar checks apply and restore the NPC
filter against live network contacts, preserve the selected world target, and
exercise coordinate visibility from both Character Options and the radar
button. The same scenario verifies locked hotbar edits, the Shift drag override,
skill-book double-click quick-add, and the native lock checkbox's draft, Apply
and Defaults behavior. Stages 37–41 exercise the instant-scroll checkbox,
quarter-second hotbar transitions, clipped partial rows, final-page padding,
mid-transition reversal and returning to instant paging. Stages 42–45 open the
native height dropdown, select five rows without applying, then apply and verify
the resized bar before restoring the fixture. Options and shortcut
testing do not persist their temporary settings.
Logs and screenshots are
written to a disposable `Saved/AC2NetworkTests/journal-ui-*` directory.

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

Compatibility details: Pyrealium accepts unencrypted character creation on queue 4, and retains the incoming ordered counter across character logout. The bridge adapts these two behaviors in its own session layer. It decodes both recovered selection layouts, including Pyrealium's current admin7148 layout when using a newer login profile.

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
