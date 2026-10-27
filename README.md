# Asheron's Call 2: Unreal client and AC2E server

AC2E combines an Unreal Engine 5.8 client for Asheron's Call 2 with a persistent
Rust server derived from Pyrealium. The client is in `Unreal/`, and the server is
in `AC2E/`. This is an experimental implementation, with offline exploration and
native multiplayer; it is not yet complete retail emulation.

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
newer. The verified Rust toolchain is 1.96.1. Windows is the supported client
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

The client opens its login screen before character selection and world loading.
Use offline exploration or connect to an AC2E/Pyrealium server. See the guides for
account setup, hosting, supported gameplay, and remaining implementation limits.

## Build and verify

```powershell
.\Launch-AC2.ps1 -Mode Build
.\Launch-AC2.ps1 -Mode Test -SkipBuild
.\Build-AC2Network.ps1 -Test
python .\AC2E\test_export_source.py
```

The launcher creates Unreal material assets from C++ on first play. Build output,
generated materials, accounts, and characters are ignored by Git. The server
creates its compatible placement file from the local DATs when it starts.

Network integration tests prepare their own content and isolated stores:

```powershell
# Optional when the DATs are outside this checkout:
$env:AC2_DAT_DIRECTORY = 'D:\Games\AC2'
python .\Network\test_network.py
python .\Network\test_auth.py
```

Pass `-Python 'path\to\python.exe'` to the build/server scripts if Python is not
on PATH. Pass `-EngineRoot` to the client launcher for another UE 5.8 location.

## Project files

- [Client guide](Unreal/README.md) and [Unreal project](Unreal/AC2Unreal.uproject).
- [AC2E setup and hosting](AC2E/AC2E-README.md).
- [Networking guide](Network/README.md) and the native transport bridge in `Network/Bridge/`.
- [DAT and decompile investigation](Docs/AC2_CLIENT_FOUNDATION.md).
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
