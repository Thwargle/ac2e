# AC2:Unreal and AC2E

AC2:Unreal is a Windows client for Asheron’s Call 2 built with Unreal Engine 5.8. It renders the world from the DAT archives on your computer. AC2E is the Rust multiplayer server in this repository, derived from Pyrealium. The client also has a separate profile for the community-operated Icefire server.

**[Download the Windows client](https://thwargle.github.io/ac2e/)** · [Versioned installers and checksums](https://github.com/Thwargle/ac2e/releases) · [Host AC2E](AC2E/AC2E-README.md)

## Play

1. On 64-bit Windows 10 or 11, run the setup program. Use a DirectX 12 graphics adapter with Shader Model 6 support. The installer offers Microsoft runtime prerequisites where needed.
2. Start **AC2 Unreal** and choose your existing Asheron’s Call 2 data folder. Keep the installation together. The client checks for `portal.dat`, `cell_1.dat` and `local_English.dat`. Game data must be supplied separately.
3. Select **Play**. Choose offline exploration, a local AC2E server, or a configured community server. AC2E’s local profile expects a server on your computer. For hosted play, use that operator’s address, account method and compatible DAT version. Icefire uses its own accounts and data revisions.
4. To receive client updates, use **Check for updates** in the Windows launcher or enable **Automatically download and install updates**. Automatic updates are off by default. The launcher checks on opening and every 15 minutes while idle, verifies the installer’s size and SHA-256, and gives you eight seconds to cancel before it closes for installation. Your DAT directory and player settings are stored separately from the installer.

## What works today

AC2E supports accounts, persistent characters, combat and skills, inventory and corpse loot, crafting, travel, chat, social groups, and playable quest routes. Coverage varies by creature, skill and quest. The atlas distinguishes guide evidence from implemented routes. AC2E and Icefire run different server implementations, so support on one can differ from the other. This is a community preview. The 0.11.0 installed build passed launcher and update testing. Two extended offline gameplay checks remain open: rain emission at block 5667 and a scripted attack at C654. [Release notes and installer checksum](https://github.com/Thwargle/ac2e/releases/tag/v0.11.0)

- [Client behavior, renderer and controls](Unreal/README.md)
- [Server setup, gameplay coverage and hosting](AC2E/AC2E-README.md)
- [Network profiles, account setup and protocol versions](Network/README.md)
- [World and quest atlas](Docs/Atlas/README.md)
- [Development and verification notes](DEVELOPMENT-NOTES.md)

## Build from source

Install Git, Rust via rustup, Python 3, Visual Studio with the MSVC C++ toolchain and a Windows SDK. Building the client also requires Unreal Engine 5.8. Clone the pinned dependency along with the project.

~~~powershell
git clone --recurse-submodules https://github.com/Thwargle/ac2e.git
cd ac2e
.\Launch-AC2.ps1 -Mode Play -DatDirectory 'D:\Games\Asherons Call 2'
~~~

To build and run the server separately, supply the Legions DATs for its world data. Defaults are loopback game port 9000 and account port 19400. The startup script keeps accounts and character state on the host.

~~~powershell
.\AC2E\Build-AC2E.ps1 -Test
.\AC2E\Start-AC2E.ps1 -DatDirectory 'D:\Games\Asherons Call 2'
~~~

The client build can be exercised with `.\Launch-AC2.ps1 -Mode Test -DatDirectory 'D:\Games\Asherons Call 2'`. For the distributable build and update feed, see [Windows release packaging](Release/README.md).

## Credits and licenses

AC2E retains Pyrealium’s AGPL-3.0-only license and notices. The standalone network and audio programs use the pinned AC2_Rusted project. Component licenses and corresponding-source instructions are included with the download. Unreal Engine is distributed under Epic’s terms. Original game assets and trademarks belong to their respective owners. [AC2E notices](AC2E/LICENSING.md) · [Network bridge](Network/Bridge/NOTICE.md) · [Audio renderer](Audio/Bridge/NOTICE.md)

