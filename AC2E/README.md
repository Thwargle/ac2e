# AC2E server

The Rust server for the AC2 Unreal client, derived from Pyrealium. Start with
[AC2E setup and hosting](AC2E-README.md) for local play, accounts, supported
combat and persistence, and current limitations.

The Character panel shows earned quest points. AC2E pays documented awards on
completion and saves the total with the character. The [quest reward catalog](emulator/content/quest-point-rewards-v1.json)
lists the evidence for each amount. The [world atlas](../Docs/Atlas/index.html)
shows which guide quests have server scripts and which objectives still lack a verified route.

From the repository root:

```powershell
.\AC2E\Build-AC2E.ps1 -Test
.\AC2E\Start-AC2E.ps1
```

See [the client and repository setup](../README.md) and [networking](../Network/README.md).
The older [Pyrealium documentation](UPSTREAM.md), `emulator/docs/`, and `deploy/`
are retained as upstream references. The old native-client launcher is not included.

Server code remains AGPL-3.0-only. Original notices and dependency licenses are
preserved in [LICENSE](LICENSE), [LICENSING.md](LICENSING.md), [NOTICE](NOTICE),
and `LICENSES/`.
