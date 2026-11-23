# Windows releases

The public client is the Windows x64 Shipping target. The launcher and game can be installed per user. The launcher stores its DAT folder and update preference in HKCU and launches the game with user-local saves.

Run `Build-Windows.ps1` from PowerShell on a Windows machine with Unreal Engine 5.8, the Visual C++ toolchain, rustup, Python 3, .NET Framework 4.8 and Inno Setup 6. Initialize the submodule first. The script builds the standalone bridges, generates the engine materials, cooks and archives the game, exercises the update verifier, then creates an installer and the matching public page and manifest in `Saved/PublicRelease/<version>/`. The game directory must be dedicated to that build. Supply your data directory when testing the installed game.

Publish the exact installer and checksum under the GitHub release tag `v<version>` in Thwargle/ac2e, then deploy the generated `site` directory to the root of GitHub Pages. The feed uses a version-specific installer URL and is published only after that installer is available. Never replace an existing version with different bytes. Publishing a higher version is what triggers updates for existing players.

The launcher validates the feed product, version, exact GitHub asset URL, transferred byte count and SHA-256. It follows HTTPS redirects only to GitHub release asset hosts. A cancelled or failed transfer is discarded. The user can always start the installed game when the update service is offline. Player-supplied DATs and accounts are separate from client updates.

`UpdateTests.cs` covers malformed metadata, unexpected destinations, byte counts, altered content and cancellation. After building, install to an empty directory and launch with real DATs. For an upgrade smoke test install an older build in an isolated Windows profile, opt in, and verify the new version opens with the saved preferences.

