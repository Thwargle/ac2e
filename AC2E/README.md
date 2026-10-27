# AC2E server

The Rust server for the AC2 Unreal client, derived from Pyrealium. Start with
[AC2E setup and hosting](AC2E-README.md) for local play, accounts, supported
combat and persistence, and current limitations.

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
