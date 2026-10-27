# AC2E — Asheron's Call 2 Emulator

AC2E is this workspace's persistent multiplayer server project, derived from Pyrealium `7c2e856840b04302d1c78f057dcf8094326472a4`. It serves the Unreal AC2 client over the native protocol. It is a Rust server, independent of the AC1 ACE code and database; it is not an ACE plugin or a claim of complete retail AC2 emulation.

## Running

Run `start-ac2e.bat` from the workspace root, or `AC2E/Start-AC2E.ps1`. This builds AC2E, prepares a compatible world placement file from the installed DATs, and listens on 127.0.0.1 UDP 9000 and 9001. In another terminal run `Launch-AC2.ps1 -Online`. The client builds its native bridge if missing. The server owns character creation, progression and inventory. All local-development clients use account `local` by default.

Parameters: `-Port`, `-Account`, `-DatDirectory`, `-DataDirectory`, `-Content`, `-Python`, `-SkipBuild`. Defaults resolve relative to the project directory, regardless of the current working directory. Graceful shutdown: `Start-AC2E.ps1 -Stop`. Back up `data/world` with the server stopped. The store uses append-only journals with exclusive writer locks, validated replay, atomic transactions and persisted placements; it is not a MySQL schema. The CLI also exposes store inspection and compaction commands inherited from Pyrealium.

## Initial gameplay ruleset

The default server loads 15,116 DAT-compatible placements from the retained population and authored starter content. Twenty-two rows reference assets absent from this Legions installation and are excluded with a report next to the generated TSV. Seven inactive authored portal test fixtures are also excluded; four compatible lifestone fixtures are enabled along with the server's native binding/recall handlers. Recovered world placements are retained. Static NPC/portal placements do not imply complete NPC scripting.

The authored starter encounter supports targeting, range-checked repeated attacks, retaliation, health/vigor, mob death/despawn/respawn, XP, gold and automatic potion loot. Rewards and loot commit together before publication and survive logout/restart. The initial ruleset deliberately does not enable the upstream merchant/crafting/training-kit catalogs, which reference records absent from these DATs. Native loot-container protocol requests are forwarded by the client, but retail corpse generation/looting and complete quest/merchant behavior remain separate work. PvP, full original AI, server terrain collision and complete anti-cheat enforcement are not implemented by this integration.

The native server/client layer also supports persistent character appearance, multiple players, scene visibility, cell changes, server position corrections, local/tell/emote chat, owned inventory replication, item use/equipment requests, death/resurrection, and authenticated ticket login. Client compatibility details and verification commands are in `../Network/README.md`.

Unarmed starter attacks now publish native martial-arts mode and attack stories;
supported starter swords retain one-handed mode. The Idol uses Melee_Normal1,
which exists in its Legions animation map, instead of the unavailable Auto1 key.
The scene snapshot retains the selected combat mode for observers entering later.

## Authenticated hosting

Start `Start-Accounts.ps1` to open the account backend at `http://127.0.0.1:19400`. Its page can register accounts. Run the game server with `-AuthDirectory AC2E/data/accounts` (use the absolute path when starting elsewhere). Ticket mode rejects development/no-ticket login. The client uses `Launch-AC2.ps1 -Online -AccountUrl http://127.0.0.1:19400 -Account NAME`.

For external hosting, put the loopback account backend behind a TLS reverse proxy, pass the same `-PublicOrigin https://accounts.example.com` to both scripts, set the account service's `-GameHost` to the advertised IPv4 hostname, and bind the game server with `-Bind 0.0.0.0 -AuthDirectory PATH -PublicOrigin https://accounts.example.com`. The proxy must overwrite `X-Forwarded-For` with the client's single IPv4 address; tickets bind to that address. Publish both UDP ports. No firewall/router settings are changed by these scripts. Remote deployment itself has not been performed.

The account backend serves `/source.zip` with the source used to build this modified server, preserving the AGPL source offer. Build after changing source; do not point the offer at unmodified upstream. `export_source.py` packages the current server source, docs, dependency lockfile, and build/launch helpers using explicit source directories. It works in this monorepo and in an unpacked source download, and excludes game DATs, account state and journals.

## Implementation notes

- The opt-in `PYREALIUM_UNREAL_LEGIONS_DATA=1` profile recognizes the supplied Legions state fingerprints. It does not modify DAT bytes or claim compatibility with the original 7148 client VM. Unreal decodes wire data and does not execute the merchant/group/allegiance WLIB packages whose retail layouts differ.
- Skill metadata (1,927 resolved definitions; 186 unresolved) and 30 advancement curves were regenerated with the existing strict DAT readers. Unsupported records remain unsupported. Server metadata validation remains active for supported skills, inventory and loot.
- Ordered network counters wrap as native 16-bit stamps; a reordered rollover is covered by a transport regression test. Client re-entry keeps the upstream-compatible incoming sequence.
- `emulator` retains the `pyrealium` library name for existing tests/modules; the executable is **ac2e.exe**. Original licensing/notices remain intact. `UPSTREAM.md` describes upstream features, not the completion level of this AC2E/Unreal ruleset.
- Upstream native-client launcher assets and launcher-specific startup scripts are omitted. The `deploy/` templates and upstream documentation remain reference material for porting; use the AC2E scripts above for this Windows/Legions setup.

Build the server independently with `Build-AC2E.ps1 -Test`, or build the server and Unreal transport bridge with `../Build-AC2Network.ps1 -Test`. Protocol/render integration tests are in `../Network`; they use isolated temporary stores. The source archive can be regenerated with Python `export_source.py`, then `cargo build --release --locked --manifest-path emulator/Cargo.toml --target-dir target`.
