# Retail reference probe

This read-only tool executes `SetCGRDefaults`, `InqValidPhysTypeValues`, and
`InqRaceSexInfoData` from
the installed Legions DAT script library through the pinned AC2_Rusted VM. Its
output is an independent reference for the Unreal character-generation port.
It does not edit the DATs, server state, or reference checkout.

From the repository root, with the project Rust toolchain on PATH:

```powershell
cargo run --release --manifest-path Tools/RetailProbe/Cargo.toml -- "C:/dev/ac2e/Asheron's Call 2 - Legions" > Saved/retail-chargen-defaults.json
```

Run Unreal's `AC2.Retail.CharacterDefaults` automation test with
`-AC2CharacterOracle=C:/dev/ac2e/Saved/retail-chargen-defaults.json` and the same
`-AC2DatDir`. This compares every supported physique-choice list and initial
choice for all ten race/sex profiles, including their randomized appearance and
midpoint height/build, physics object, and held-object lists. The current Legions
script reports empty held-object lists for every avatar; the test preserves that
result rather than inventing preview equipment. A missing reference is an error,
not a skipped pass.

The pinned `valid_phys_type_values` wrapper pushes its three parameters in
declaration order. Both local client decompiles show the native caller pushing
physique type, sex, then race. The probe compensates at its call boundary; the
reference checkout is unchanged.

The Unreal port keeps a private character-creation random stream. Agreement
with the probe verifies the algorithm and choice ordering from the same initial
state. It does not reproduce every random call in the retail process or promise
the same randomly selected clothing as a particular video frame.

Keep the generated JSON under ignored `Saved/`. The tool contains no extracted
DAT records, native code, or video assets. Re-run it when changing datasets or
updating the pinned reference VM.

## World-object use request

`--usage` executes the installed DAT's `UsageDesc::37` constructor with its
class-default WState, following `CUsageSystem::09` / `WM_Usage::22`. It uses
synthetic player/object IDs and no target list, as when double-clicking an NPC
or training stone. Append `targeted` after `--usage` to execute the same constructor with one 64-bit target in the native list, as when using a Menhir Glyph on a portal. The same comparison test accepts both outputs. The probe runs offline.
An interpreter failure returns an error instead of producing a valid oracle.

```powershell
cargo run --release --manifest-path Tools/RetailProbe/Cargo.toml -- 'D:\Games\Icefire' --usage | Out-File -Encoding utf8 Saved/usage-oracle.json
$env:AC2_USAGE_ORACLE = (Resolve-Path Saved/usage-oracle.json).Path
cargo test --manifest-path Network/Bridge/Cargo.toml native_use_matches_installed_dat_constructor -- --ignored
```

The opt-in test compares the bridge's actual outgoing request with the entire
constructor-produced graph, including defaults, flags, unsigned ID tags and
the result message. Only process-local reference indices are remapped. Run it
separately for each DAT installation; this checks request compatibility, not
server-side quest implementation or the original client's rendered behavior.

`--use-selection ENTITY_DID` additionally executes `CPlayer::Select`, a repeated
selection, `ggmCWInterface::CallIFun_UseSelection`, and clearing the selection.
It loads the selected entity's actual WState and a class-default player, captures
outgoing script requests, and fails if execution faults or drops a host call.
For example, using a local Icefire dataset containing Shelter Sentry:

```powershell
cargo run --release --manifest-path Tools/RetailProbe/Cargo.toml -- 'D:\Games\Icefire' --use-selection 47002e05 | Out-File -Encoding utf8 Saved/selection-oracle.json
$env:AC2_SELECTION_ORACLE = (Resolve-Path Saved/selection-oracle.json).Path
cargo test --manifest-path Network/Bridge/Cargo.toml icefire_selection_and_use_match_installed_dat_path -- --ignored
```

The Icefire-specific test checks both general and combat selection notifications, repeated-target
suppression, the explicit examination request, and the complete use descriptor.
Entity IDs vary by dataset; a missing WState is an error. This offline sequence
does not replay live player qualities, quest state, server responses, or dialogue
rendering. Stock Legions emits only the general selection request in this path;
use `--usage` for the shared descriptor comparison. Keep generated references
under ignored `Saved/`.

`--use-selection <entity DID> --combat --selected` executes the installed
`CPlayer::RequestAttackStateChange` path after selecting a synthetic object.
It emits the enter/leave-combat requests for independent comparison with the
bridge (`AC2_COMBAT_ORACLE`, ignored test `native_stance_uses_the_current_selection`).
Omit `--selected` to examine the null-target case. The harness supplies only the
physics StopBehavior return value; it does not simulate combat or contact a server.

`--move-desc` executes `InvMoveDesc::3F` for a synthetic corpse-to-player transfer,
preserving the native constructor's unsigned ID and ordinal tags. Set
`AC2_MOVE_ORACLE` to its JSON output to run the ignored
`native_loot_transfer_matches_installed_dat_constructor` test. Both oracles were
checked against the locally patched Icefire DAT snapshot. No recovered binary
fixtures or DAT contents are committed.

`--move-desc drop` and `--move-desc give` instead use a synthetic player-owned
stack of three items. They execute the same constructor, then its native move-type
setter with 33 (creature to world) or 36 (creature to creature), respectively.
The ground target is IID/slot zero; the gift target is a synthetic recipient IID
with slot zero. This follows the reviewed `DirectiveMoveItem` branches without
simulating its entire UI, live inventory state or server transaction.
Set `AC2_DROP_ORACLE` or `AC2_GIVE_ORACLE` to the corresponding JSON and run the
ignored `native_ground_drop_matches_dat_constructor_and_direction` or
`native_give_matches_dat_constructor_and_direction` bridge test. Both modes were
checked separately against stock Legions and the patched Icefire snapshot.

`--bulk-desc` executes the named `InvTakeAllDesc` and `InvTransmuteAllDesc`
constructors in the selected DAT. Stock Legions and Icefire allocate different
package IDs for these equal-sized layouts. Set `AC2_BULK_ORACLE` to its JSON and
`AC2_BULK_DAT` to the same directory, then run the ignored test
`native_bulk_requests_match_selected_dat_constructors` in `Network/Bridge`.
It checks class identities, defaults, unsigned IDs and request functions without
connecting to either server.

Pass `--clock` after the DAT directory to run the pinned `GameTime::UseTime`
implementation at day/year boundaries and long elapsed times instead of the
character-generation VM. For example:

```powershell
cargo run --release --manifest-path Tools/RetailProbe/Cargo.toml -- "C:/dev/ac2e/Asheron's Call 2 - Legions" --clock > Saved/SkyTimeAudit/clock-reference.json
```

`AC2.Data.NativeServerCalendar` includes the resulting Legions calendar vectors.
Weather hashing is checked separately against the Legions decompile: its high
16-bit hash differs from the pinned reference's full-word weather implementation.

## Content recovery

`--content` decodes client-retained quest WStates, localized journal phases,
related quest IDs, and quest-effect status words. It also catalogs virtual method
offsets, distinguishing retained bodies from empty stubs and trap instructions.
Empty function-name exports do not mean that bytecode is absent.

`--code QuestMucorMushrooms QuestKillGreenWaspStriker Generator` lists the named
packages' own virtual bodies, resolved call targets and string literals. It does
not run the scripts. Numeric selectors without recovered names stay numeric;
constants passed to empty server setters are not labeled as XP or rewards by guess.
Append `+` to a package name (for example `CPlayer+`) to include inherited
bodies. `function_ids` lists virtual selectors resolving to each body in that
instance's vtable; it is not proof that every selector's interface is supported.
`--code @114260` lists instructions from a hexadecimal byte offset up to the
next known virtual body boundary, for following retained static calls. That
boundary can include adjacent static functions; interpret returns explicitly.
`--state 70001678` dumps a WState reference graph; `--strings 250014c9` inspects a
native localization table. Keep exploratory listings in ignored `Saved/`.

`--loose-strings DIRECTORY 25000ec2 25020ec2` decodes historical loose
StringTable records named `0x25000EC2`, using the same pinned packed decoder.
It reports private-table redirects and variables without formatting or executing
them. Each input must be a StringTable DID, the embedded DID must match, and
records larger than 16 MiB are rejected. This mode is used by
`recover_quest_text.py` to fingerprint and follow missing private strings; it
does not install an older translation over the current DAT.

The checked-in authoring catalogs are generated by
`AC2E/emulator/tools/import_native_quests.py` and `import_native_trophies.py`.
See `Docs/ADMIN_CONTENT_RECOVERY.md` for runtime scope and unresolved dependencies.
