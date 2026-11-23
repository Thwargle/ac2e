# Native gameplay, appearance and content audit

## Celestial lighting, FX lights, native idles and Icefire actions — 2026-11-11

The outdoor directional light now follows the rendered DAT sun/moon centre.
The existing `LScapeSky::CalcFrame` mesh rotation is retained; the actual mesh
centre matters because the celestial geometry is authored away from its origin.
Native day-key colors/intensity remain unchanged. Alignment eases back toward
the keyed direction at the horizon, and the sun takes priority when both bodies
are visible. This is an Unreal shadow alignment adjustment, not a claim that
the original independently keyed lighting vector followed the orb exactly.

`LightSourceFX::UnPack` records were previously consumed and discarded. Their
range, color, intensity waves, lifespan and fade now create and update source-
attached point lights. Light-only FX also survive table loading. Zero lifespan
means persistent until stopped; finite lights fade and are destroyed. Native
`CPhysicsObjRep` offset/lifetime behavior supplies the attachment and envelope
rules. Lights affect outdoor and dungeon surfaces, with Unreal shadows and the
same intensity conversion used by other native point lights. UI preview scenes
retain their separate lighting path.

Lightning Bolt (`2A6`) uses cast `41000487` and release `410004A9`.
Correction from the subsequent DAT review: `41000031` is ongoing vigor-damage
status FX, not a one-shot impact. Its stock entity `47000CDB` (visual `1F00031A`)
and behavior `41000086` accompany unlimited particle emitters. They now remain
until authoritative status removal, then finish their authored particle tails.
The earlier captures demonstrate these status visuals, not a fully recovered
Lightning Bolt impact implementation. No speculative caster-to-victim beam was
substituted. FX with no authored light are not assigned an invented light merely
because their particles glow.

A stationary native-ID NPC could enter the unsupported airborne animation branch
when a short collision probe missed the floor. Grounded native motion now remains
grounded in that case. The Lost Wish Explorer's female Tumerok body (`1F000D8C`)
animates its hand and body independently of the head/tail in the regression test;
six other NPC/creature visuals also animate without a nearby floor.

Live Icefire debugging reproduced GUID errors from the empty draw-weapon payload.
`WM_Combat::16` requires a LongInt target even when that target is zero; the bridge
now sends it. `CInventorySystem::0C` reads actual equipment location from descriptor
slot 15 and does not interpret native slot 0 as an error status. The bridge uses
that native layout for direct login while retaining AC2E's status extension.
It also forwards `Parent`/`DeParent`, and no longer conflates `Contain` with
`DestroyObject`. The client retains owned item metadata and visuals through
containment/world exit, rebuilding the weapon on a subsequent parent event.
Actual inventory removal and destruction still remove the item.

Validation: 47 bridge tests and 5 capture-decoder tests passed. Focused Unreal
tests cover native equipment lifecycle, player position stamps, skill/equipment
feedback, seven animated NPC/creature visuals, celestial alignment and FX lifetime.
The GPU test confirms an authored FX light changes nearby surface pixels; native
status-effect captures cover their visible presentation. Live version-7160 checks on
Icefire exercised stance changes, repeated attacks against a training creature,
weapon switching/restoration, self/stone examination and training-stone use without
server errors. These checks do not establish complete Icefire gameplay parity.

## Native ramps, moving grips, quest feedback and shadows — 2026-11-10

The reported ramp at world `(254.4, 40731.8, -71.3)` is native entity
`47000050`, ordinal 6 in `03FD0120`, setup `0400008F`. Its visible `0600014B`
mesh and separate `0600014C` physics mesh agree within four centimetres at
the reported position. Animated scenery preparation previously discarded
collision. The unweighted physics part now remains static in the group frame,
while weighted ornaments animate. The retail `CPartArray::RefreshMatrices`
implementation (`004E1250`, Legions 7160 decompile) uses the group matrix for
all parts of a skinned group, rather than treating mesh ordinal as a bone index.
That rule also applies to static fallback and unweighted rendered parts.

Restoring native tower/ramp collision makes the forward and reverse tutorial
routes pass without the prior reconstructed spiral stair, which has been removed.
The walking test starts on the cave floor at the buried ramp foot and climbs
the native surface, then descends, for both Tumerok sexes at size extremes.
The authored placement is retained; no speculative ramp relocation was made.

Held-item attachment now runs after local animation and CharacterMovement.
Holding transforms combine socket, animated bone and complete part matrices
before decomposition; independent weapon scale is retained. The running test
uses the engine scheduler and checks the current hand against the attached
weapon every frame, with both sexes and minimum/maximum height/build.

Shelter quest items already went directly into the player's bag on eligible
kills. A read-only review of the reported character confirmed the Imbued Digging
Tool had been awarded. An empty quest corpse now reports that fact and the
next interaction, rather than the generic unavailable-script response. Missing
prerequisites and full-bag retry instructions are also explicit. The reward
rules, duplicate protection and saved character state are unchanged.

World opaque, cutout and foliage materials now receive UE direct lighting and
shadows instead of emitting the complete lit color. DAT texture/color stages,
ambient and finish parameters remain inputs. Native dungeon light records
create point lights on a separate lighting channel; outdoor sun cannot light
those surfaces through an unloaded roof. All visible lights illuminate, with
cube shadows limited to the nearest eight. UI scenes keep native vertex lighting.
This is an Unreal shadow implementation using recovered lighting data, not an
assertion that the original renderer's illumination has been reproduced exactly.

Validation: all three racial Shelter quests passed over UDP through overworld
exit and relogin, including the new corpse guidance. Server library tests:
289 passed, one existing ignored. Six focused client data suites passed,
including all starter forward/return clearance routes and actual ramp/passage
walking. GPU pixel checks confirm sun and dungeon point-light shadows; offscreen
captures cover the native ramp, dungeon bridge, outdoor building and javelin poses.

## Tumerok passage backtracking guard — 2026-11-10

The follow-up obstruction at world `(196.0, 40825.8, -72.9)` was reproduced
at the `03fd012a` / `03fd0128` passage. It was not a solid wall: the walking
capsule crossed the native floor, but the GameMode safety ray missed a narrow
seam and reset the player to `LastSafe` every frame. Increasing the ray length
did not fix the missing point contact. The previous A* clearance audit did not
exercise this runtime safety guard.

The guard now accepts CharacterMovement's valid, nearby walkable floor contact
before using the existing point-ray fallback. It rejects stale contacts after
teleports, contacts far from the current capsule, and disabled/unloaded support.
The same guard protects indoor and outdoor movement. Native geometry, collision
walls and quest requirements are unchanged.

`AC2.Data.TumerokPassageWalking` drives actual movement inputs and capsule physics
through the reported doorway, around the corridor bend and back, for both sexes
at the smallest/largest height/build combinations. It checks the same safety
predicate used by GameMode, including unsupported-space and stale-contact
rejection. The four round trips exercise 16 valid walking steps missed by the
old point ray. This is an automated movement regression, not a manual playthrough
of the complete Shelter quest chain.

## Tumerok Shelter return route, drums and javelins — 2026-11-10

The previously reconstructed stair in cell `03fd0120` was rotated 180 degrees,
putting its top tread away from the upper bridge. A clockwise quarter-turn now
joins the tread to that bridge. Position and native stair geometry are unchanged.
This remains an AC2E reconstruction using native `47000436 / 040003da`, not a
recovered retail placement. The collision audit now checks the reverse route
from Sentinel through Scout, Digger, Sentry and Lieutenant to Guard, as well as
the forward route and vault. The return search uses a 25 cm grid where narrow
stair treads defeat the coarser 50 cm search. Both use swept player capsules.

The reported character has no Shelter quest progress. The portal previously
always asked for a glyph, even before the player could earn one. It now uses
the existing stage-specific Shelter guidance. The native quest sequence remains
Guard → Lieutenant → Sentry → Digger's Tool → Scout → Sentinel's Glyph → Menhir
Portal → Advance Scout → Shard → overworld. No production save or inventory was
edited, and neither the glyph gate nor quest requirements are bypassed.

`Equipment::hand_slot` previously allowed special holding points only for
starter weapons. It now also admits exact validated treasure weapon identities,
including Rain Drum `47000f02 / 1f000059`, Fury Drum `47000f05 / 1f000427` and
Spine Javelin `47000f2e / 1f0002b3`. Native metadata continues to supply the slot,
holding point and restrictions. Recovered implement masks now select missile
or magic combat categories, instead of falling back to melee. Existing level,
race, visual, quantity and ownership checks remain in force.

Legions `CPhysicsObj::UpdateChild` (`004D83D0`) scales the socket's translation
with its parent, while the held child retains its own scale. The client now
matches that behavior for local and remote held items. The former transform
product stretched javelins and drums with the avatar's height/build.

The shared male/female Tumerok body clips give missile release at
`05000705`: 1.416669965 s × 0.472692996 = 0.669649970 s (auto throw), and
`05000707`: 2.041670084 s × 0.629001975 = 1.284214525 s (normal throw).
Their `40000003` story hooks dispatch `StoryManager::FireMissile` (`00502AA0`).
The server now waits 670 / 1284 ms respectively before release; damage follows
flight, not windup. Other release timings and projectile flight remain authored.
`CreateAndParentMissiles` (`00502DE0`) hides the original thrown weapon while
using a missile clone; `CleanupStory` (`00502BA0`) restores it. Our separate
network projectile now pairs with hiding the held javelin from release through
recovery, also restoring it immediately on cancellation. This is scoped to the
two verified clips; full native missile cloning and the retail restore fade are
not implemented by this change.

Validation: 327 server tests passed (four pre-existing tests ignored); real UDP equip/stance/restart checks for both drums
and Spine Javelin; all three racial Shelter quest chains through overworld and
relogin; forward/reverse collision route checks; both-sex/scaled-avatar held-item
checks; offscreen windup/release/recovery captures inspected. Collision searches
and network anchor traversal are not a manual playthrough. Client editor,
standalone game and server builds succeeded. The source offer was refreshed;
the game server is left stopped for the operator.

## Selected-object removal and targeted attack cancellation — 2026-11-10

Auto Target now handles deletion and loss of selectability. A selectable corpse
remains selected on death; only removal or unselectability invokes replacement.
With the option off, selection clears without choosing another object. An
unselectability notice cancels the old attack regardless of the option; deletion
requests targeted cancellation when Auto Target is on, matching the recovered
notice handlers. Instance stamps reject stale removal/quality notices.

Evidence: Admin `gmSelectionUI::RecvNotice_UpdateSelectability` (`00424670`)
and `RecvNotice_BeingDeleted` (`00424720`) supply the notice ordering. These
handlers do not have named mappings in the available Legions function index.
The nearest-foe selector is present in Legions at `00423B70`, key `41000024`:
radar range is 75 m outdoors / 50 m indoors, range uses XY, ordering uses
`sqrt(X*X+Y*Y) + 1.2*abs(Z)`, and `gmSelectionUI::Farther` (`004236C0`) breaks
equal distances by unsigned IID. The client uses installed script inheritance
and explicit descriptor/quality selectability, rather than names or visuals.

Legions `gmCWInterface::StopAttacking` invokes IFun `00b40104`. In the installed
WLib, package `1aa4` resolves it at `2f32c` to `9aac0013` (`b86c8`), which calls
`82040026` (`facc8`) and sends native `82040025` with one tagged target IID.
The bridge now exposes this through private `stop_target` IPC; the existing
peace-mode command is unchanged. The server preserves stance when stopping a
targeted attack and rejects delayed cancellation of a different current target.
Selection changes only continue an already running basic repeat chain. Previously,
being in combat stance was enough to start attacks on a newly selected creature.
Selection changes still complete canceled skill requests once.

Scope: current replacement eligibility is conservative PvE: native monster
classes, visible placed actors, no self/fellow/player/NPC/pet, no known corpse,
and a shared indoor/outdoor area. Unpublished health does not make an otherwise
eligible monster unavailable. Retail's complete `CanAutoAttackTarget` PK/faction
rules are not reconstructed here. Head/eye `LookAtSelection` and last-attacker /
last-selection commands remain separate gaps. Evidence is local decompile and
installed DAT inspection, not a live retail-client comparison.

Validation: editor and standalone builds passed. All 85 client tests passed
(75 clean, 10 with existing fixture/resource warnings); 362 server/bridge tests
passed with four existing ignored tests. New checks cover death versus removal,
stale instance notices, explicit unselectability, nearby monster selection with
unpublished health, pets/unknown classes/hidden/dead targets, distance ordering,
unsigned-ID ties, the outdoor range boundary, option-off cancellation, native
stop serialization and idle/pending-skill/repeating-attack selection changes.
Client report: `Unreal/Saved/Automation/Run-e29f91d10dee45ffbc2d7eb2b0784fd4`.
Build/test logs use `Saved/replacement-*`.
The real two-client UDP fixture passed targeted stop with matching local/remote
combat stance and no attack after clearing/reselecting the creature. Existing
melee/magic/missile, cast/impact effects, starter-dungeon exit, resurrection and
restart persistence checks passed as well. Report:
`Saved/AC2NetworkTests/gameplay-e0cb27a5de714cfc94ad3b6a4a046530/result.json`.
The fixture stopped its disposable server; the normal server was not started.

## Native incoming-attacker selection — 2026-11-10

Added Auto Target to Options → Game using `ID_Option_AutoTargetText` and its
native help string. It defaults on and maps to `GameplayOptionsProfile` bit
`0x20`. Draft edits wait for Apply; Revert and confirmed Defaults retain the
existing options behavior. Login applies the server bit before any subsequent
combat stories in the same receive batch, and unrelated native flags survive.

Evidence: Legions `gmSelectionUI::RecvNotice_E_LastAttacker` (`004235A0`,
Admin `004237D0`) remembers the attacker and selects it only if Auto Target is
enabled and no object is selected. `StoryManager::DoStoryHook` (`00503000`,
Admin `00514FF0`) raises that notice for a type-1 hook targeting the player when
the story's explicit skill passes `IsSkillHarmful`; damage amount is not the
trigger. `gmCWInterface::IsSkillHarmful` (`00453170`) and
`ggmCWInterface::CallIFun_IsSkillHarmful` (`0045FD50`) dispatch `00b400c5`.
The installed Legions WLib resolves that to code `88ff4`, then SkillData
function `022c004a` at `1092cc`, reading field 70's `0x400` flag. Native
`gmCharacterUI::{RefreshFlag,ApplyFlag,Setup}` supplies the option/default.
These are local decompile and DAT checks, not a live retail-client session.

The bridge now retains explicit target, attacker, skill and story-owner stamp
as a private `story_target` event. Unreal rejects stale owners/instances and
third-party targets, then checks the native skill flag. It remembers the last
attacker even when disabled or already targeting something. Selection uses the
existing visible/placed object checks and ignores explicitly unselectable actors.
An existing target, including a corpse, cannot be replaced by an incoming hit.
Selection does not change combat stance or request an attack.

The server's creature melee stories previously lacked a skill, preventing this
native trigger. They now carry melee auto-attack skill `6`. Misses carry an
explicit target-only type-1 hook (`0x101`), without a health result, damage or
blood event. Hits retain the signed health hook (`0x981`); skill presence adds
story bit `0x10`. Victim-owned stories retain the victim's instance, not the
attacker's. Potion recovery and falling damage retain no skill; known player
skill hits/heals preserve their actual maneuver. This does not add new monster
spell behaviors or change combat ratings, damage, loot or store formats.

Validation: editor and standalone builds succeeded. All 85 client automation
tests passed (75 clean, 10 with existing fixture/resource warnings), including
native harmful/helpful skill flags, misses, full-width object identities,
preserved selections, stale/hidden/unselectable objects, no automatic stance or
attack change, and option drafts, apply, revert and immediate profile restore.
The server and bridge suites passed 357 tests (four existing ignored tests). Logs are
`Saved/auto-target-{editor,standalone,network}-build.log`; the client report is
`Unreal/Saved/Automation/Run-6f8ef1992cd74847b25cc769edab46df`.
The real two-client UDP gameplay fixture verified the explicit melee skill and
victim-owned story stamp at both peers, and passed the combat, dungeon exit,
death/resurrection and restart regressions. Its report is
`Saved/AC2NetworkTests/gameplay-0f76319a643c4039b1da74c3fa9a1bea/result.json`.
The fixture stopped its disposable server and did not start the normal server.

This earlier pass implemented the incoming-attacker branch of Auto Target.
Replacement targeting is covered above; native `LookAtSelection` head/eye
behavior and last-attacker cycling commands remain unfinished.

## Native shortcut-height option — 2026-11-10

Options → Game now exposes the native `OptionDropDown` / `DropDownMenu` choice
for shortcut height. Its localized labels `ID_Option_ShortcutHeight1` through
`5`, menu display, popup, text-item templates, spacing, font and selection art
come from local-English layout `22000001`. The popup is placed above other HUD
content rather than clipped to the options list, and constrained to the viewport.
Outside clicks and Escape dismiss it without passing input to the world.
All five entries fit the popup, so its scrollbar is disabled and its thumb is
hidden, matching `UIElement_ListBox::UpdateControls` (`005B4400`) and
`UIElement_Scrollbar::UpdateLayout` (`005BA660`) with the DAT's
`hide_when_disabled=false` setting. Clicking that track does not dismiss the menu.

Evidence: Legions `gmCharacterUI::ApplyChoice` (`00445FE0`, Admin `004454A0`),
the setup of the five values and default one in `gmCharacterUI`,
`OptionChoice::{SetCurrent,Apply,Recall,Default,ListenToMessage}`
(`0044F260/0044F3E0/0044F430/0044F450/0044F470`), and
`UIElement_Menu::{InsertTextItem,Open,Close,MouseDown}`
(`005B6050/005B6410/005B6600/005B6790`). The admin decompile supplies the
`selection_display`, `text_item`, `popup` and `open_up` attribute names.
The pinned reference's `character.rs` shortcut-height notice confirms that a
grip resize refreshes the current and saved choice together. Reference sources
were read only. No live retail-client session is claimed.

Choice changes remain pending until Apply; confirmed Revert restores the applied
height and confirmed Defaults applies one row. Grip resizing immediately refreshes
the height choice while preserving unrelated drafts. Neither path changes any of
the 100 bindings or the selected page. Native `UploadGameOptions` continues to
persist height; the existing character-specific shortcut cache is retained for
offline play. No global height setting or new wire message was introduced.

Validation: editor and standalone builds succeeded. All 84 client automation
tests passed (74 clean, 10 with existing fixture/resource warnings). The final
dropdown refinement also passed the two focused option tests without warnings.
Coverage includes native labels/layout at 1024×768, 1600×900 and 2560×1440,
draft/apply/revert/default behavior, popup dismissal and input isolation, the
disabled scrollbar, grip synchronization, preserved bindings/page, and uploading
the applied height through the existing server profile. The connected rendered
journal scenario now includes opening this menu, choosing five rows, applying
and restoring its disposable fixture. Captures 42–44 show the open menu, pending
choice and expanded bar; both menu and bar were visually inspected.
Final build logs are `Saved/height-choice-{editor,standalone}-final-build.log`;
automation reports are `Unreal/Saved/Automation/Run-e1baa45cb8dc49e49025588ade33efd5`
and `Unreal/Saved/Automation/HeightChoiceFinal`. The final connected render pass
and screenshots are in
`Saved/AC2NetworkTests/journal-ui-f354c57706724893b5184e02ccde120b`.

## Native game-options persistence — 2026-11-10

Completed the existing `UploadGameOptions` path through the Unreal HUD and Rust
bridge. The native request is `8212000c` with exactly 32 bytes: tagged integer
option bits, radar mask and shortcut height, then a tagged float damage-text
range. The server already owned, journaled and replayed these fields as `o1`;
this pass changes the client integration, not the journal format.

Evidence: Legions `gmCWInterface::UploadGameOptions` at `00452C40` (Admin
`00451AC0`), `gmCharacterUI::Apply`, and `ApplyValue` (`00445F90`, Admin
`00445450`). The latter forwards the normalized 0–1 slider directly. Admin
WLib dispatch `9aa4008f` at offset `437ac` and `8212001d` at `102078`, argument
tags, bounds and retained capture evidence are recorded in
`AC2E/emulator/docs/world-evidence.md` under native gameplay options. The pinned
reference's native options codec and recovered `character_options.rs` map number
labels `1`, damage `4`, third-party damage `8`, radar coordinates `10`, shortcut
lock `800` and instant scrolling `2000` (hex). Radar categories use low bits
`1/2/4/8/10/20`; unexposed bits are preserved. The existing reference sources
were read, not changed. Validation uses available native data and decompiles;
a live retail-client session is not claimed.

Login LOAD reads presence bits `10a8` for these fields. An absent inventory-only
profile is ignored; a full login with shortcuts but no saved options establishes
an explicit local-default baseline. Partial profiles merge only their present
fields, atomically rejecting invalid words, heights or ranges. Height accepts
1–5. The native range retains finite values >=0 or the `-1` unset sentinel.
The UI displays a bounded slider but does not normalize a sentinel or a larger
native range when changing unrelated options.

Restore waits for the matching character's ready world session. Per-character
server options do not overwrite the global legacy defaults. First-time
characters use those defaults until the player makes an explicit change.
Apply commits applied values only; unrelated drafts survive radar-coordinate
and shortcut-number actions and profile refreshes. Hotbar resizing saves height;
page changes and unchanged preferences send nothing. Native bits outside the
supported checkboxes and radar filters survive every upload. The existing
ordered self-examination barrier now also covers options-only writes before
logout, preventing control-channel logoff from overtaking persistence.

Remaining local preferences include named alias definitions, selected hotbar
page, graphics/audio/chat settings and character options whose native mapping
is not yet exposed here. Direct process termination is not a graceful
preference-flush operation. No new gameplay entitlement comes from these flags.

Verification: editor and standalone Development builds passed. All 83 Unreal
tests passed (73 clean, 10 with existing warnings), including the new options
sync test. Rust suites passed 355 tests with four existing ignored tests. The
real UDP fixture verified native options/shortcut round trips, unknown flags,
height rejection, exact float/sentinel values, character isolation, immediate
options-only logout, server restart and twelve rapid alternating re-entry cycles.
The rendered connected HUD regression also passed; the native options panel
and expanded hotbar screenshot were visually inspected. Isolated test services
were shut down; the regular game server remains stopped.

Local artifacts:

- `Saved/game-options-editor-build.log`, `Saved/game-options-standalone-build.log`,
  `Saved/game-options-network-build.log`, `Saved/game-options-client-tests.log`.
- `Unreal/Saved/Automation/Run-6b3d93ff36cd4bdeb3190577f4b339d6/index.json`.
- `Saved/game-options-preferences-tests.log` and
  `Saved/AC2ShortcutTests/1eddf3facc8a44c797b78cdab949bf0a`.
- `Saved/game-options-render-tests.log` and
  `Saved/AC2NetworkTests/journal-ui-735635ec22d54fd394e154da68e922c9`, including
  the retained `game-options-panel.png` screenshot.

## Reliable return to character selection — 2026-11-10

Fixed the logout `Capacity` failure found during the hotbar pass. The old handler
sent `CharExitGame` directly and immediately changed phase before sending
`CharacterSet`. A full reliable window could reject the first response, or leave
the session in selection after only the first response had been admitted.
Removing the fixture's 2.5-second scene-settle delay reproduced the failure on
the previous binary (`Saved/logout-baseline-tests.log`).

The server now prepares the character list and flushes saved positions before
admitting one bounded pending logout. The departing character is removed from
gameplay once; pending item actions, attack requests, targeting, merchant state
and unsent scene output are cleared. Matching duplicate logout requests are
idempotent. Other application requests cannot cross the pending transition.
Native acknowledgments, echoes and retransmissions continue normally.

Once capacity is available, `CharExitGame` is sent on the departing world's
ordered queue. The pre-world unordered character list is sent only after exit
and preceding reliable traffic are acknowledged. Time-sync updates pause during
this short transition so they cannot keep the barrier open. Transport history,
checksums, sequence numbers and normal disconnect/retry limits are retained.
The bridge keeps selection controls closed until the fresh list arrives.

Evidence: the native `0x10027`/`0x10055` payloads and control-queue request are
documented in `AC2E/emulator/docs/world-evidence.md` (Admin `004c9d10`,
`004933b0`, `00494010`, `006db7f0`). The local Legions decompile dispatches world
exit and character-list notices separately through `FUN_0048c150` (`0x10405`)
and `FUN_0048c280` (`0x10402`). The acknowledgment barrier is an authored server
reliability policy, not a claim to have recovered the retail server scheduler.
No live retail-client verification is claimed.

Validation: release server and bridge builds passed, with 353 Rust tests passing
(320 server, 33 bridge) and four existing ignored tests. Targeted regressions
fill all 256 reliable slots, release only one slot, lose the first exit packet,
verify retransmission and acknowledgment gating, and reassemble a reordered
multi-fragment character list. They also cover invalid admission, duplicate
logout, leaving during entry, and blocking premature application requests.

The real UDP shortcut fixture passed its persistence/restart checks and 12
immediate alternating-character return/re-entry cycles with no settling delay.
The two-peer UDP suite passed observer removal, movement/chat/combat, inventory
restoration on re-entry, and server restart. No Unreal source changed in this
pass; the bridge uses the existing leaving/selection UI states. The regular
game server remains stopped.

Artifacts: `Saved/logout-network-build.log`, `Saved/logout-unit-tests.log`,
`Saved/logout-bridge-tests.log`, `Saved/logout-shortcut-tests.log`,
`Saved/AC2ShortcutTests/cf358a6d2c804f6496d648841640cc64`,
`Saved/logout-presence-tests.log`, and
`Saved/AC2NetworkTests/971c6358f83e4e03923b9f09700b93db/result.json`.

## Native hotbar preference round trip — 2026-11-10

Completed the existing server preference path through the Rust bridge and Unreal
HUD. Edits send `0x82120018` with the native `ShortcutInfo` graph (`0xAC`) followed
by a tagged slot ordinal. Login reads field 3 of `0x82120027`, the
`GameplayOptionsProfile` (`0x9B`), only when its shortcut-array bit `0x2` is present.
Inventory-only LOAD messages leave the bar untouched. Skills, item instances,
recipes and text/alias buttons retain all 100 slots; native enum commands are
preserved but do not gain speculative execution behavior.

Primary references: Legions `gmShortcutUI::SetShortcut` at `00437E70` (Admin
`00437620`), `gmCWInterface::SetShortcut` at `00452210` (Admin `004510D0`),
`ShortcutInfo::StreamPack` at `005C8FF0`, and the pinned reference codec's
`protocol/native/options.rs`. The server already decoded the native update but
the Unreal client neither sent it nor used the login profile. Validation is
against the installed DATs, decompiles and native protocol; no live retail
client session is claimed.

Migration imports an existing local bar once into a connection/character-scoped
cache, leaving the legacy INI section intact. Subsequent logins use the server
snapshot, including an empty array. The server now distinguishes uninitialized
and cleared bars. A first-write `h2` checkpoint preserves untouched racial starter
slots atomically; subsequent writes retain `h1`. Existing journals replay without
a conversion. The server's starter ordering now agrees with the established
client and tutorial: melee, magic, missile. These bindings do not grant abilities,
recipes or items.

Text limits align at 512 UTF-16 units without control characters, with a 16 KiB
whole-bar bound. Client validation occurs before sending any part of an oversized
edit. Shrinking writes precede growing writes to keep swaps inside the bound.
Only changed slots are transmitted; paging and resizing do not resend bindings.
Profile application waits until the matching character finishes world entry;
inventory cleanup cannot erase a bar while its profile is pending.

The UDP test exposed control-channel logout overtaking ordered shortcut writes.
The bridge now waits for a read-only native self-examination reply on the ordered
channel before issuing control-channel logout after edits. Earlier self-inspection
responses cannot satisfy the probe. Unrelated examination failures do not block
it, and missing confirmation cancels logout. This is an authored adapter guard;
the native request/response formats and transport remain unchanged. Direct
disconnect/process termination is not a graceful preference-flush operation.

Remaining scope: named alias definitions have no verified upload call in the
available references and remain local. Page and other unmapped UI preferences remain local. Height, lock and
number-display persistence are completed in the later game-options pass above. The rapid headless reconnect stress run
additionally hit the existing server's direct logout-response `Capacity` path
while the initial scene's reliable send window was full. The pass above resolves
that follow-up; the acceptance fixture no longer delays its next transition.

Validation: editor and standalone Development builds passed; all 82 Unreal tests
passed (72 clean, 10 with existing warnings). Rust suites passed 349 tests
(317 server, 32 bridge), with four existing ignored tests. The real UDP fixture
passed upload/restore, starter preservation, Unicode and 64-bit IDs, character
isolation, immediate edited-bar logout, all-clear and server-restart checks.
The rendered connected HUD regression also passed. The regular game server
remains stopped.

Local artifacts:

- `Saved/shortcut-editor-build.log`, `Saved/shortcut-standalone-build.log`,
  `Saved/shortcut-network-build.log`, `Saved/shortcut-client-tests.log`.
- `Unreal/Saved/Automation/Run-93428f4a86164d31ae3325ffc3413a64/index.json`.
- `Saved/AC2ShortcutTests/3bb792bbf78d40ba929df1faae09f2a9` and
  `Saved/shortcut-udp-tests.log`.
- `Saved/AC2NetworkTests/journal-ui-f5a12ef2b3bb4a359096696e0b01c341`,
  `Saved/shortcut-render-tests.log`, and the inspected HUD screenshot
  `Unreal/Saved/Screenshots/AC2Journal-40.png`.

## Native named aliases — 2026-11-10

Implemented `/alias ;name <replacement>`, `/alias add ;name <replacement>`,
`/alias remove ;name`, `/alias list`, `/alias clear`, `/alias clearlist`, and
`/alias shortcut ;name <1-10>`. Clearlist is the native synonym for clear.
The alias table is separate from text shortcut bindings, saved per character
under `AC2.Aliases.<id>` with canonical Base64-encoded names and values.
Malformed entries and attempts to override built-ins are discarded on load.

Retail's expander finds semicolons anywhere in text and consumes the following
alphanumeric characters. Lookup is case-insensitive; replacement case and
surrounding punctuation remain intact. Nested aliases expand recursively with
an active-name stack, leaving unknown/cyclic references literal. `/alias`
definitions are exempt from expansion. Hotbar alias bindings store the name,
so changing a definition or selecting a new target affects the next activation.
`/shortcut text` remains expandable, matching the shell's command registration.
Original input is retained in chat history, and each expanded line goes through
the normal local-command or network-chat path exactly once.

StrictAliasControl provides protected `;target` (selected object's current name)
and `;loc` (current map coordinates). Missing target/position values leave the
token unchanged. Both predefined aliases can be bound to shortcuts but cannot
be removed or overwritten; clear only removes user definitions. Alias commands
do not send public chat or gameplay messages. Ordinary shortcut focus, page,
drag-lock and persistence rules apply to alias buttons.

Primary evidence: Admin 7148 `gmCWInterface::ExpandText` (`004539E0`; Legions
`00454A60`) scans semicolon/alphanumeric tokens and calls `HandleAlias`
(`00453670`; Legions `004546F0`) for recursive expansion. `CPlayerShell::ExpandableText`
(`00732A90`) honors the non-expandable alias command; `CWInterface::ProcessText`
records history first. DAT CPlayer handler `1AA400B5`, body `3A1F4`, implements
the command forms, lowercases names, protects the strict alias table, and stores
the original alias name in `ShortcutInfo` for `/alias shortcut`. InqAlias
`1AA400E8`, body `2F500`, resolves strict aliases before the user table.
StrictAliasControl bodies `152DBC` and `152E1C` define and resolve the two
built-ins. StringTable `250007C8`, entries `09620F79` / `09620F7F`, supplies
the command reference. Focused extracts are in ignored `Saved/AliasReference/`.

Client limits are 256 definitions, 512 characters per name/replacement, a
2048-character expanded line, 32 nested expansions and 256 expansions per
submission. Over-limit expansions fail without sending a partial command.
These are defensive client limits, not claimed recovered retail limits.
Preferences remain local; native server profile replication is outstanding.
Verification uses installed DATs and decompiles, not a running retail client.
Confirmation/validation messages are authored English.
`/help alias` displays the native command reference from the DAT locally, with
an authored fallback if that resource is unavailable.

Validation: Editor and standalone Development builds pass. All 81 `AC2.Data`
tests pass (71 clean, 10 with existing warnings). The new alias regression
covers native command forms and help, protected aliases, embedded punctuation,
nested/cyclic references, expansion limits, Unicode persistence, invalid saved
entries, original-input history, live target/location changes, local commands,
bridge equivalence to normal chat/tells/emotes, hotbar rebinding, removal,
character isolation and clear/reload. The full rendered UI fixture passes;
its real keyboard path creates and activates `;wave` on the native hotbar.
The 1600x900 HUD capture was visually reviewed. No game server was started.

Artifacts: `Saved/aliases-editor-build.log`, `Saved/aliases-standalone-build.log`,
`Saved/aliases-client-tests.log`, `Saved/aliases-render.log`,
`Unreal/Saved/Automation/Run-002ae26757a74d809d43f1b47fb4e11e/index.json`, and
`Unreal/Saved/Screenshots/AC2UI-aliases.png`.

## Native shortcut number option — 2026-11-10

Options > Game (the native `CharacterPage`) now exposes **See Numbers on Shortcuts**, using the DAT
label (`41E19E90`) and help (`98367AF5`) from StringTable `25000012`. Its native
default is enabled. Apply, Revert and Defaults follow the existing options
transaction. `/shortcut numbers on|off` changes the same preference immediately,
synchronizing the draft checkbox without applying unrelated option edits. Bad
arguments leave the preference and bindings unchanged. This is a local UI
command and sends no public chat or server gameplay command.

The setting governs the small `NumberDisplay` labels on empty slots and
`SetNumberDisplay`, including expanded rows and the currently visible row while
scrolling. The first row now receives its native corner labels too; the prior
implementation suppressed them because bitmap `41008D29` already contains large
digits. Those fixed digits are separate artwork and remain visible with this
option disabled, as in the DAT layout. Occupied slots retain their icons/text;
padding after binding 100 has neither number labels nor input targets. Changing
the option does not change bindings, page, row count or an active page scroll.

Primary evidence: Legions `gmShortcutUI::MakeElement` (`00437B70`) clears the
number text and fills it for empty shortcuts when `_showNumbers` is enabled;
`UpdateSetNumber` (`00437190`) clears the page label when disabled. Native
`gmCharacterUI` defines `ID_Option_ShortcutNumbersText` with default 1 and routes
its changes to `SetShowShortcutNumbers`. CPlayer's `/shortcut` handler
(`1AA400BA`, body `3CFBC`) calls that same setter for its numbers branch, then
requests a UI refresh. The retained setter/getter (`B40073` / `B40074`) use the
GameplayOptionsProfile number flag. Focused extracts are in ignored
`Saved/AliasReference/`. Verification uses DATs and decompiles, not a running
original retail client. Preferences use our existing local `AC2.Options`
section; native server-side profile replication is still outstanding.

Validation: Editor and standalone Development builds pass. All 80 `AC2.Data`
tests pass (70 clean, 10 with existing warnings). Coverage includes native
label/help resolution, checkbox draft/Apply/Revert/Defaults, saved option
reload, unrelated draft preservation, malformed commands, bridge silence,
expanded-row numbering, page-ten padding and changes during smooth scrolling.
The full rendered UI fixture passes, including actual pointer clicks on the
native checkbox and Apply button. Both on/off screenshots were visually
reviewed at 1280x720. The initial render exposed an uninitialized options array
in the added fixture after returning from character selection; the fixture now
loads options before touching its draft, and the final rerun passes. No game
server was started.

Artifacts: `Saved/shortcut-numbers-editor-build.log`,
`Saved/shortcut-numbers-standalone-build.log`,
`Saved/shortcut-numbers-client-tests.log`, `Saved/shortcut-numbers-render.log`,
`Unreal/Saved/Automation/Run-e5bd119b87904cacb1bccab77bdbb8fb/index.json`,
`Unreal/Saved/Screenshots/AC2UI-shortcut-numbers-on.png` and
`Unreal/Saved/Screenshots/AC2UI-shortcut-numbers-off.png`.

## Native text shortcuts — 2026-11-10

The hotbar now supports `/shortcut text <1-10> <message or command>`, including
the native abbreviated `/shortcut <1-10> <text>` form. Locations are one-based
within the currently visible top row, including during page animation. A
command replaces that location without sending its payload. Quoted words are
parsed and joined as text; malformed locations, incomplete quotes, controls,
empty text and payloads exceeding the client's 512-character limit leave the
existing binding intact.

Clicking or pressing the binding submits it through the existing chat path.
Local commands remain local; network chat, tells, emotes and supported gameplay
commands retain their existing validation. The native AliasText node supplies
the icon, outlined single-line label and slot clipping. Tooltips show the full
payload. Text has no item/skill examination action. Moving, overwriting,
removing, expanded rows, page-ten padding and the shortcut lock use the same
binding paths as skills and items. Text is UTF-8 Base64 in the existing local
slot list, preserving commas, quotes and Unicode without splitting slots.
Existing skill/item/recipe storage remains compatible.

Primary evidence: installed StringTable `250007C8` help entries `5F4899FA` and
`5F48A9FA`; CPlayer handler `1AA400BA`, body `3CFBC`, subtracts one from the
location, bounds it to the first ten slots and adds the current set's offset.
It creates ShortcutInfo type `40000003`. CPlayer `UseShortcut` (`B40007`, body
`2F5EC`) obtains that string and calls `CWInterface::ProcessText` (`7F0052`).
Admin 7148 `gmShortcutUI::MakeElement` (`00437320`) populates `AliasText` and
uses its `icon` attribute; DAT layout `22000001` supplies bitmap `4100159A`.
Reference extracts/listings are under ignored `Saved/AliasReference/`.

This pass implemented literal text shortcuts; named aliases and the number
option are covered in subsequent passes above. Shortcut preferences
remain local per-character settings; server replication is still outstanding.
Verification uses installed retail data and decompiles, not a running original
retail client. Confirmation and validation messages are authored English.

Validation: Editor and standalone Development builds pass. All 80 `AC2.Data`
tests pass (70 clean, 10 with existing warnings). The new UI integration test
checks real pointer routing, bridge command equality with ordinary chat,
editing-focus protection, locked/unlocked drag behavior, exact persistence,
invalid replacement, local commands, page animation and character isolation.
The full rendered UI fixture also passes, exercising text creation and keyboard
activation and leaving three native text bindings visible for screenshot review.
The HUD capture was visually checked at 1600x900. No game server was started.

Artifacts: `Saved/text-shortcuts-editor-build.log`,
`Saved/text-shortcuts-standalone-build.log`,
`Saved/text-shortcuts-client-tests.log`, `Saved/text-shortcuts-render.log`,
`Unreal/Saved/Automation/Run-fd2262d93b5f4efc8e52d7a1fca1abfe/index.json` and
`Unreal/Saved/Screenshots/AC2UI-world-hud.png`.

## Support casting from peace mode — 2026-11-10

Animated support spells previously applied their effect and sent a cast story
while the caster remained in peace mode. Ease Suffering (`55`, behavior
`410000C2`) has no peace-mode combo in the installed human behavior table
`44000001`; its combos exist for one-hand, unarmed, missile and magic stances.
The rendered cooldown fixture had been working around this by entering combat
before casting.

The server now prepares an equipped-weapon stance for accepted animated self
and player-targeted support casts. It validates space for the owner's state
notice and mode delivery before spending resources, queues the private combat
state and publishes native DoMode before the cast PhysicsStory. Visual stamps
advance in that order for the caster and nearby observers. Selection is kept,
and entering casting stance does not enqueue a weapon attack. Already-combat
casters do not receive a redundant mode transition. Rejected casts and effects
with no behavior do not change stance.

This fills the missing stance sequencing using the installed animation combos
and existing native DoMode/PhysicsStory messages. It does not recover the full
retail server scheduler, cast interruption/refund policy or temporary stance
restoration rules. The renderer fixture now starts its heal in peace mode and
requires the real server transition before checking the native active artwork.

Regression coverage includes preparation without mutation, queue admission,
selection preservation, mode/story stamp order, and no-mode/repeated-mode
cases. Live two-client checks verify mode-before-story for a targeted heal,
peace-mode preservation on cooldown rejection, self-healing with scenery still
selected, and cast/release/recipient FX order.

Validation: server release, editor and standalone Development builds succeeded.
All 314 server tests passed (four explicit skips); all 79 `AC2.Data` tests
passed (69 clean and ten with warnings), including the native support-combo
check. The two-client support and Flame Bolt UDP fixtures passed. The ten-stage
rendered cooldown fixture passed through the existing 500 ms return-latency
proxy, now casting its heal from peace; before/cast captures were visually
reviewed. Logs: `Saved/support-stance-server-build.log`,
`Saved/support-stance-client-tests.log`, `Saved/support-stance-render.log`,
and `Saved/AC2NetworkTests/cooldown-ui-43d7e7a4470e4296aaeb0e4f9105983c/`.
Disposable test servers, clients and proxy were stopped afterward.

## Server cleanup and attack cancellation review — 2026-11-10

Reviewed server request dispatch, encounter targeting, item-action transitions,
transport retransmission entry points, persistence helpers and call sites across
the repository. Removed eight functions with no callers: `PreparedCast::ready_at`,
`combat_visual::basic_attack`, `groups::Runtime::member_updates`, `preferences::options_words`,
`starter_gear::is_sword`, `Store::grant_training_kit`,
`Transport::retransmit_requested` and `ui::hand_equipment`. The live generic
implementations remain. Historical `t1`/`t2` inventory journal replay is retained;
removing the obsolete human-only grant writer does not migrate or discard saves.
The unreachable second handling of loot pickup and a redundant combat binding
were also removed.

Target changes previously could leave attacks running against the old creature
when the new one was out of range, or replace a queued skill without completing
its client request. Re-selecting a target could turn a one-shot spell into a
repeating attack. Selection updates now admit the status and cancellation
messages together before changing state; a repeated selection only refreshes
status. Item animations and corpse transmutation now cancel both the server
attack and its pending request. Basic attack/stance input also removes a queued
special strike when completing that request. Completion must enter the outgoing
queue before the request is forgotten; subsequent item-animation admission
failure cannot leave that cancelled attack running.

Nine regression tests exercise request counter zero, duplicate cancellation,
full-queue retry, same-target refresh, out-of-range target changes, deselection,
invalid/inadmissible selections, and accepted/rejected item actions. The release
build and server suite pass with installed DAT data: 311 tests passed, four
explicitly ignored. Real UDP checks also pass for Flame Bolt and its request
correlation; item drop/pickup animations, competing ownership and restart
recovery; and the starter combat/quest/exit circuit, death and resurrection.
This is a scoped cleanup and correctness pass, not a claim
that all server features or retail behavior have been audited completely.

## Active attack stories and shortcut highlighting — 2026-11-10

Accepted skills now retain the actual request counter and maneuver in the
native PhysicsStory stream. The client replaces the pending hourglass with
the DAT active artwork only after the matching behavior successfully starts.
It tracks a unique action instance, so natural completion, an explicit stop,
same-behavior replacement, death, water entry and model rebuilds release the
marker without guessing a duration. A reaction on another animation layer
does not end that action. Owner and object-instance checks prevent foreign or
stale stories from activating local shortcuts; wrapped request counter zero
remains distinct from an absent counter.

Duplicate bindings and other pages share the active state. Pending and active
shortcuts remain protected from dragging. A rejected repeat finishes only its
own request. No-animation stories finish immediately, and failed behavior
playback clears its request instead of leaving a stuck hourglass. Server
rejection/cancellation notices remain explicit; successful attacks no longer
send completion at the moment their animation starts. Cooldown, damage and
ability-acceptance policy are unchanged. Self-heal health stories precede the
cast story, with cast, release and recipient FX retaining their normal order.

Primary evidence: Legions 7160 `StoryManager::DoStory` (`005033C0`),
`CleanupStory` (`00502BA0`) and `HandleBehaviorDone` (`00503500`), plus admin
7148 `gmShortcutUI::RecvNotice_StartSpecialAttack` (`00437770`). Installed
layout `22000001` supplies active bitmap `41005B1B`. The native story flag
`0x08` carries the client attack context; `0x10` carries the maneuver. These
are existing native fields, not a new wire message.

Validation: editor and standalone Development builds succeeded; all 78
`AC2.Data` checks passed (68 clean, 10 with warnings), all 24 bridge tests
passed, and the server unit/integration suite passed. Real UDP tests confirmed
Flame Bolt request correlation, self/target healing and effect order,
Lightning Bolt vigor ticks, casting with looted weapons, and the two-client
combat/death/resurrection/restart circuit. The ten-stage rendered fixture passed through
a 500 ms return-latency proxy, verifying pending → active → cleared artwork,
duplicate/paged bindings, rejection isolation and independent cooldown expiry.

Local results: `Saved/active-story-client-build.log`,
`Saved/active-story-client-tests.log`, `Saved/active-story-bridge-tests.log`,
`Saved/active-story-server-build.log`,
`Saved/AC2NetworkTests/active-skills-77866030f038443f98e50c3649beb83f/`,
`Saved/AC2NetworkTests/support-skills-aea3955f79ed4197a2af2cbdd0b2e3b7/`,
`Saved/AC2NetworkTests/weapon-loot-f21c35d99eb54c93aa3f4ed3c3ac8483/` and
`Saved/AC2NetworkTests/cooldown-ui-4849d4fc8ab445fbab59498d95fd2dcf/`.
Additional runs are logged in `Saved/active-story-live-gameplay.log` and
`Saved/active-story-live-vigor-loot.log`. Regression fixtures now allow time
for retaliation through native armor, move inside melee reach, await the
Outcast's delayed spawn, and place the Tumerok spell test within its own
dungeon on native ground with verified sight lines. Rendered captures of the
hourglass, active highlight and subsequent cooldown-only state were visually
reviewed. All disposable clients, servers and proxies were stopped afterward.

The support-casting pass above resolves the peace-mode animation boundary
identified here. Full retail interruption/refund policy and toggle-ability
gameplay remain separate work.

## Requested attack and toggle indicators — 2026-11-10

The network bridge now publishes the actual special-attack request counter
after successfully sending a skill or Recall. The hotbar uses the native
pending artwork for every binding of that skill, including other pages and
partially visible rows. Completion/rejection removes only its matching
counter; a rejected repeat cannot clear another outstanding request. Pending
shortcuts cannot start a drag even with Shift or unlocked shortcuts, and the
drop path rechecks requests that arrive during a drag. Incoming shortcut
assignments remain permitted. These markers do not change cooldown timestamps
or grant successful skill use.

Native `WM_Combat` `0x82040028` toggle notices now reach the UI instead of being
discarded. Their wire order is flag, then maneuver. When no request remains,
the skill displays the native toggled artwork if that flag is set. Death and
world teardown clear transient request/toggle state. Rendering continues to
use the existing clipped DAT template rather than adding custom icon frames.

Primary evidence: admin 7148 `gmShortcutUI::RecvNotice_RequestSpecialAttack`
(`004376C0`), `RecvNotice_EndSpecialAttack` (`00437820`) and
`RecvNotice_ToggleSpecialAttack` (`00436330`). The installed Legions layout
`22000001` supplies pending bitmap `41001710` and toggled bitmap `41007F72`.
The request is a local native notice, separate from server acceptance; the
bridge JSON event mirrors that notice and adds no wire protocol.

This pass initially left active highlighting as a separate gap because server
stories omitted the attack context and sent completion when a swing started.
The subsequent active-story implementation above closes that presentation gap.

Validation: editor and standalone Development builds succeeded; all 78
`AC2.Data` checks passed (68 clean, 10 with warnings), along with all 24 bridge
tests. The real UDP Flame Bolt test checks request counters on accepted and
rejected casts. The rendered cooldown fixture now runs ten stages through a
500 ms return-latency proxy on both native UDP ports, checking duplicate and
paged pending markers, accepted/rejected cleanup, moving cooldowns and expiry.
The captured native hourglass and its removal were visually reviewed. Toggle
wire decoding and native artwork are covered by bridge/UI tests; this pass
does not add server gameplay support for toggle abilities.

Local results: `Saved/attack-indicator-build.log`,
`Saved/attack-indicator-bridge-tests.log`,
`Saved/attack-indicator-data-tests.log`,
`Saved/AC2NetworkTests/active-skills-3158dbf4c60f4457b223599df35018e6/` and
`Saved/AC2NetworkTests/cooldown-ui-785c2ff0ad0c4f65aba25ff6006c39a3/`.
The test proxy and disposable server were stopped after validation.

## Timed skill cooldown displays — 2026-11-09

Accepted casts now send the native SkillInfo last-use timestamp and shared
recovery qualities. The hotbar animates the existing DAT crosshatched
`TimeLeftMeter` against synchronized server time, using the skill's authored
reset duration. It chooses the skill or shared recovery timer with more time
remaining and uses that timer's duration for the fill. Duplicate shortcuts,
other pages and bindings moved during recovery show the same countdown.
Equipment incompatibility keeps the full shade while the timer expires
underneath it. Tooltips expose the remaining seconds.

Rejected requests and `skill_finished` notices do not start or restart a timer.
Skill advancement and full skill-list refreshes preserve the last-use stamp;
re-entering the world restores both per-skill and shared recovery state.
Timestamps are recorded only after the authoritative ability commit. They
remain session state, matching the existing ability runtime: restarting the
server clears cooldowns. The existing one-second global recovery policy is
unchanged. Requested and active attack markers were subsequently added above.
Item cooldowns and complete retail cast-interruption/refund rules remain
separate work.

Primary evidence: Legions 7160 `gmShortcutUI::CheckSkill` (`00436FF0`) and
`UpdateTimeLeft` (`004369D0`), corroborated by admin 7148 (`004367A0`,
`00436180`). `gmCWInterface::InqSkillTimeLeft` dispatches script interface
`0xB4002F`; CPlayer's inherited slot `0x2c4`, body `0x1e14`, delegates to
bytecode `0x114260`. This reads SkillInfo flag `0x20`, TIME slots 0–1, skill
root field 34, shared timestamp quality `0x12f` and float duration `0xbb8`.
CPlayer quality-change handler `0x31b58` calls native
`CM_Character::SendNotice_UpdateResetTime` (`0x00850007`) for that timestamp.
The duration is sent before the timestamp notification. DAT layout `22000001`
supplies the bottom-fill meter and bitmap `410016EA`. RetailProbe now exposes
inherited method listings and byte-offset inspection for this evidence.

This verifies the recovered format and UI behavior against available retail
files, not a running retail client. Retail bytecode also has a shared-reset
exception whose full gameplay policy has not been recovered; the displayed
shared timer currently follows this server's enforced policy. The remaining
seconds tooltip is authored English presentation text.

Validation: Editor and standalone Development builds, release server and
release bridge builds pass. All 78 `AC2.Data` tests pass (68 success, 10 with
existing warnings); server library tests pass 262 with one existing ignored
test; bridge tests pass 23. The real UDP cooldown scenario checks accepted and
rejected casts, advancement and re-entry. Its six rendered stages check
duplicate bindings, decreasing fill, paging and expiry; captures were visually
reviewed. The Flame Bolt integration test also passes weapon rejection,
accepted native reset delivery, release/projectile effects and burn ticks.
Its isolated fixture now uses two validated nearby placement points instead
of overlapping the growing recovered starter population.

Artifacts: `Saved/cooldown-build.log`, `Saved/cooldown-data-tests.log`,
`Saved/cooldown-server-tests.log`, `Saved/cooldown-bridge-tests.log`,
`Saved/cooldown-render-test.log`, `Saved/cooldown-active-skills.log`,
`Unreal/Saved/Automation/Run-f99dc523178a4c6f84b5d352f5c4b1ea/index.json`,
`Saved/AC2NetworkTests/cooldown-ui-9016c1d6c21e4c198d75bf40099d414c/` and
`Saved/AC2NetworkTests/active-skills-31bfff49bf2b4b4ebef4972df975c3c6/`.

## Skill equipment feedback — 2026-11-09

Learned skill shortcuts now show the DAT's native full equipment shade when
the confirmed main-hand or off-hand implement fails the skill's requirement.
Changing equipment refreshes the shade and requirement text in hotbar tooltips,
skill-book tooltips and the skill examine panel. A skill remains trained and
can still be bound, moved or examined with incompatible equipment. Activation
continues to the existing server validation; this is presentation feedback,
not a new grant or a client-side substitute for casting rules.

Skill root fields 30 and 49 provide the right/left masks. Item implement types
come from `CBaseQualities` integer quality `0xFA0`, after the bounded
`gmWeenieDesc` payload. Names cover the installed `23000003` implement enum,
including combined masks. A worn snapshot whose object has not arrived, a
missing quality and a disconnected inventory remain unknown; none is treated
as an empty hand. Actual empty hands use enum bit 1. Native weapon category
and implement type are distinct: the Lugian starter missile's authored
implement is Hammer (`0x100`). Passive mastery records have no hand masks.

Primary evidence: Legions 7160 `gmShortcutUI::CheckSkill` (`00436FF0`, admin
`004367A0`) calls `IsSkillUsableBasedOnInventoryByEnum` and fills `TimeLeftMeter`
when unusable; inventory notices recheck skills. `UIElement_Meter::UpdateLayout`
(`005B5780`, admin `005F4C50`) supplies top/bottom moving fills. Installed layout
`22000001` supplies the bottom-fill overlay `410016EA`; it is rendered above
the icon inside the slot clip. Skill offsets follow the recovered Pyreal
`SkillCollection` schema already used by the server's effect importer; hand
mask checks agree with `abilities.rs`. `gmWeenieDesc::StreamPack` (admin
`006037C0`) supplies the descriptor field order.

This pass does not start a cooldown on a local click or on `skill_finished`:
the latter is also emitted for rejected casts. Authoritative reset-time
delivery and pending markers were added in the subsequent passes above;
active attack states still need implementation. The
requirement sentences are English presentation text, not recovered retail
localized phrasing.

Validation: Editor and standalone Development builds pass. All 77 `AC2.Data`
tests pass (67 success, 10 with existing warnings, zero failures). New tests
cover six real DAT starter implements, both hands, alternate implement masks,
inventory-before-create ordering, missing metadata, live tooltips/examination,
passive records, disconnects and clipped vertical meter fills. The rendered
UDP journal scenario passes all 42 stages; stages 16–17 verify shade changes
against actual bow equip/unequip acknowledgements and were visually reviewed.
Artifacts: `Saved/skill-equipment-build.log`,
`Saved/skill-equipment-data-tests.log`, `Saved/skill-equipment-render-test.log`,
`Unreal/Saved/Automation/Run-7efe6081798e4f4ab85b992d32999879/index.json` and
`Saved/AC2NetworkTests/journal-ui-d6267d769bc44a599c5da1bb09fe4b1e/`.

## Smooth and instant shortcut paging — 2026-11-09

Options → Game now exposes the localized **Instantly Swap Shortcut Bars**
checkbox with retail's enabled default and the existing local persistence,
draft, Apply, Revert and Defaults behavior. With it disabled, changing pages
scrolls the contents inside the fixed native frame. Duration comes from the
installed ShortcutListBox's `smooth_movement_duration` (0.25 seconds). A new
destination during motion continues from the current view and finishes over
half the configured duration. Applying instant scrolling or restoring its
default settles the transition immediately, including when the bar is hidden.
Loading another character's saved shortcuts also settles at that saved page.

Edge rows render and hit-test only within the list's clip. Pointer use,
examination and incoming/outgoing drags resolve the displayed absolute binding,
including the extra partial row. A held click cannot activate a different
binding that scrolls underneath it. Keyboard activation, relative keyboard
paging, quick-add and the page label use the top visible row. Mouse arrows
retarget the pending destination. Existing end padding after binding 100 stays
disabled and page wrap scrolls through the intervening rows.

Primary evidence: admin 7148 `UIElement_ListBox::ScrollToRow` (`005F1BF0`),
`StartAnimation` (`005F23F0`) and `Global_Loop` (`005F3320`), corroborated by
Legions 7160 (`005B2720`, `005B2F20`, `005B3E50`); admin
`gmShortcutUI::ListenToMessage` (`004379A0`, `00438270`; Legions `004381F0`,
`00438AC0`) for view offsets and key actions. `gmCharacterUI::Setup` supplies
the enabled default. DAT layout `22000001` supplies the duration, and string
table `25000012` supplies `ID_Option_ShortcutInstaScroll`. The decompile loses
the floating-point expression passed to `_ftol2` in `Global_Loop`; this client
uses linear interpolation with native integer view offsets. The exact retail
easing curve has not been verified from machine code or a running retail client.

Validation: Editor and standalone Development builds pass. All 75 `AC2.Data`
tests pass (65 success, 10 with existing warnings, zero failures). Coverage
includes clipped edge hits, click/examine/drag binding identity, held-click
cancellation, visible-row keyboard paging, interrupted/reversed transitions,
page wrap, end padding, profile reload and enabling instant mode while hidden.
The rendered UDP journal scenario passes all 42 stages; stages 37, 38 and 40
were visually reviewed for the localized checkbox, clipping during motion and
the settled final page. Test changes to options and shortcuts are restored;
the isolated server and client stop afterward. Artifacts:
`Saved/shortcut-scroll-build.log`, `Saved/shortcut-scroll-data-tests.log`,
`Saved/shortcut-scroll-render-test.log`,
`Unreal/Saved/Automation/Run-55ff7d7b2f7f4abfa7f62a7e7f033567/index.json`, and
`Saved/AC2NetworkTests/journal-ui-faf16828f76044888ed07bcb97c8e3a3/`.

Remaining shortcut gaps include the native number-display option, chat aliases,
cooldown overlays and server-side preference replication. The first-row DAT
frame still contains baked numeral artwork; this pass does not replace it.

## Shortcut editing lock and skill-book quick-add — 2026-11-09

Options → Game now exposes the native localized **Lock Shortcuts** checkbox,
defaulting on. It participates in tab drafts, Apply, confirmed Revert/Defaults
and existing local preference persistence. The lock gates outbound shortcut
drags, with Shift sampled as the drag begins. Releasing Shift after starting
the drag does not cancel it. Incoming assignments from the book, inventory and
crafting list remain allowed, including replacing a destination, matching the
separate native drag-in callback. Activation, examination, paging and resizing
remain available. A blocked drag cannot lift the icon, remove a reference,
drop an inventory item or become an activation when returned to its source.
Capture loss cancels the gesture without changing bindings.

Double-clicking a trained active skill adds it to the first empty slot in the
currently selected ten-slot set. It does not cast, replace occupied bindings or
spill into another set when multiple rows are visible. Full sets, passive,
unlearned and untraining skills do not add a shortcut. Tooltips explain quick-add
and the Shift override. Existing hotbar activation remains the casting path.

Primary evidence: admin 7148 `gmShortcutUI_DOCB` (`00435E70`) checks
`GetShortcutLocked` and `GetKeyState(0x10)` before drag-out; `gmShortcutUI_DDCB`
(`00435E10`, Legions `00436660`) validates drag-in without the lock. Admin
`gmShortcutUI::ListenToMessage` (`004379A0`, Legions `004381F0`) handles the
draggable SkillNodeIcon double-click (`40000007`) by searching offset through
offset+10 for an empty slot. `gmCharacterUI::Setup` supplies the true default,
and installed string table `25000012` supplies `ID_Option_ShortcutLocked`.

Validation: both Editor and standalone Development builds pass; all 75
`AC2.Data` tests pass (65 success, 10 with existing warnings, zero failures).
The rendered UDP journal scenario passes through stage 36. Its hotbar checks
exercise locked drags, Shift movement and skill-book quick-add; stages 34–35
were visually reviewed for checkbox placement, clipping and checked/unchecked
feedback. The item-shortcut test also verifies blocked drag return-to-source,
world-drop prevention, releasing Shift before drop, unlocked movement, first
empty/full-page assignment and unlearned/untraining guards. Test preferences
and shortcuts are restored, and the disposable server is stopped afterward.
Artifacts: `Saved/shortcut-editing-build.log`,
`Saved/shortcut-editing-data-tests.log`, `Saved/shortcut-editing-render-test.log`,
`Unreal/Saved/Automation/Run-b2c173cada614a519d5dbfa90bf2ef99/index.json`, and
`Saved/AC2NetworkTests/journal-ui-9808c85b109c49269486c4060b37cb85/`.

Remaining shortcut gaps at that pass included the native number-display option,
animated page scrolling (addressed above), chat aliases, cooldown overlays and
server-side preference replication. This pass does not alter the DAT frame's
baked numeral artwork.

## Radar filters and native contact presentation — 2026-11-09

Character Options exposes the six retail mask bits (monsters 1, NPCs 2, players
4, portals 8, lifestones 16, items 32) and coordinate visibility, using the
installed localized labels. The default mask is 31. These settings participate
in per-tab drafts, Apply, Revert and Defaults. The radar coordinate button is an
immediate change to the same saved local setting; it preserves unrelated drafts.
This is local preference persistence, not server character-profile replication.

`FAC2RadarData` resolves native package inheritance from the installed WLib
export metadata (`56000005`), then follows EntityDesc bases and typed property
overrides. It does not infer categories from names, model IDs or the server's
content object namespace. Supported template families are MonsterTemplate,
NPCTemplate, CPlayer, Portal, Lifestone, CItem and GameplayContainer. Unknown or
malformed definitions remain unclassified. Authored RadarBlip, RadarColor and
Item_IsSelectable overrides are preserved, including NoBlip. Live visual
qualities for shape, color and selectability override the DAT values, with
instance-stamp checks. The emulator's retained monster entities switch to the
item category at zero health, preserving nonselectability and NoBlip.

Base symbols now match retail: red X monsters, yellow edge NPCs, white filled
player squares, purple hollow portals, blue L-shaped lifestones and white item
points. All ten native pixel shapes and the selected outline are supported.
The filtered contact list drives drawing, tooltip and radar click selection;
filtering never clears the selected world object or disables world targeting.
Native radar range, heading and map behavior are unchanged.

Evidence: Legions 7160 `gmRadarUI::DrawBlip` (`004258E0`), `DrawL`
(`00424F00`), `DrawEdges` (`00424F70`), `DrawSelected` and `Update`
(`004259F0`); `gmCharacterUI` radar mask/coordinate callbacks; and installed
`InqRadarLook` virtual slot 454 in NPCTemplate (3455808), MonsterTemplate
(3377408), CPlayer (268896), Portal (653684), Lifestone (650716), CItem
(619916) and GameplayContainer (636456). Body offsets here are decimal offsets
in the installed script stream. The options and radar geometry come from
`22000001` and `22000005`; strings come from `25000012`.

Validation covers class inheritance, custom colors/shapes, NoBlip, unknown and
truncated data, native pixel patterns, live quality updates and stale stamps,
draft versus applied filters, coordinate synchronization and Defaults. The
rendered journal scenario additionally clicks the radar controls through Slate,
filters real network NPCs and checks that the world target remains selected.
Both Editor and standalone Development builds pass. All 75 `AC2.Data` tests
pass (65 success, 10 success with existing warnings, zero failures). The rendered
UDP journal scenario passes through stage 33; stages 30–32 were visually
reviewed for labels, list clipping, filter feedback and radar expansion.
Local artifacts: `Saved/radar-build.log`, `Saved/radar-data-tests.log`,
`Saved/radar-render-test.log`,
`Unreal/Saved/Automation/Run-00fc133785d840e9b6b6ab5b2c11091b/index.json`, and
`Saved/AC2NetworkTests/journal-ui-d0e3d68a38a84456a9e9d4cc8858d3d2/`.

Remaining radar gaps: complete script execution for per-object overrides,
relative-level/hostility/fellowship/PvP color variants, owned-corpse tinting,
server-persisted character options and the complete native options tree.

## Options scrolling and confirmation behavior — 2026-11-09

The supported options now use the installed `22000001` page/list geometry and
33-pixel `OptionSlider` / `OptionCheckbox` rows. The scroll range comes from the
content height and native list height; wheel, arrow, thumb and track input share
one offset. Partially visible controls are clipped for both drawing and input.
Short pages and list endpoints disable the corresponding arrows. Repeated row
templates restrict hover/press artwork to the actual row, and selected-tab
artwork is prepared before constructing the panel.

Draft values and applied values are separate per tab. Apply and Revert are
disabled until that tab changes. Revert and Default require confirmation; Default
applies that tab's defaults immediately. Chat Apply also confirms. Explicitly
closing options with pending edits asks whether to apply all tabs or discard
their drafts. Temporarily covering options with another left-side panel preserves
drafts when reopened. Confirmation input is modal, including controller hotkeys,
camera gestures, text entry and mouse wheel input.

Retail evidence: Legions 7160 `gmClientUI::Changed` (`00447410`),
`UpdateButtons` (`004475B0`), `Apply` (`00447730`), `Recall` (`00447820`),
`Default` (`00447850`) and `ListenToMessage` (`00447A80`);
`gmCharacterUI::UpdateButtons` (`00446340`), `Apply` (`00446430`),
`Recall` (`00446520`) and `Default` (`00446550`);
`gmChatOptionsUI::ConfirmApply/Recall/Default` (`00444670`, `004446D0`,
`00444730`); and `gmOptionUI::Show` / `DoConfirm` (`004354E0`, `004352C0`).
The confirmation uses the native `ConfirmationField`, `QuestionText`, `YesButton`
and `NoButton` from `22000005`, matching `ConfirmationDialog` (`0042D5C0`–
`0042D620`). Text comes from `25000012` (`ID_Option_ApplyText`,
`ID_Option_RecallText`, `ID_Option_DefaultText`). The close prompt reuses the
native Apply question; the exact close-specific string token remains unresolved.

Both Windows Editor and standalone Development targets build. All 73 `AC2.Data`
tests pass (63 success, 10 success with existing warnings, zero failures).
Validation covers installed DAT geometry at 1024×768, 1600×900 and 2560×1440,
scroll endpoints, clipping and drag release, dirty button state, per-tab apply,
confirmation cancellation, defaults and apply/discard on close. The controller
key test also checks modal shortcut suppression. The rendered UDP journal test
exercises options and native confirmation buttons through Slate and captures the
top/bottom of the list and each dialog for visual review. Test settings use an
in-memory mode so the user's saved options and graphics configuration remain
unchanged.

Remaining work: the complete retail option catalogue, bindings editor, native
chat routing/filter configuration, server-persisted character options, graphics
mode confirmation/rollback, logout/quit confirmation, and confirmation when
options is implicitly covered by another panel. Current graphics settings use
Unreal equivalents; this does not reproduce the original renderer's settings.

## Native hotbar keyboard controls — 2026-11-09

Hotbar keyboard bindings now come from the installed default `CKeyMap` records.
The loader resolves `KEYMAP_GROUP_ENUM` (`4000000F`) through `28000000`, then
resolves the main/control/shift enums (`41000001`–`41000003`). In the installed
Legions DAT these resolve through `28000010` to `1D000000`, `1D000004` and
`1D000005`. It validates resource identity, packed string boundaries, counts and
record length before installing any bindings; unrelated movement/camera actions
are left to the existing controls. Unsupported shortcut semantics fail decoding
instead of silently becoming an unrelated key.

Retail evidence: Legions 7160 `gmClient::InitKeymap` (`00403C80`),
`CKeyMap::StreamPack` (`00567270`) and
`gmShortcutUI::ListenToMessage` (`00438AC0`, admin 7148 `00438270`). Actions
`4100002B`–`41000034` activate the first visible row; `41000035`–`4100003E`
select pages 1–10. `4100003F`/`41000040` advance/reverse with wraparound, and
`4100005F`–`41000068` activate the second row only when visible and below index
100. The installed default maps bind these to 1–0, Ctrl+1–0, minus/Shift+minus
and Shift+1–0 respectively. Both left and right Ctrl/Shift work, matching the
native modifier folding. Combined Ctrl+Shift has no default binding; Alt/Command
combinations are also suppressed to avoid accidental activation of plain keys.

Mouse arrows and keyboard paging share one save/rebuild path. Activation retains
the existing item identity, learned skill and granted recipe checks. Text entry
focus and frontend/character selection suppress both casting and page changes;
auto-repeat does not repeatedly activate a shortcut. Keyboard page selection
uses the existing per-character local settings.

Validation: both Windows Editor and standalone Development targets build.
All 72 `AC2.Data` tests pass (62 success, 10 success with existing warnings,
zero failed). The new keyboard test decodes installed maps, rejects truncated or
misidentified records, and drives the controller's input stack to verify every
page, wraparound, left/right modifiers, second-row visibility, end padding,
repeat suppression, text/frontend guards and saved page selection. The rendered
UDP journal test routes keys through Slate and the controller, verifies chat and
fellowship-name focus guards, and receives server acknowledgement for second-row
bow equip/unequip. Captured page-nine geometry, icon and padding were inspected.
Custom keymap editing, server-side shortcut/profile persistence, aliases,
cooldown overlays and animated page scrolling remain separate gaps.

## Resizable native hotbar — 2026-11-09

The shortcut bar now supports one through five visible rows. Dragging its top
border snaps to whole rows while keeping the bottom edge and native slot size
fixed. The root, referenced list, clipping and hit targets resize together using
the DAT's `border_size` and `row_height` attributes. Its full-area border no
longer conflicts with dragging a shortcut. Crafting, vendor and side panels
cover the resize grip when they overlap it.
Hover and press feedback is resolved for each slot instance, so clicking one
cell cannot highlight or depress every copy of the shared native template.

All rows use the existing skill, item and recipe activation, examination and
drag paths. A visible row corresponds to the next ten bindings; arrows still
advance by one page, wrapping between pages one and ten. Rows beyond binding
100 are disabled padding. Collapsing the bar retains hidden bindings, and
hidden/padding cells cannot activate or accept a shortcut. The first row uses
the numerals already in the native background bitmap; lower empty cells use
the native number label without adding duplicate text over that artwork.
Occupied cells show their icon. Existing number-key
bindings continue to use the first visible row.

Retail evidence: Legions 7160 `gmShortcutUI::RefreshShortcutHeight` (`004386A0`)
clamps height to 1–5 rows and retains the bottom edge. `UpdatePadding`
(`004385E0`) grows the list to `90 + rows * 10`, while `MakeElement`
(`00437B70`) disables cells beyond index 99. The corresponding admin 7148
functions are `00437E50`, `00437D90` and `00437320`. Native layout `22000001`
supplies a 12-pixel border and 32-pixel rows. No DAT art or geometry is replaced.

The row count is stored with each character's existing local shortcut settings;
older profiles default to one row and invalid values are clamped. Server-side
shortcut/profile saving, chat aliases,
cooldown overlays and animated page scrolling remain separate gaps.
Native second-row and page key bindings were added in the pass above.

Validation: Windows Editor and standalone Development targets build. All 71
`AC2.Data` tests pass, including expanded item activation/inspection, hidden and
padding slot rejection, per-character persistence, and layout checks at
1024×768, 1280×720 and 2560×1440 with moved windows.
The rendered UDP journal test exercises the actual mouse grip, fifth-row skill
drag, collapse/expansion without binding loss, page arrows and disabled end
padding, alongside its existing skill, quest, fellowship and item-use checks.
Screenshots are inspected for five-row geometry, native artwork and labels.

## Recipe shortcuts and examination — 2026-11-08

Recipes now drag from the Craft tab to any of the ten hotbar pages. The typed
shortcut format adds `r:<recipe data ID>` alongside existing skill and item
bindings without changing the settings section or discarding older slots.
Clicking a recipe shortcut opens the ingredient window and its existing
server-validated ingredient selection; crafting still requires the Craft button.
Reopening the same recipe preserves its chosen level. Activating any recipe
while a craft is pending cannot discard that request's state.

Retail evidence: Legions 7160 `gmShortcutUI::FromElement` (`00436D40`) identifies
recipes as shortcut type `40000006`; `ListenToMessage` (`004381F0`) routes their
right-clicks to examination type `40000007`. The recovered `OpenRecipe` IFun
(`00B40118`, CPlayer method `1B7`) opens the ingredient panel locally. These are
separate actions from ingredient validation and execution. Saved bindings remain
present while the registry loads or a recipe is unavailable, but activation
requires the current character's granted registry and catalog entry. Removing a
recipe shortcut does not unlearn the recipe.

Recipe rows and shortcuts follow `gmCraftUI::GetRecipeIcon` (`0044A210`).
In both Legions and Icefire the WSL caller of `gRecipeAdaptor::SetIconDID`
gets the icon from recipe member 4. A zero value selects the profession's
icon, as on Brigandine Gauntlets and Mucor Mushroom Health Potion. Among the
364 supported Legions recipes, 331 inherit the profession icon and 33 set
an explicit icon.

Right-clicking either a recipe row or its shortcut opens the native examination
panel with the recovered name/description, profession/rank, level range, current
level's difficulty and gold cost, workshop requirement, ingredients and possible
outputs. Inspection reads the same catalog and curves as crafting, and does not
open a recipe, select ingredients, spend materials or send an item examination
request. Recipe inspection updates when the selected level or profession rank
changes and is replaced correctly by item, world-object or skill examination.
This is recovered catalog presentation, not a claim of exact retail examination
phrasing. Server-side shortcut persistence, chat aliases and cooldown meters
remain separate gaps. Multiple visible rows were added in the pass above.

Validation: both Windows Development client targets built successfully. All 70
`AC2.Data` tests passed, including recipe and item shortcuts. The rendered UDP
crafting smoke passed recipe dragging, right-click examination, opening with the
book closed, protection of a pending craft, consumption and output creation, and
recipe-binding retention. An isolated server restart also preserved crafted items
and quest progress. Evidence: `Unreal/Saved/Automation/Run-e4add664217a4c6085276778300034f8`
and `Saved/AC2NetworkTests/craft-ui-401319ee665347a69c6d08a3e2bc8756`. After adding
the retail profession-icon fallback, both targets were rebuilt and the focused
recipe suite passed again (`Unreal/Saved/Automation/RecipeShortcuts-Final`), as did
the rendered craft and restart check.

## Item shortcuts and hotbar examination — 2026-11-08

The hotbar previously persisted only 32-bit skill IDs. It now distinguishes skills
from item instances, preserving unsigned 64-bit item IDs as decimal strings.
Existing `Slots` skill settings migrate on read; typed bindings save under
`SlotsV2` in the same character section. Inventory drags create references without
moving the item. Activation resolves the current inventory entry by identity and
uses the existing server-authoritative item-use/equip/unequip path. Right-click
opens the item's authoritative examination or the skill's native information.

Compared Legions 7160 `gmShortcutUI::FromElement` (`00436D40`), `CheckItem`
(`00436C90`), `FIXMESetupItemElement` (`00437830`) and `ListenToMessage`
(`004381F0`), with the named admin 7148 equivalents. These establish skill type
`40000001`, item type `40000002`, pending item creation, layered item artwork,
right-click examination and replacement of an occupied shortcut when dragging.
The client now follows that replacement behavior rather than swapping bindings.

Ownership comes from a complete inventory snapshot plus acquisition/removal
events, independently of visual object creation. Pending login data cannot erase
saved bindings. Consumption, sale and ground drops clear references on all ten
pages; deleting a shortcut never sends an inventory drop. Inventory reordering,
equipping, quantity refreshes and appearance updates retain the instance binding.
Tooltips show the current item name and quantity, and icons retain native layers
and tints. Shortcut persistence remains local; native server-side shortcut saving,
chat shortcuts, cooldown overlays and multi-row resizing are separate gaps. Recipe
shortcuts were added in the subsequent pass above.

Validation: both Windows Development client targets built successfully. All 69
`AC2.Data` tests passed (10 reported resource/material warnings; the new
`ItemShortcuts` test had no warnings). The new test covers legacy migration,
full-width instance IDs, pending inventory, reordered items, consumption,
equip/unequip commands, save/reload, cancelled drags and removal without dropping.
`Network/test_journal_ui.py` also passed against an isolated real UDP server:
the rendered client dragged its starter bow onto page 10, received right-click
examination, equipped it with the inventory closed, and unequipped it again.
Native icon layers and examination text were checked in screenshots 15 and 16.
The isolated test client/server exited afterward; no production store was used.
Reports: `Unreal/Saved/Automation/Run-229bb7fe959c4ff093c35b0e6d37ca01` and
`Saved/AC2NetworkTests/journal-ui-1af0a4817e254003883803f7cd45b8f0`.

## Racial Shelter routes and dungeon traversal — 2026-11-02

The three server-supported races now have separate starter routes: Human in
`0299`, Tumerok in `03FD`, and Lugian in `04FD`. Lugians again start in native
room `04FD0144`; the old outdoor fallback incorrectly treated a floor-height
mismatch as an unavailable dungeon. Optional practice Idols, mentor, vendor and
lifestone remain available, with Lugian services moved into the correct start.
Practice Idols retain their passive-until-attacked behavior and level-1 loot.

`starter-areas-v1.json` and `tools/author_starters.py` supply 42 additional actors
and six travel routes, reusing existing early NPCs/services. Seven obsolete
one-room prototype actors and two shortcuts into another race's dungeon were
removed from the default training pack. Legacy portal IDs remain recognized for
older custom manifests. These encounters use the existing encounter respawn
implementation, not a new generator scheduler.

The shared sequence is Guard → Lieutenant → Sentry → Digger/tool → Scout/three
potions → Sentinel/Glyph → racial Menhir portal → Advance Scout → Menhir Shard →
overworld. The server gates travel by quest progress, consumes exchanged items,
grants rewards once, and persists progress. Final exit cells are `74BA0008`
(Human), `8DB70024` (Tumerok), and `6CB90034` (Lugian).

Evidence combines the installed DAT, existing Pyreal content, the archived
[Rescue the Shelter Hold walkthrough](https://www.20oz.net/index.php/Rescue_the_Shelter_Hold.html)
and supplied packet captures under
`Saved/PacketCorpus/extracted/896e60d43e617c38/Packets`. Three character-creation
captures and six starter-area travel captures provide native anchors; the catalog
records capture filenames, object/cell identities and message hashes. Mirrored
instances `028D`/`0298`, `03F4` and `04F3` are mapped to the loaded racial instances.
The last captured player position before travel establishes interaction proximity,
not an exact portal center. Later quest-role placements are explicitly reconstructed
from this evidence and walkable geometry; they are not recovered retail spawn tables.

The reported landing at `(134.7, 40848.3, -62.8)` meters is in Tumerok room
`03FD0132`. The captured quest exit is at ground level beside that tower. No
verified bridge placement connects its upper decorative structure, so it retains
its native transform. Lowering it obstructed the existing staircase and was
rejected. A regression route checks that the player can descend from the reported
landing and reach the quest exit.

An earlier workaround added a reconstructed spiral stair in `03FD0120` after
collision appeared to be missing along the native descent. That workaround was
removed on 2026-11-10: animated scenery had discarded its separate native physics
mesh. Restoring those colliders makes the original forward and return routes
traversable without inserting another structure.

Native room collision now reads the `01` cell mesh's physics polygons and its
CCellMesh tail, excluding portal boundary polygons. Previously the room path
could treat those boundaries as solid walls. The existing `06` static-object
physics collision remains in use. Synthetic decoder checks cover portal exclusion
and truncated-tail rejection.

Validation on the installed DAT:

- All 67 client data tests passed; existing missing-DAT-resource warnings remain.
- Server library tests: 252 passed, one existing ignored. Both starter-specific
  tests were rerun after the final manifest/provenance update and passed.
- All 17 content-preparation tests passed, including tampered catalog, spawn and
  route hash rejection.
- Production manifests completed all three quest chains over the real UDP protocol,
  including travel gates, exchanges, one-time rewards, exits and relogin persistence.
  These tests warp between interaction anchors; they do not test walking.
- The separate academy test passed passive approach, retaliation, killing and loot.
  All six supported bodies passed armor replication, atomic weapon replacement and
  dense inventory reordering checks.
- `AC2.Data.StarterClearance` passed all three main-dungeon routes, three vault
  routes and the reported tower-landing return. It loads native collision and the
  solid Menhir Shard, checks interaction range/line of sight, and searches with the
  maximum gameplay capsule across both sexes and size extremes for each race.
  Checks use 60-cm steps, a 55-degree slope limit and drops no greater than 3 meters.
  This is a capsule-sweep geometry audit, not an interactive playthrough or a full
  CharacterMovement simulation.
- Editor, standalone client and release game-server builds passed. Offscreen
  `-AC2EnvironmentSmoke -AC2StarterSmoke` captures show the descent repair and
  unchanged tower geometry in `Unreal/Saved/Screenshots/AC2Starter-*.png`.

Evidence logs: `Saved/starter-client-data-suite.log`, `starter-rust-suite.log`,
`starter-rust-final.log`, `starter-prepare-tests.log`, `starter-shelter-network.log`,
`starter-academy-network.log`, `starter-armor-network.log`, and
`starter-final-render.log`; geometry details are in
`Unreal/Saved/starter-clearance.json`. `Network/test_shelter.py` now runs the
production starter and armor regressions instead of the retired one-room fixture.

Restart the game server and client to load the rebuilt code and generated content;
the account service needs no restart or migration. Drudge and Empyrean appear in
creator previews but remain unsupported by server character creation and have no
completed starting routes. The audit established the three implemented routes.
The Menhir cinematic was recovered in the subsequent departure work below.

### Menhir Shard departure

The final Menhir Shard in the vault is distinct from the Menhir Vault Portal
that takes the glyph. Using the shard plays its activation motion. The local
camera follows the animated `Camera_Origin` and `Camera_Target` mounts.
The Human, Lugian and Tumerok retail captures all send a private illustrated
story before transferring the player. The client reads the pictures, timings,
subtitles and sound tracks from that message against the connected server's
DAT set. Icefire and Legions assign different resource IDs to the same pictures
and narration.

Finishing the story or pressing Escape acknowledges the private scene. The
destination can load in the meantime. The tunnel covers it until the scene
ends and the terrain is ready. The native Enter World effect remains on the
local avatar through the visible part of emergence, including when a server
stop arrives under cover. AC2E now completes the final transfer from the shard
itself and accepts returning characters who already earned its reward.

`Network/test_starters.py` exercises the three AC2E exits over UDP. The bridge
regression replays the captured story, transfer and effect packets against both
protocol profiles. `AC2.Render.MenhirShardNetworkCamera` samples the player's
view against the moving shard mounts and checks that bystanders keep their
cameras. `AC2.Data.MenhirPresentation` checks the artwork, narration resource,
subtitles and skip behavior. Pass `-AC2StoryRender` with offscreen rendering to
check the artwork in the framebuffer. `AC2.Render.PortalArrivalReadiness`
checks destination coverage and visible particles after watching the story.

## Tumerok tail seam under character scaling — 2026-11-02

Reproduced the reported tail-root hole using the six matching body/tail seam
vertices in the DAT meshes. The old component transforms opened gaps up to
4.58 cm (male) and 6.50 cm (female) at character-size extremes. A rotated child
component composed with unequal height/build scale loses the affine shear in
Unreal's `FTransform` representation; the skinned body and attached tail were
therefore scaled in different coordinate frames.

Compared Legions 7160 `CPartArray::UpdateBones` (`004E0A20`),
`CPartArray::RefreshMatrices` (`004E1250`), `CPartGroup::RefreshMatrices`
(`004E7730`) and `CPartArray::Draw` (`004E2440`): attachment matrices remain
separate from the deformation palette and are composed before object scaling.
The renderer now preserves the full attachment matrix and bakes both skinned
and rigid parts into their common model-root coordinates. Character scale is
then applied once to every part. Normal matrices use the inverse transpose.
Native unscaled attachment bones, socket offsets, meshes and DAT files remain
unchanged; this is not a Tumerok-only offset correction.

`AC2.Data.TumerokTailSeam` checks male/female, clothed/unclothed bodies, all four
height/build extreme combinations, idle, locomotion, combat stance, attack,
pickup and putdown. It compares actual mesh-section vertices in world space,
not just the attachment transform calculation. Maximum measured separation
after the fix is below 0.001 cm. Editor and standalone builds pass; all 66 data
tests pass (58 clean, eight with missing-DAT-resource warnings; the new test
also encounters the existing absent `2A0008BE` resource). Close-up captures are
generated by `AC2.Render.TumerokTailSeam` under
`Unreal/Saved/Screenshots/TumerokTail/`. Full data report:
`Unreal/Saved/Automation/Run-6ce4c704d7324da4b5151ef774538f3c/index.json`;
expanded seam/render checks: `Saved/tail-render.log`.

## Creator avatar overlap and point selection — 2026-11-02

The creator still used projected whole-body rectangles after the account selector
switched to posed-mesh picking. This allowed the front Tumerok female's rectangle
to intercept the pointer over the visible Empyrean female behind her.

Compared Legions 7160 `gmAvatarSelectionUI::ListenToMessage` (`0040DDE0`,
`0040DFC0`): the viewport requests `UIScene::StartSelection(..., pointSelect=1)`,
then uses the first returned object for hover state and the `41000005` selection
behavior. `SceneTool::SelectObject` (`00540B70`) sorts selected objects by depth;
`FinishSelection` (`00540B10`) uses potential bounds hits only when there are no
actual selections. The creator now shares the account selector's ray/posed-mesh
path, prioritizing the nearest actual triangle. We do not reproduce retail's
potential-bounds fallback for empty space. Native scene placement, projection,
horizontal capture mirroring, hover states and selection-animation wait remain
in place. Decorative carousel pedestals remain excluded.

`Launch-AC2.ps1 -SkipBuild -AvatarPickingSmoke` checks ten fixed visible-body
points, one for each native race/sex choice, through real Slate mouse routing.
It covers the rear Empyrean overlap, background/out-of-viewport rejection,
animated poses, native race/sex descriptions, and click-through to Empyrean
customization after the native selection animation. Coordinates are independent
of the hit-test implementation. Editor and standalone builds passed, as did all
65 `AC2.Data` tests (58 clean, seven existing DAT warnings).
The pointer smoke passed at 1600×900 (letterboxed/scaled page) and 1024×768
(native page size). Inspected the rendered Empyrean selection result at both
sizes. Logs are `Saved/avatar-picking-1600.log` and
`Saved/avatar-picking-1024.log`; the full data report is
`Unreal/Saved/Automation/Run-31bee0be8d3b44ada42802d40697e8d2/index.json`.

## Academy combat, treasure and scenery — 2026-11-02

The twelve authored practice Idol placements in `training-world.tsv` now use
level 1, their existing 60 health, matching vigor, and passive initial behavior.
They still retaliate after being hit. Their shared `470007D1` template otherwise
describes a level-24 outdoor creature; the override is scoped to the authored
placement IDs, survives respawn, and precedes explicit operator behavior rules.
Other creatures retain their existing inherited AI controls. Relationship scripts
remain incomplete, so this does not establish universal retail aggression parity.

Death events now carry the actual encounter level into loot generation. Native
quantity bounds and trophy slots remain separate. The inferred generic equipment
pool filters fixed armor above `2 * level + 2`, and fixed weapon damage above
`6 + 2 * level`, as well as native weapon use-level restrictions. Non-tradable
quest apparel is excluded from the generic pool. These stat budgets and randomized
finishes are AC2E reconstruction, not recovered retail treasure-table weights.
Explicit operator tables and explicit quest rewards remain authoritative. This
affects new drops; existing inventory records are not rewritten.

The client now decodes the polygon/BSP tail of static `06xxxxxx` meshes using
`CBaseMesh/CStaticMesh::StreamPack` and `CPolygon::UnPack` from Legions 7160.
It creates hidden collision sections independently of visible materials. The
Tumerok academy stair setups `040003DA` and `040003F4` contain material-free
physics meshes `0600074B` and `0600077D`; these previously produced no collision.
Their native ramp polygons now supply pawn collision, with bounded decoding and
the existing fallback for unsupported mesh payloads. Animated scenery collision
and full native physics simulation remain separate work.

The screenshot positions resolve to Tumerok cells `03FD0111`, `03FD012A`, and
`03FD0132`. The fire and mushroom offsets match the installed DAT, rather than
being caused by an extra room transform. `AC2AcademyRepairs.h` therefore records
four narrowly guarded reconstruction repairs, without modifying the DATs:

- Cell `03FD0111`, ordinals 25/26: smoke `4700016B` and fire `470000FC` move
  +1.6 m X / +0.92 m Y to the adjacent brazier bowl, retaining authored height.
- Cell `03FD012A`, ordinals 2/6: mushroom clusters `4700058A` and `47000592`
  are planted on the native room floor across their footprint, accounting for
  the slope and stem bounds. Cell, ordinal, identity and original coordinates
  must all match; missing floor geometry leaves the source placement intact.

Validation: 250 server unit tests passed with installed DATs (one existing ignored
test), including 1,000 academy loot rolls, outdoor-tier preservation, passive
profiles and respawn. `Network/test_academy_combat.py` passed real UDP approach,
retaliation, kill and loot with production encounter rules. Editor and standalone
client builds passed. All 65 `AC2.Data` tests passed (58 clean, seven existing
DAT warnings), covering malformed static collision data, bounded placement
repairs, all four repairs against the actual room data, both staircase meshes
and 288 cooked walkable stair faces.
Rendered room checks use `-AC2EnvironmentSmoke -AC2AcademySmoke` and save
`Unreal/Saved/Screenshots/AC2Academy-{brazier,mushrooms,stairs}.png`.

## Character selection feedback and hit targets — 2026-11-02

Compared the retained `gmCharacterSelectionUI`, `UIElement_ListBox`, and Legions
layout `22000002`. Retail selects the entire 155×20 list row: `highlight` uses
surface `410002F6` and blue text, while `normal` uses `4100225F` and white text
with font `42000024`. Removed the gold/outline override so those states are
visually distinct again. Long labels now end in an ellipsis, matching the native
`TruncateText` call; full names remain available on hover.

List clicks use their rendered clips, including the empty horizontal area beside
the name. Blank rows do not select anything. Carousel picks now intersect posed
character triangles and choose the nearest hit, excluding pedestal models, rather
than accepting empty areas within projected bounding rectangles. The subsequent
creator overlap fix above extends the same picking path to avatar creation.

A hover hint uses the existing DAT tooltip frame/font and names the target,
marks the selected character, and explains click/double-click actions. This is
an added usability hint, not a claim that retail displayed this exact wording.
Hover does not select another character. Double-click now selects the clicked
character before entering, matching both native list and 3D selection handlers.
Arrow keys and scrolling cannot change selection behind delete confirmation.

Validation: editor and standalone builds passed; all 62 `AC2.Data` tests passed
(55 clean, seven existing DAT warnings). `Network/test_login_ui.py --selection-only`
passed real Slate pointer routing, full-row and empty-space hits, native row
states, model picking, carousel rotation, hover identification without changing
selection, modal protection, and double-click entry with the correct server
character identity. Inspected the rendered `AC2Login-selection-01.png` and
`AC2Login-selection-03.png` screenshots. The broader login/creator/combat smoke
passed all 19 frontend steps and combat input steps 0–8, but timed out later in
its combat sequence; it is not reported as a full gameplay pass.

## Warder Scout's Notebook and Captain referral — 2026-11-02

The Scout now starts a playable level-10 Notebook task, including when approached
without the earlier Warder introductions. An active Find the Warder Scout referral
completes in the same transaction. The native journal distinguishes killing the
Drudge Slinker Bandit, taking its notebook, and returning it. The 24-hour deadline
survives logout and restart. Hand-in awards 800 XP, an Emblem of Order, a Mad Crone
Glyph and a Prosper Glyph, then starts the 48-hour Seek the Warder Captain task.
Speaking to the Captain in his hideout awards 2,500 XP once. Repeat limits are
20 hours; the Scout can renew an expired Captain introduction.

Recovered DAT records identify the kill/acquire effects, item assets and journal
stages; the retained wiki supplies levels, limits and rewards. The manifest pins
those sources and the three existing production placement rows. Older guide
coordinates disagree with retained world placements, so current directions use
the Scout near 14.0N, 0.6E, the Bandit camp at 15.5N, 1.7W, and the hideout entrance
at 20.6N, 14.0E. No additional duplicate actors are authored. These are reconstructed
server transitions, not recovered retail server scripts.

Notebook drops require the owner's active task. Ordinary captured/random loot
cannot inject extra notebooks, and a corpse remains tied to the quest attempt
that produced it. Hand-ins commit inventory, XP and journal changes together:
full backpacks preserve the notebook, repeated requests cannot duplicate rewards,
and an expired attempt cannot reuse an old notebook. The task item cannot be
dropped or traded. Native inventory and quest messages drive the existing UI.

Inventory item use now targets the selected Scout, alongside the conversation
hand-in path; ownership, range and reward checks remain on the server.

Validation: release server/bridge and editor/standalone client builds; server and
bridge Rust suites; three Notebook integration tests with native DAT validation;
dedicated corpse/attempt loot and production Bandit respawn tests; and
`Network/test_warder_notebook.py` exercising production placements,
level/range checks, combat, pickup, both rewards and four server restarts over UDP.
The network fixture grants test XP and weakens its Bandit; production stats and
placements are unchanged. All 62 client tests passed (55 clean, seven existing
DAT-resource warnings); this pass does not claim rendered client acceptance.
Potion Raid, Seek Dantry, and the glyphs' separate vault progression remain
unfinished.

## Shared mastery controls and support targeting — 2026-11-02

The skill UI previously required an exact racial match, excluding nine Race=0
mastery/grandmaster/paragon skills already supported by `skills.rs`. The UI now
uses that same bounded exception for playable races; other race-zero NPC,
kingdom and grant records are not automatically enabled. Training/raising
controls, node highlights and inspection share one eligibility calculation.
Missing levels, credits, XP and prerequisites, incompatible skills, dependent
untraining, death and maximum rank have specific explanations. Disabled controls
retain tooltips without becoming clickable. Passive skills do not advertise
double-click activation; untraining explains the remaining earned-XP requirement.
This changes access to supported training, not the availability of unrecovered
active effects or expansion race profiles.

The admin decompile's `FellowshipHelper::ListenToMessage` calls `SetSelection`
when a roster row is clicked. Roster clicks now select the nearby member or self
through native targeting. Members outside the loaded visible scene report that
they are not nearby. Ease Suffering no longer replaces every target with the
caster ID. The server's native target mask and `normalize_aim` preserve selected
player targets while resolving helpful casts on scenery/enemies to self; normal
range, line-of-sight, living-target and implement checks still apply.

Validation: editor and standalone builds; 62 native client tests (55 clean,
seven existing DAT-resource warnings, zero failures); a real two-client UDP test
training and raising the three base common masteries, restoring 25 health to the
selected player without healing the caster, self-healing with scenery selected,
and accepting native self-selection. `Network/test_support_skills.py` creates
and removes only disposable test sessions; its XP, damage and idol placement are
fixtures, not production content. Rendered journal/skills/fellowship acceptance
also exercises the existing train/raise/untrain/cancel controls.

## Fellowship, skills and inspection controls — 2026-11-02

The flag button now opens the DAT social/fellowship panel. Its controls use native
WM_Fellowship requests through the bridge, with 64-bit player identities preserved
as strings. The bridge decodes the 1f0/1ee/1ef fellowship graph, invitation, clear,
and member vital updates. Create, recruit, accept, decline, dismiss, leave and
disband use the existing authoritative server group rules. The roster has native
health/vigor meters and scrolling for eight members; names fit their columns with
full names in tooltips. Invitations replace the creation form and expire after
30 seconds. Nonleaders cannot use management buttons. Membership is session-only;
XP/loot sharing, allegiance and friends management remain unavailable and the UI
states those limits instead of promising them.

Skill-tree arrows, track/drag and wheel input share the same scroll offset; the
Craft tab's scrollbar controls recipe pages. Selecting a skill opens its native
description, level and prerequisite requirements, credit cost, rank and XP data in
Examine. Native help buttons explain training, raising, untraining/cancellation,
active-skill use, hotbar dragging and quest journal navigation. Eligibility and
unsupported skill rules still come from the server; this does not unlock every
unrecovered skill effect.

Inspection profiles allow 48 bounded lines rather than 12, including longer
native descriptions. Runtime stats stay authoritative. Known equipment slots,
armor/damage rolls, two-hand requirements, race/level restrictions, implemented
consumable and quest-item behavior, gathering tool capacity, survey radius and
crafting-material professions are shown when available. Client supplements avoid
duplicating server descriptions and requirements. Text height uses native font
metrics and margins. Scroll arrows, dragging and wheel input reach the last row;
shorter live updates clamp and reposition immediately without an empty frame.

Validation includes the full Rust suites, 61 native client tests, a two-client UDP
fellowship round trip, and the rendered journal/skills/fellowship smoke. The client
tests cover invitation controls/expiry, member vitals and leader restrictions,
eight-member scrolling, long inspections and live refresh. Rendered tests use
actual Slate input and native server replies for skill training, raising,
untraining/cancellation, chat resizing, quest tabs/expansion and group creation.

## Quest conversations and native journal layout — 2026-11-02

The wiki conversation catalogue describes many quests without executing them.
This pass adds three native Warder introductions as durable server quests:
Esper Warder → Warder of Cragstone → Warder Corporal → Warder Scout. After
Drudge Outcast Camp, another conversation with Esper starts the Cragstone
introduction. Cragstone and Corporal can also start their outgoing referrals
independently. The Scout referral has a 48-hour deadline and awards 30 XP;
all three use the retained guides' 20-hour repeat restriction. The first two
award no XP. Repeated clicks cannot award completion twice. `wr1` transactions
persist acceptance, completion and rewards together, including compaction.

An existing dispatch conflict also prevented the Windmill's Find the Esper
Warder referral from completing: the newer camp handler intercepted it. The
unfinished referral now takes precedence, then the camp and post-camp route
become available in order. Wrong items still use the item-specific handlers.

The admin `gmJournalQuestsUI`, `gmQuestsUI` and `gmQuestInfo::InqExtraText`
implementations and installed DAT layout supply the journal structure. Rows now
use `QuestTemplate` and `QuestExtraText`, their native fonts, margins, arrow
states and measured text heights. Clicking a row expands/collapses details and
marks its changed indicator read. Native string-table substitutions supply the
description, current instructions and intended level. Active/completed/failed
states, deadlines and repeat timers arrive through native quest messages; timers
continue ticking locally. Finished entries retain the native completed/failed
detail. Scroll arrows, track clicks, thumb dragging and wheel scrolling share
the same range. Story has a separate repository and remains unimplemented.

Chat uses its native 235×50 minimum and 512×512 maximum resizing limits. Its
anchored log, entry and scrollbar reflow before painting, without scaling the
font. Size and position are saved. Fixed-size journal/inventory/examine roots
ignore saved dimensions. The hotbar's specialized multi-row resize behavior is
not implemented; its full-area border cannot intercept slot drags. This is a
bounded layout correction, not certification that every window matches retail.

The Notebook/Captain continuation is now implemented in the section above.
The Dantry introduction remains unavailable pending Drudge Potion Raid.
The authored content file records native identities, cached guide provenance,
deadlines, rewards and reconstruction policy. NPCs outside implemented quest
handlers can still provide information only.

## Animation continuity and item handling — 2026-11-02

The native idle graphs are present for Orven, Adaro and Esper Warder: human
behavior table `44000001`, body map `03000014`, idle key `80000012`, graph
`05000024` and body clip `05000029`. The remote animation classifier was probing
around the network pivot instead of the displayed feet. Authored offsets or
missing nearby collision could select Air, which has no matching idle branch.
Server content now retains its ground-navigation element (water still selects
swimming); remote player probes use the displayed foot position. Missing idle
branches retain the native ground/peace idle, and zero-period static poses are
sampled instead of exposing the skeleton's rest pose.

Ground/air and stance transitions keep their gait phases and use the existing
native transition ramps. The movement component keeps separate ground limits
for the first landing physics step, before the actor updates its element.
Remote positions interpolate across the measured packet interval, including the
arrival frame, rather than exponentially easing at each update. Gait input now
includes actor/body instance scale. The offline mob controller reads native
speed, acceleration and braking instead of using 160 cm/s for every creature.
The server already resolves each creature's MotionInterpDesc and inherited
visual scale: regression examples retain 3.9, 6.5, 7.475 and 7.8 m/s limits.
This does not implement unrestricted flying or underwater creature navigation.

Native Pickup `41000020` and Putdown `41000021` resolve through keys `80000031`
and `80000032`. Original-race body durations (seconds) are Human 1.79167/1.75,
Lugian 1.45833/2.0 and Tumerok 1.875/2.0, shared by both sexes. A separate head's
longer idle period no longer slows the body action; the largest root skeleton
drives the shared behavior phase. Handheld network attachments hide during the
reach and return afterward.

AC2E broadcasts stamped native behavior messages at the start of ground pickup,
drop or corpse pickup. The existing durable inventory transaction commits at
half the body duration, with ownership, range and capacity checked again.
The midpoint is an authored approximation: the available clips do not supply a
recovered inventory-contact hook. Moving more than 0.75 m, returning to combat,
death or leaving the avatar cancels an uncommitted transfer. Consecutive pickups
can replace the recovery half of a committed reach. Combat looting returns to
the prior stance without restarting attacks on the corpse.

Validation includes all 61 `AC2.Data` tests, with actual multipart vertex changes
for the three named NPCs, Red Soldier Wasp and Crawlspace Vermin in a visible
landblock without nearby floor collision. All six supported online body/sex
combinations exercise pickup/drop resolution, timing, vertex movement and return
to idle. Movement regressions cover landing limits and 30/60/120 Hz presentation.
Real UDP tests check both observers, transfer delay, movement/logout cancellation,
competing pickup ownership, combat stance restoration and restart persistence.
The rendered combat/inventory smoke also passes all 16 inventory stages, including
screenshots of the native putdown and pickup reaches before the item transfers
(`Unreal/Saved/Screenshots/AC2Inventory-putdown-reach.png` and
`AC2Inventory-pickup-reach.png`). Its world-drop point avoids saved movable panels,
so a relocated inventory window cannot silently consume the test drag.
These are bounded regressions, not exhaustive visual approval of every animation
or a claim of complete retail animation parity.

## Follow-up UI, signs and grass corrections — 2026-10-30

The shared road-arrow meshes `06000554` and `06000555` now correct the
horizontal lettering UV orientation. Front/back rendered captures both show
readable lettering; arrow geometry, world headings, collision and DAT placement
are unchanged. Selected labels use `SelectionName`, while general labels retain
`NameTextTemplate`; explicit multiline names receive enough vertical space.
The native `InqRadarLook` color policy is still incomplete, so this does not
establish full retail nameplate color parity.

Grass material version 17 converts native partial alpha to spatial coverage
instead of discarding all texels below a fixed cutout threshold. It retains the
existing distance fade, batching, native textures and exact DAT placements.
Rendered checks include the `7CB9` cave approach. This improves cutout edges;
it does not fill authored bare patches or reproduce the native per-instance
scenery shadow-color calculation, which remains missing.

Inventory wheel, arrow buttons and thumb dragging now keep rows and the thumb
in sync immediately. Inventory inspection requests authoritative server rows,
including native item descriptions and supported potion requirements. A short
right-click on the local character also requests self-examination without
turning the player into their own combat target.

The corpse window sizes its title and close button against its final dragged
bounds. Its single-bitmap frame now extends the center while preserving curved
end caps, rather than repeating the entire frame. Native chat background tiles
shared boundary pixels; clipping their overlap removes double-blended dark
lines through the text. Chat scrolling also rebuilds its thumb immediately.

These changes do **not** complete generated loot or active skill effects:

- Brave Crawlspace Vermin (`47000F7E`, loot profile 784) has three captured
  observations: two elixir-only and one elixir-plus-boots bundle. The current
  capture-derived distribution is not the full retail loot generator.
- Item instances still lack generated armor/damage/affix and color-variant
  state. Recovered footwear has distinct native models and race restrictions,
  but color defaults to zero. Missing ratings are reported as unavailable.
- The supported health/vigor potions retain the existing development recovery
  rules. Slayer elixirs and other unsupported effects are not substituted with
  healing effects. Their effect formulas still require recovery/implementation.
- Native skill training, raising, untraining and cancellation pass the rendered
  test. Most active skill effects remain unsupported; catalog visibility does
  not mean those effects are implemented.
- Examination reports known server level/health/vigor. A retail chance-to-hit
  calculation is not yet implemented and is not fabricated in the panel.

This pass built the editor, game and server. Checks include the rendered
combat/inventory/mouse flow, self-examination, inventory scroll/drag regression,
native sign front/back and ground-cover captures, recovered equipment and
corpse-loot UDP tests, and examination unit tests. All 52 `AC2.Data` tests passed.
The recovered-population combat render passed with the widened corpse title,
native tooltip and item pickup. The shop render passed sale/purchase/close and
door/chest animation and collision. Render tests temporarily avoid overlapping
saved panels without changing the user's persisted positions. Account-service
state was not changed. Tests use isolated accounts and world stores.

## Recovered equipment, chat and cave furnishings — 2026-10-30

`recover_apparel.py` now generates 31 bounded wearable profiles from matching
Legions EntityDesc, qualities, pile appearance and explicit wearable-family
records. Each profile records its source evidence and fingerprints; equipment
slot, race and male/female appearance must agree. Complex quest-item physics
and ambiguous families are excluded. Armor ratings come only from the item's
own properties, not from another item with the same mesh. Two of these profiles
have a known rating; the others explicitly report that the armor rating is
unavailable. Random retail loot rolls/affixes remain unavailable.

Previously saved mask-zero copies receive a narrowly validated, journaled
equipment-metadata repair when used. Leather Boots `47000FDC` equip on a Human;
Initiate Moccasins `47001009` require a Tumerok. Incompatible races are rejected
with that requirement instead of the generic unsupported-use message. Clothing
appearance and equipment state survive logout and server restart. This is not a
general grant of item-use support to every recovered loot definition.

Basic skill attacks now enter the repeated ordinary-attack queue after their
first skill hit. A new attack cannot replace an in-flight projectile or windup.
The real UDP test verifies that a basic missile skill kills its target without
another click. Shortcut icons resolve from the racial skill repository rather
than three unrelated fixed icon IDs.

Chat text is painted at the native ChatLog node, within its clip and beneath
later windows. Wrapped-row counts determine history range and thumb position.
Dragging the thumb preserves its grab offset, and overlapping panels receive
input first. The rendered UI test drags to both history endpoints and back.
Overhead names now use the native label-template font and outline attributes.

Cell furnishing offsets are already in landblock space. Applying the room frame
a second time displaced dungeon torches/furnishings while their light sources
remained on the walls. The extra transform is removed; a rendered capture of
`7CB90102` confirms its torch mesh and flame at the wall. This does not change
room geometry or source placements. Native ground cover now fades across
120–160 m in planar distance, before component culling at 200 m.

Validation: 106 Rust library tests passed, one explicitly ignored; 17 bridge
tests passed. UE automation reported 47 successes and five successes with
warnings, zero failures. Rendered UI and environment smokes passed. Real UDP
gameplay, service and recovered-equipment tests passed using isolated stores.
`Network/test_recovered_equipment.py` verifies old-item repair, appearance,
race rejection, inspection, restart persistence and the captured interior
Drudge Taskmaster in `7CB9013A`. A stationary online 1600×900 performance run
at `7EBA` averaged 10.605 ms/frame (~94 FPS) on this machine, with the extended
foliage distance. This does not establish performance in every biome.

Remaining findings from these screenshots:

- The raised-hand bow idle reproduces using native Human map `03000014`,
  mode `41000010`, idle animator `0500004C`. The firing sequence animates and
  attack repetition is fixed, but the idle-pose discrepancy is not resolved.
- Native grass atlases are loaded at their available high resolution. The
  overhead cross-card/grid appearance is not resolved; no extra plants or
  random placement changes were introduced to conceal it.
- Cave lighting/material fidelity remains incomplete. Programmable and
  generated-coordinate layers still need native equivalents; the torch
  placement correction is not a claim that all cave shading is now correct.
- The compatible pack has two exterior creatures and one captured interior
  Drudge Taskmaster in `7CB9`. Its 128 DAT furnishings are a separate client
  resource. The available captures do not provide a complete room-by-room
  population, spawner schedule, or chest-content definition for this cave.
- Existing supported vendor interaction, buy/sell and inspection tests pass.
  Recovered vendor identities without store properties and complete retail
  vendor stock remain unsupported; the bounded development stock is not retail
  stock. No name-based heuristic promotes arbitrary NPCs to vendors.

## Blood, inspections, skill controls and floating text — 2026-10-30

Ground decals are now decoded from `DecalFX` instead of discarding its
`0x80000000` field. The renderer projects them onto world collision, retains
them independently of the victim, and expires them using the recovered
`DecalEffect::Think` fade calculation. Native material, size, rotation, radius,
colour and lifetime are preserved; red/green and gore preferences select the
authored variants. Invalid ground samples and sharp discontinuities are omitted.
Projection uses a bounded grid, not the original engine's triangle clipping.

The generic AC2E damage event reuses the ground decal asset in native FX table
`1E000076`, key `41000620`: red material `310003DD`, green `310006DD`, lifetime
30 seconds. It does **not** invoke the Decapitate ability's animations or other
effects. This asset and fade are evidenced by the DAT and recovered decal code;
an ordinary-hit trigger on every retail attack has **not** been established.
This generic trigger is an AC2E policy pending stronger capture evidence.
Only positive authoritative damage emits it; health snapshots do not.

Creature deaths broadcast the native composed `Corpse of <name>` StringInfo,
including its nested original name, as a stamped string-quality update.
Observers entering later receive that update; respawns restore the original
name. Inspections preserve that composition and include runtime player vitals,
level/race, self-only progression, service interaction information, creature
health, item quantity/equipment slots and known armor values. Owned corpse loot
and ground drops can be inspected. Loot inspection still enforces ownership,
visibility, expiry and range. Unknown random affixes/resistances are not invented.

The skill panel's button area now follows its selected skill state; it was
previously forced to the overview state, hiding modification controls. Native
Train/Raise/Untrain/Cancel controls update the repository. Raised skills untrain
as newly earned XP debits invested XP one-for-one, refunding available XP without
increasing total XP; completion returns the skill credits. Dependencies and
pending untraining prevent invalid use/advancement. Cancellation, restart and
journal compaction preserve progress. A malformed old credit-only packet was
removed; credit changes use a complete native repository update.

All 70 decoded panels remain browsable, but **full skill gameplay is not
implemented**: training rules currently support Human/Lugian/Tumerok skills up
to level 50, with hero/faction/crafting and additional race rules still gated.
Most advanced ability effects still require implementation. Functional tree
controls do not establish support for every ability shown in the DAT.

Nearby nonselected objects now receive clickable DAT `NameTextTemplate` labels
within the client's current radar/target range, with overlap and viewport checks.
Authoritative hit events create outlined `DamageTextTemplate` numbers; portal
space hides them and world teardown clears them. Native reticle tuning supplies
placement attempts and float travel. Character options expose persistent
Damage numbers, Third-party damage, its range, and Object names controls.
Personal damage defaults on and third-party damage defaults off. Exact retail
damage-script colour/scale/duration, radar-look name colours and averaged bounds
are still incomplete; the hit presentation currently uses a two-second lifetime.

Validation: server library 104 passed/1 ignored; bridge 17 passed; UE Editor and
game builds passed. `AC2.Data.NativeBloodDecals` verifies native red/green assets,
gore suppression, terrain projection, missing-ground rejection and expiry.
`Network/test_corpse_loot.py` verifies two-client and late-observer naming,
inspection access and persistent loot. `Network/test_services.py` also checks
self/other-player, equipment, vendor, door and chest inspections.
`Network/test_journal_ui.py` routes actual Slate clicks through training, raising,
untraining/cancellation, quest tabs and dragging, and rejects skill decode errors.
The outdoor combat render test checks damage-number visibility toggling, ground
decal creation, corpse naming, native loot pickup and original-name respawn.
Screenshots were reviewed; this is not a side-by-side retail-client acceptance.

## Server time and the native sky — 2026-10-30

The client previously started each world at noon and advanced a float by frame
delta. AC2E supplied process uptime only at connection, and the bridge discarded
clock events. Clients joining at different times therefore saw different skies,
and restarts reset server time. The weather seed also used a local day counter.

AC2E now saves `data/world/clock-origin.json` under the world store lock. Native
wire time is seconds since that world's origin, including downtime; the running
process advances it with `Instant`. The origin is an AC2E world convention, not
a recovered epoch of a historical retail server. A first launch creates it;
subsequent launches preserve it. Gameplay deadlines remain on process monotonic
time. Keep the origin with world backups.

The server sends an initial and five-second periodic `CTimeSyncHeader` with the
native reliable sequence and ISAAC checksum. Plain sequence-zero time headers
are rejected by the native header policy. The bridge forwards connection time
and the adopted native timer; Unreal extrapolates using real monotonic seconds,
accepts forward corrections and ignores stale backward updates. World loading,
idle characters and frame rate no longer determine the calendar speed.
Server-generated creature position timestamps use the same wire clock; AI
scheduling continues to use process monotonic time.

Retail verification uses Legions `GameTime::StreamPack` (`005D52B0`),
`CalcDayBegin` (`005D5420`), `UseTime` (`005D5980`), and
`CSkyDesc::GetCurrentDay` (`0050DD80`), plus the installed DAT and an independent
pinned-reference clock probe. `35000000` has zero-time offset 4893 seconds,
zero-year 498, day length **7347 seconds**, and 360 days/year. The sky follows
that calendar's fraction and absolute day seed; weather selection uses the
high 16 hash bits and strict cumulative-frequency comparison from the decompile.
The pinned reference's full-32-bit weather hash differs, so it is not used as
the weather oracle. Existing native sky keys drive object motion, tint,
sun/ambient lighting, fog and weather as the fraction advances.

Validation: `Network/test_world_clock.py` passes real UDP checks for idle time,
periodic synchronization, clients joining at different times, and restart after
three simulated elapsed days. Server library tests pass (93 passed, one ignored).
The UDP test also enters two characters and checks movement replication at the
restored multi-day timestamp.
`AC2.Data.NativeServerCalendar`, `AC2.Data.NativeSkyLightingAndLifetime`, and
`AC2.Render.ServerTimeSkyCycle` pass; the render test samples 06/12/18/23 hours
at different frame deltas. Captures in `Unreal/Saved/Screenshots/SkyClock` were
visually inspected. This verifies native calendar/protocol logic and our rendered
cycle; it is not a live side-by-side retail-client screenshot comparison.

## Swimming mode selection — 2026-10-30

The old `Swim = 40000003` mode constant actually selected **riding** according
to native EnumMapper `23000048`. Swimming uses the Water element with Peace
mode. Local movement/behavior and remote network animation now share that
selection, preserve death mode, and restore the requested stance on land.
This prevents combat-mode packets from selecting riding poses while submerged.

Native human water graphs select idle clip `05000280` and movement clips
`05000323`, `050002DD`, `05000345`, instead of riding graphs `05000EBE/05000EBF`.
`AC2.Data.NativeSwimmingClips` and `AC2.Render.NativeSwimmingObservers` pass.
The render test drives the actual local controller and remote network handler,
compares their multipart mesh poses, and verifies attachment continuity and
return to land. Forward crawl and idle tread-water captures were visually checked
in `Unreal/Saved/Screenshots/Swimming`. This test does not use two live UDP clients.

## Tent interior rendering — 2026-10-30

The circus tents in block `6EAD` use entities `47003669`/`4700366A`, visuals
`1F00192D`/`1F001CCD`, and materials `31000845` through `31000848`. Their
modifiers set property `4003`, bound to material field kind 5, to true. This
maps to native `CULLMODE_NONE` (1); false maps to `CULLMODE_CW` (2). The renderer
previously skipped both this override and the serialized layer culling field.
Opaque cloth therefore lost its back faces even while the masked fringe stayed
visible. The missing interior was not missing geometry or texture data.

Material layers now retain their culling state and modifiers apply the native
boolean mapping. World and UI mesh materials select precompiled one/two-sided
variants independently of alpha blending. Ordinary one-sided templates remain
one-sided; DAT geometry, UVs, textures and placement are unchanged. Material
template version 14 and the launcher regenerate missing variants automatically.

`AC2.Data.NativeMaterialCulling` checks both tent sizes' modifiers and the
unmodified one-sided template in world and preview modes. The rendered
`AC2.Render.NativeTentInterior` test loads the actual tent and captures exterior,
inner-wall and inner-roof views in both modes. Both interior views failed before
the fix and pass afterward; screenshots were visually inspected under
`Unreal/Saved/Screenshots/TentInterior`. Opacity, native preview lighting and
appearance, grass contact, and texture/alpha regressions also pass.

This pass uses the installed Legions DATs, the retail/admin decompiles and the
pinned Rust reference. It preserves exact DAT scenery choices and keeps
incompatible capture placements excluded. It does not establish full retail
physics, rendering, quest or population parity.

## Appearance and effects

Some programmable-material alternatives serialize an empty texture slot but
declare its default sampler in their native property list. Ignoring that default
terminated the fixed-function texture cascade before its palette/lighting stages.
The material reader now resolves those defaults before evaluating the cascade.
Creator skin/clothing palette tests cover six avatars at their choice extremes;
rendered body/clothing screenshots confirm visible palette changes. This fixes a
specific color-processing defect, not every unsupported native shader.

FX table channel conditions now select the configured blood intensity and color
before starting particles, visuals, scripts or sounds. Previously normal, green
and the retail comic-style "Fun" blood variants could run together. Blood effects
and green blood are available in the existing options panel.

Persistent model wounds now use native `BloodyDamaged` FX `41000002`, not hit
particles or an invented decal. `DBFXTable 1E000057` selects appearance tables
`200001E8/2000003A/200001E9/20000056` with key `2`, according to the low/normal
and red/green blood channels. The normal table's five intensity thresholds are
`0.40/0.55/0.70/0.85/1.0`. AC2E health snapshots drive the scalar as
`(maximum-current)/maximum`; this maps to 60/45/30/15/0 percent health. The
thresholds and textures are recovered DAT data; that health-to-scalar mapping
is our client fallback, not a recovered retail server script formula. Explicit
native FX scalar and StopFX messages are also supported. The admin decompile's
`FXManager::DoAppearanceFX` at `00505C30` confirms multiplying the appearance
key's scalar modifier by the effect strength and removing it at zero.

Wounds modify materials on existing mesh sections without rebuilding the rig or
interrupting animation. Base skin, dyes, garments and held attachments remain
intact. Local players, remote players, compatible creatures and the inventory
portrait update on health changes. Current health is retained for model/equipment
rebuilds and initial observation; healing, resurrection and logout clear wounds.
Changing blood preferences reapplies the current stage. None/Fun do not acquire
normal wound textures. DAT material-type restrictions are preserved: for example,
Idol Spawn material `3100047D` has type `2`, which its inherited wound table does
not target. It does not get fabricated humanoid wounds. General appearance-FX
mesh/opacity atoms and timed ramps remain outside this wound-material pass.

Portal travel and world entry use the native tunnel textures, particle definition
and enter/loop/exit sounds. The capture animates during loading. Login, server
travel and offline map/recall now remain covered until all required landblocks
and scenery finish uploading, collision is ready, the destination's distant
terrain is installed, and a submitted frame/render fence completes. Closed
dungeons wait only for their connected interior block, not unrelated outdoor
blocks. Sky, lighting and distant terrain continue updating during that hold.
The editor runtime also waits for outstanding first-use shader compilation.

Entry fades the tunnel over an opaque backing, so the new destination cannot
show through during the first second or before the portal actor's first tick.
The player stays held through final placement/camera setup and the initial
network object upload batch. Queued network data, new object/visual revisions
and pending collision invalidate the reveal fence. SceneRenderingComplete is
sent after destination rendering is ready, including subsequent teleports;
AC2E's post-acknowledgement inventory/population arrives under portal cover.
A new arrival during the exit flash immediately resumes opaque portal space.
This follows the Legions SmartBox::HandleTeleport / WorldRender sequence in the
local retail reference; the native dome and exact camera/FOV distortion remain
missing. There is no native protocol marker guaranteeing receipt of every
future population update; later live-world updates still stream normally.

`AC2.Render.PortalArrivalReadiness` exercises real DAT dungeon/outdoor streaming,
render-fence readiness, reentry during the exit flash, eventual reveal and the
offline map request. `Network/test_map_travel.py --render` verifies login and
server-driven map travel, with captures of both portal cover and the revealed
world. These tests use disposable worlds/accounts.

## Movement and physics

- Native behavior callback thresholds no longer incorrectly hold the last frame
  of ordinary movement actions. Death retains its explicit held end pose.
- Jump animation starts before the physical impulse, using the decoded native
  animation jump phase/delay. Releasing a pending jump cancels it.
- Entering water clears incompatible ground actions and selects the local
  player's native water/swim graph. Incoming ground movement behaviors cannot
  overwrite it while swimming. Remote actors now classify water from replicated feet positions, native body
  height/water thresholds and the local DAT water plane, and select their own
  water/swim graph. They no longer inherit the local player's movement state.
  Swimming does not receive the presentation-only floor correction.
- Falling and landing select their native animation/effect paths.
- AC2E now tracks accepted descent and applies damage once on contact
  with DAT-verified terrain, interior BSPs and static-object floors. Drops up to six meters are safe; faster descents over
  six meters scale damage to maximum health, reaching maximum at thirty meters.
  Every verified landing clears the tracked apex and speed, including harmless
  steps and downhill walking. Contact detection must not be gated by the damage
  threshold: doing so accumulated earlier grounded descents into later falls.
  This is an explicit AC2E policy, **not a recovered retail formula**. The collision implementation is imported from the pinned Rust reference.
  This remains contact validation, not fully authoritative movement simulation.
  Water friction/lift use native force branches; the Unreal swept-capsule solver
  still supplies local movement. Retail collision transitions, water-entry fall
  cancellation and the exact retail fall-damage formula remain incomplete.

## Native windows and loot

Drag the headers of the inventory/journal side panel, examine side panel, chat,
vitals, toolbar, shortcut bar, shop and loot window to reposition them. Positions
are clamped to the viewport and saved in client settings. The separate radar/map
widgets are not included yet.

The toolbar opens native skills and quest windows. SkillPanel registry
`70001976` supplies 70 panels with their actual node cells, parent lines, icons,
localized labels and specialization/back links. Native tree cell dimensions and
orientation control placement. Training, raising and untraining send the existing
server requests, with credit, prerequisite and XP checks. Displaying a skill does
not implement its gameplay effect: the server still restricts unsupported hero,
faction and advanced class rules. Quest tabs display received active/completed
records; missing story data stays empty instead of being invented.

Double-click a defeated encounter to open the native external-container window.
Double-click an item or use Loot All to collect it. Close/reopen, range and owner
checks, backpack updates and saved claimed items use the server protocol. Native
icons and the loot window are checked through rendered mouse input.

Current ordinary encounter loot is one explicitly authored potion reward. XP and
gold remain automatic, as do tutorial-specific quest rewards. Pending corpse loot
expires after sixty seconds and is not persisted across a server restart; claimed
inventory is persisted. Full retail loot tables, public chest contents and all
native-client container acknowledgements remain incomplete.

## What the placement audit establishes

The read-only DAT scan examined 15,024 entity templates, 3,348 outdoor blocks and
57,013 interiors. It found 20,784 outdoor and 151,868 interior static placements,
but no creature population placements. Encounter descriptor `14000000` has zero
rows in this installation. Five templates could not be parsed completely and are
listed in the report. An AI definition is not evidence for a spawn location.

The selected server pack now contains 1,276 compatible or explicitly authored
objects: 534 encounter candidates, 41 vendors, 48 doors, five chests, seven signs
and 641 other objects. Encounter classification is not proof of hostility. Combat
metadata was regenerated from the installed DATs for 317 entity profiles, using
the upstream candidate list without copying mismatched upstream combat values.

The new pinned capture snapshot has 22,422 observations. Source priority retains
existing IIDs and whole earlier cell snapshots; 6,798 overlapping observations
are withheld instead of stacking different-time captures. The final report also
withholds 14,051 incompatible observations, 1,744 placements missing records and
five duplicate landmarks. Capture names are preserved in spawn and examination
messages. Exact DAT foliage remains unchanged.

Non-service encounter actors can now pursue within their native homesick radius
(where supplied), using native floor and wall checks. Both sides' attacks check
native line of sight. Animation/state and movement updates reach observers and
late arrivals. Three authored tutorial enemies were moved onto verified Shelter
floor positions; captured placements were not relocated. Aggro relationships,
obstacle-routing navigation, roaming, generic attack reach and loot remain authored or
unfinished; no missing retail spawn generator is inferred from an AI template.

Native chest classes open the external-container UI, including an empty result
when no contents are configured; key/quest-restricted chests remain restricted.
The open/closed model state is shared by viewers and sent to late arrivals.
Other visible world objects can be examined through the common interaction path.
This does not execute every native server script or make static art interactive.

### Cell-boundary pursuit and shared chests (2026-10-29)

- Walking steps use native `LandDefs::adjust_to_outside` to rebase block-local
  coordinates, then native LOS traversal and floor queries to resolve the next
  cell. Tests exercise both 20 m cell edges and 160 m block edges against the
  installed Legions DATs. Three body rays and a bounded floor/step check remain;
  this is not the complete retail sphere/capsule movement solver or a pathfinder.
- Moving creatures update the content cell index. Existing observers receive
  position packets, newly interested observers receive CreateObject/current
  stats, and observers losing interest receive DestroyObject. Respawn restores
  the authored home and its index. A two-client UDP test also checks late login.
- Homesickness is checked against the original home before pursuing. On losing
  an eligible target, creatures retrace up to 4,096 verified steps, rechecking
  collision. Returning creatures cannot be attacked. A blocked return waits;
  there is no teleport through obstacles. The 2 m/s chase speed, restoration of
  health on disengagement, and return policy are AC2E behavior, not recovered
  retail AI. Aggression/faction policy and obstacle routing remain gaps.
- Unlocked, configured server chest identities publish native open/closed
  modes. Multiple viewers share the state; only the last viewer closes it.
  Logout, death, loss of visibility and leaving the four-metre use radius close
  the viewer's container. Stale close requests do not close another chest.
- The Unreal client resolves native EntityDesc base classes before applying
  door-specific collision changes. Opening a chest preserves its collision;
  reversing open/close state interrupts the previous behavior, including assets
  without a separate close animation. The rendered acceptance test exercises
  the native Order Cache animation/FX alongside door collision and shop input.
- Chest contents and encounter loot tables are unchanged. Empty containers are
  still empty where no supported contents exist; this pass adds no placements,
  invented retail stock, spawn schedules or faction relationships.

Validation: 98 Rust tests passed (four explicitly ignored legacy/converted-DAT
tests), including native geometry and the compatible capture pack. Real UDP
service tests cover shared viewers, logout, late arrivals and range cleanup;
the pursuit test covers two players, block crossing and walking home; corpse
loot tests cover ownership, take/all, replay protection and persisted grants.
The Unreal client builds and the rendered shop/door/chest acceptance passes.

The admin decompile describes object, script and interaction mechanisms. It does
not supply the absent live server population/quest database. A chest, door or sign
rendered as static artwork does not necessarily have a usable server object.
Existing validated server doors and vendors were retested, including replication
to a second player. Unbound scenery doors/signs/chests still require a matching
server identity and behavior/content definition; no visual-name heuristic turns
all matching meshes into interactive objects.

To recover exact spawns and quest bindings, useful inputs are a version-matched
server content database, administrative exports or captures of those locations.
The existing placement/content tools can also create explicitly authored AC2E
content, but such entries must not be described as verified retail placements.

Generate the reports without changing the game data:

```powershell
python Tools/AC2/population_probe.py "Asheron's Call 2 - Legions" --output Saved/NativeReview/population.json
python Tools/AC2/service_audit.py --population Saved/NativeReview/population.json --content AC2E/data/world/legions-world.tsv --output Saved/NativeReview/services.json
```

`services.json` records each selected object's identity, cell/block, coordinates,
resolved script class and available store/AI properties. Local generated reports
and retail inputs are ignored by Git.

## Supplied SQL archive and upstream snapshot

The user-supplied `5 AC 2 Pyreal SQL Database.7z` was extracted only under ignored
`Saved/PyrealSQL-2017`. The dump SHA256 is
`e70658d4856784ae8a032c81cac2664c014caf009c1cadbbe6387ba5c2e2dd0d`.
The read-only audit found 1,553 telepads, 160 portals, 89 lifestones, 150 player
levels and 1,750 crafting-level rows. Entity, quest, quest-phase, skill and
submission tables are empty. All 160 portals already match `portals.json`, so no
SQL was executed and no existing server data was overwritten. These tables are
useful corroboration and travel/progression references, but not a world spawn DB.

Upstream content is pinned to Pyrealium revision
`468793018b332b87228794b0720ea1ce0400f96d`. See
`AC2E/emulator/content/upstream-content.json` for hashes and import scope. The
native physics dependency remains the existing pinned AC2_Rusted revision; its
sources are now included in the server's corresponding-source archive.

Repeat the SQL check with:

```powershell
python AC2E/emulator/tools/audit_pyreal_sql.py Saved/PyrealSQL-2017/pyreal.sql --output Saved/PyrealSQL-2017/audit.json
```

## Validation

- Wound pass: all 45 AC2 data tests passed. The new tests cover customized
  race/sex profiles, actual bound DAT textures at every stage, native material
  exceptions, blood preferences, health/max ordering, explicit/stale FX,
  appearance rebuilds, healing, death and logout.
- `AC2.Render.ProgressiveDamageAppearance` passed with D3D11. It captures
  human/Drudge clean, five wound stages and healed frames, checking visible pixel
  changes and restoration. Captures are under
  `Unreal/Saved/Screenshots/DamageAppearance/`. The capture test warms first-use
  material shader permutations before measuring pixels.
- `Network/test_damage_appearance.py` passed against an isolated real UDP server:
  player and creature current/max health reaches both clients, returning observers
  receive current vitals without another hit, and creature healing reaches both.
- UE 5.8 Editor and game Development targets built successfully.
- All 43 AC2 data automation tests passed, including all 70 skill panels,
  native swim vertex movement, water thresholds, lift and friction.
- This continuation: 85 server library tests passed, one existing ignored test;
  the installed-DAT geometry integration test also passed.
- Bridge tests from the preceding pass: 17 passed.
- Isolated UDP tests passed for falling, corpse loot, gameplay and doors/vendors.
  The fall check verifies a safe small jump, a damaging eighteen-meter descent,
  no repeated charge and saved health after re-entry.
- Corpse tests verify single take, Loot All, close/reopen, foreign-owner and
  distance rejection, no duplicate grant and persistence of claimed inventory.
- The connected rendered smoke passed combat, actual native loot-window use,
  inventory dragging/equipping/drop/pickup, icons and hover interaction checks.
- The journal smoke verified native skill trees/quest pages, routed header dragging
  and a rendered portal-space capture. Creator palette screenshots were inspected.
- Fall accumulation regression: forty separate short drops and a forty-meter
  downhill walk followed by a small drop are harmless. The installed-DAT test
  also walks a native slope from 129.88 to 122.78 meters before a quick contact
  adjustment, verifying no inherited fall damage. Continuous large falls still
  apply damage only once, at verified contact.

Relevant repeatable checks:

```powershell
.\Launch-AC2.ps1 -Mode Test -SkipBuild
python Network/test_falling.py
python Network/test_corpse_loot.py
python Network/test_damage_appearance.py
python Network/test_gameplay.py
python Network/test_services.py
python Network/test_journal_ui.py
python Network/test_combat_ui.py --gameplay
```

These integration tests use isolated stores. They do not delete production
accounts or characters. They are functional and rendering checks, not proof that
every race, spell, world object or retail animation is complete.

## Loot, shortcuts, cursor and shoreline continuation (2026-10-30)

- Taking corpse loot from combat temporarily sends peace stance, commits the
  inventory operation, then restores combat. Rejected mutations also restore
  stance; neither path resumes attacks against the corpse. Ownership, range,
  persistence and duplicate-claim checks still apply.
- The corpse panel places its close button inside the right end cap and exposes
  the native Transmute All control alongside Loot All. Transmutation validates
  the native request, journals gold before consuming eligible loot, and leaves
  unsupported/quest items in the container. Prices currently use AC2E's bounded
  merchant appraisal policy; retail appraisal tables have not been recovered.
- Learned active skills drag from the book to any of ten shortcut pages. Page
  arrows, numbers 1 through 0, moving bindings and per-character persistence are
  connected. Untrained/untraining skills remain unavailable. Alt and middle
  click no longer toggle persistent mouse capture; held camera gestures remain.
- Six existing placed hostile templates now have combat/AI profiles recovered
  from their inherited DAT properties: Crawlspace Vermin (47001073), Drudge
  Digger (4700158b), Drudge Sentinel (4700158c), Fanatic Juggernaut (47003581),
  Swarm Drone Nymph (47003ad4), and Plains Bull (47003b67). The Volcanic Rift pet
  template was excluded. This fixes classification of existing spawner-managed
  actors; it does not create extra static placements or make town NPCs hostile.

### Explicit shoreline content repair

The user approved extending the adjacent lake into block **74A9**. Its DAT water
map specifies 70.95 m, below all of its terrain (minimum 90.5 m), while adjoining
75A9 and 74AA use 96.7 m. The native lake stops at the block boundary even where
the bank is below that surface. The admin/Legions `CLandBlockData::CacheFarValues`
and admin `LScapeTerrainMesh::UpdateNearWater` read these same per-block values;
they do not interpolate a missing shoreline. This is an inferred content repair,
not a claim that a different retail water algorithm was recovered.

`FAC2WaterSource::At` borrows 75A9's native height for the dry, connected 74A9 bank.
Near/far water rendering and client swimming therefore use one plane. Server
`Geometry::water_height` uses the same adjacent plane so terrestrial spawn
validation rejects submerged ground. The west and south boundaries are above
the lake, and nearby 73A9 remains dry. Existing DAT water wins if a later dataset
restores 74A9. No DAT files or authored scenery placements were edited.

### Verification for this continuation

- Server library: 118 passed, one existing ignored test.
- Installed-DAT geometry: five passed, including repaired water height,
  submerged spawn rejection, and the unrelated western dry block.
- `AC2.Data.RepairedShoreline74A9`: passed, checking both lake seams, native
  material identity, the reported location and dry outside boundaries.
- Offscreen shoreline smoke passed at 9670 cm. Inspected bank and overhead
  captures show the extended water meeting the terrain. Client editor/game,
  network bridge and game-server release builds succeeded; the account service
  was left running on its separate executable.
- Real UDP corpse-loot test: take/Transmute All from combat, stance restoration,
  owner/range/replay rejection and persistent inventory/gold passed.
- Real UDP outdoor-population test: the exact reported Crawlspace Vermin
  placement accepts combat with its native 81 health; pursuit/retaliation and
  protected town NPC checks passed.
- Rendered journal smoke: learned-skill drag, slot move, page arrows/indicator,
  train/raise/untrain and quest tabs passed. Shortcut smoke restores bindings
  and suppresses preference writes while exercising the test page.
- Rendered combat/mouse smoke: combat, corpse pickup, close-button placement,
  both loot controls, visible cursor, inert Alt/middle toggle and held camera
  gestures passed. The wrapper's old empty-corpse assertion was updated to also
  accept its already-supported populated-loot path after variable loot was added.

For a focused offscreen shoreline capture, launch UnrealEditor with the project,
`-game -RenderOffscreen -unattended -nosound -AC2EnvironmentSmoke
-AC2ShorelineSmoke -AC2Block=74A9 -AC2Radius=2 -AC2Hour=12` and `-AC2DatDir`.
The smoke checks the streamed swimming surface, then saves bank and overhead
views under `Unreal/Saved/Screenshots/AC2Environment-shoreline-*.png`.
