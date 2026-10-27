# AC2 native networking (0.11.0)

The Unreal client connects to AC2E and Pyrealium using native AC2 UDP, not Unreal replication. The separate Rust bridge uses AC2_Rusted's checksummed, reliable native transport and codecs. Private anonymous pipes carry JSON between Unreal and the bridge; no IPC network port is opened. Object IDs remain decimal strings across JSON to preserve all 64 bits.

## Start a local server and client

From the workspace root, in separate terminals:

```powershell
.\AC2E\Start-AC2E.ps1
.\Launch-AC2.ps1 -Online -Server 127.0.0.1 -Port 9000 -Account local
```

The root `start-ac2e.bat` starts the same server project, reusing the built binary and building only if it is missing. Use `-Rebuild` after changing server source; `-SkipBuild` prevents even a missing-binary build. Create a character in Unreal; it is saved by AC2E under `AC2E/data/world`, independently of offline characters. Stop gracefully with `AC2E/Start-AC2E.ps1 -Stop`.

For a Pyrealium server with an account service:

```powershell
.\Launch-AC2.ps1 -Online -AccountUrl https://accounts.example.com -Account myaccount
```

The game opens its account/password form before loading any environment. Enter the account-service URL and password there; the client obtains the server's one-use native ticket with Windows WinHTTP. HTTPS is required except on loopback, and redirects are disabled. Passwords and tickets are not put on command lines or in client logs/save files. Tickets go to the bridge through private stdin. For servers that accept credentialless native login, leave the account-service URL/password blank and enter the host/port. With password login, host/port come from the trusted account endpoint. Explicit launcher `-Credential` automation remains supported through a child-process environment ticket. Public Internet interoperability has not been tested against an independently operated host.

## Network controls and authority

WASD/Shift/Space move. Enter opens server chat (`/say`, `/me`, `/tell Name, message`). T cycles nearby server objects; 1 requests repeated attacks; `/stop` ends combat. 2 uses the selected world object, including lifestone binding. Home or `/recall` requests native lifestone recall; a dead character requests resurrection. I opens the native inventory; double-click an item to use/equip it; selecting an item and an empty cell requests a server move. C displays server stats. F4 or Escape → Leave World logs out to character selection. Escape on selection disconnects to sign-in. `/loot` sends the native loot-container request when supported by the connected server.

Server characters, positions, visuals, appearance aspects, actor visibility, inventory, health/vigor, levels, XP and gold update from server events. Remote actors interpolate server movement and use native locomotion animation. Client collision and prediction are retained for local movement. NPC test spawning, flight and arbitrary map teleports are disabled online. Inventory changes wait for acknowledgement; no network profile is written to the offline character file.

The AC2E initial ruleset tests **automatic loot grants** on supported mob kills. It does not yet supply a complete retail corpse-looting window, every combat animation, every merchant/quest/social panel, terrain-authoritative server collision or anti-cheat parity. Static recovered placements and authored encounter behavior are distinct; seeing an NPC does not mean its full original script has been implemented.

## Build and verify

`Build-AC2Network.ps1 -Test` builds the bridge and AC2E and runs their Rust tests. Install Rust with rustup so `cargo` is on PATH; an optional local installation in `Tools/Rust` is also supported. Python defaults to the bundled runtime if present, otherwise `python`; use `-Python` for another installation. Build Unreal with `Launch-AC2.ps1 -Mode Build`.

The bridge dependency is pinned at AC2_Rusted commit `bcf67b4b9ac3ee9eb429d68ba6766cfce47c5a25`, in `References/AC2_Rusted`; `Cargo.lock` pins registry dependencies. Initialize it with `git submodule update --init --recursive`, or clone this repository with `--recurse-submodules`. Do not silently update that checkout when building.

`Network/test_network.py` uses two native UDP clients and a unique temporary store to verify creation, visibility, movement, chat, mob death, loot, logout, re-entry and persistence across a full server restart. `Network/test_auth.py` verifies account login, wrong-password rejection, ticket replay rejection, anonymous rejection and the source archive. They use loopback ports 19270/19271 and 19280/19282/19283 respectively. Results are in `Saved/AC2NetworkTests`.

All integration scripts generate their compatible content in the temporary test
directory. Set `AC2_DAT_DIRECTORY` to use a retail installation outside this
checkout; no pre-existing `Saved` files are needed.

`Network/test_server_launcher.py` runs the public batch launcher from another
working directory, connects a native client, and stops the server gracefully.
It verifies that normal startup reuses the built server without invoking Rust,
repeated startup reports the existing process without rewriting live content,
rebuilding a running server is rejected, and unverified locks are preserved.

`Launch-AC2.ps1 -Online -Server 127.0.0.1 -Port 9000 -SkipBuild -NetworkSmoke` runs the Unreal render acceptance test against a **test server with an existing human character**. It sends chat, attacks a nearby authored training mob, receives loot, captures the native world/inventory and exits. Use isolated server data: this test intentionally changes its character's progress.

Compatibility details: Pyrealium accepts unencrypted character creation on queue 4, and retains the incoming ordered counter across character logout. The bridge adapts these two behaviors without altering AC2_Rusted or the upstream reference. It decodes Pyrealium's current admin7148 selection layout even when using the experimental 7230 login profile.

Native behavior/mode, damage story, death and impulse events now reach the model/controller.
The bridge preserves instance stamps and 64-bit IDs, including snapshot animation state.
Movement/acceleration/jump scales, global modifiers and motion-descriptor changes apply
to the local player. Item use preserves native unsigned object-ID tags (0x84); lifestone
use has no target and the stone as the item. Recall sends skill 0x343 through the native
active-skill request; the server decides whether and where it succeeds.

`Network/test_login_ui.py` tests the complete in-client password form against isolated
AC2E account/game processes on 19480/19482/19483. It binds and recalls through the native
protocol, then verifies named character selection before the environment, successful
attack/hit/death animation playback, XP/loot and native inventory/vitals. It uses a
random temporary password passed only through the child environment and fresh stores.

## Sources and licenses

AC2E derives from Pyrealium commit `7c2e856840b04302d1c78f057dcf8094326472a4`. The separate bridge and both upstream projects are AGPL-3.0-only. Their original notices and licenses are preserved. AC2E's account backend serves the corresponding server source at `/source.zip`; rebuild it after source changes. The original game DATs are read-only local inputs and are excluded from the source archive. See `AC2E/AC2E-README.md` and `Network/Bridge/NOTICE.md`.
