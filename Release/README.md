# Windows releases

The public client is the Windows x64 Shipping target. The launcher and game can be installed per user. The launcher stores its DAT folder and update preference in HKCU and launches the game with user-local saves.

Run `Build-Windows.ps1` from PowerShell on a Windows machine with Unreal Engine 5.8, the Visual C++ toolchain, rustup, Python 3.11 or later, .NET Framework 4.8 and Inno Setup 6. Initialize the submodule first. The script builds the standalone bridges, generates the engine materials, cooks and archives the game, exercises the update verifier, then creates an installer and the matching public page and manifest in `Saved/PublicRelease/<version>/`. The game directory must be dedicated to that build. Supply your data directory when testing the installed game.

Publish the installer, RELEASE-NOTES.txt and SHA256SUMS.txt together under `https://thwargle.com/downloads/ac2/v<version>/`. Publish the generated site directory at `https://thwargle.com/ac2-unreal/` after verifying the uploaded files. Supply `Release/notes/<version>.txt` before building each release. Keep versioned files immutable and publish the new release.json last to enable updates.

The launcher checks the Thwargle HTTPS feed, exact versioned installer URL, transferred byte count and SHA-256. It accepts installer redirects only within thwargle.com over HTTPS. A cancelled or failed transfer is discarded. Players can start their installed game when the service is offline. Users of the original 0.11.0 launcher must run the new installer once to change update hosts. It preserves the saved DAT folder and update preference.

`UpdateTests.cs` covers malformed metadata, unexpected destinations, byte counts, altered content and cancellation. For an isolated install without changing shortcuts or uninstall registration, pass `/AC2PORTABLE /DIR="C:\path\to\empty-folder"` to Setup. The launcher preserves portable mode when it updates. After building, install to an empty directory and launch with real DATs. For an upgrade smoke test install an older build in an isolated Windows profile, opt in, and verify the new version opens with the saved preferences.

