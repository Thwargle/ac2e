# GUN service protocol

AC2 clients reach the MSN Gaming Zone "GUN" service through `GunDll.dll` (`-gunserver HOST
-gunport PORT`, default port 2310 in the launcher's `ac2l.ini`). Microsoft's own session layer
(WSOCK32 sockets and a CryptoAPI RSA key exchange inside `GunDll.dll`) is not decoded, and the
original servers are gone, so AC2e serves GUN on a transport of its own that keeps the retail
message names and ids. Two clients speak it: `gun-stub` (a `GunDll.dll` replacement for the retail
client) and `ac2-net`'s `gun` module (AC2_Rusted). It starts with every `ac2e serve`, on the
game address's IP at port 2310, so the players on a server always have friends, ignore lists,
presence and tells there; `--gun-bind ADDR` moves it and `--gun-bind off` turns it off. A shared
GUN server for players of several game servers is the clients' choice: each client's
`-gunserver` / `-gunport` points at it instead.

## Framing

TCP. A frame is a big-endian `u32` payload length (1 to 65536), then that many bytes of UTF-8:
one XML element. Attributes use double quotes and `&amp; &lt; &gt; &quot;` plus `&#N;` for control
characters. No declaration, comments or text; children are flat (one level). Requests carry
`seq="N"` and the answer echoes it. `hr` is `0x%08X`, the `ZT_*` HRESULT (0 is success).

Golden frame (`FriendAdd`, seq 3):

```
00 00 00 24  <FriendAdd Name="Bob@AC2e" seq="3"/>   (length 0x24 = 36 bytes of text)
```

## Messages

Names are the retail `GunDll.dll` element names (`decompiled-pdb/gundll/gun_messages.csv`).

| Client sends | Server answers | Notes |
|---|---|---|
| `<Connect ProtoVer="1"/>` | `<ConnectRes hr Random="32 hex"/>` | no `seq`; the client then raises `ConnectEv` itself |
| `<PresentTicketCmd seq Ticket="hex"/>` | `<PresentTicketRes seq hr Status="0" Account/>` | then `<FStart>` with `<Friend/>` children when the account has friends |
| `<ReleaseTicketCmd seq/>` | `<ReleaseTicketRes seq hr/>` | |
| `<GetFriendsList seq/>` | `<GetFriendsListRes seq hr><Friend Name Mode Location/>...` | |
| `<FriendAdd seq Name/>` / `<FriendDelete seq Name/>` | `<FriendAddRes seq hr/>` / `<FriendDeleteRes seq hr/>` | add also pushes `<FriendStat/>` for the new friend |
| `<GetIgnoredList seq/>` | `<GetIgnoredListRes seq hr><Ignored Name/>...` | |
| `<IgnoredAdd seq Name/>` / `<IgnoredDelete seq Name/>` | `<IgnoredAddRes/>` / `<IgnoredDeleteRes/>` | |
| `<Flags seq Op Which Flags/>` | `<FlagsRes seq hr WhichFlag Flags/>` | Op 0 get, 1 set, 2 clear; Which 0 control, 1 state |
| `<SetLoc seq Flags Location/>` | `<SetLocRes seq hr/>` | the presence mode bits and a place |
| `<SendToUserEx seq Name Text Flags/>` | `<SendToUserExRes seq hr/>` | the target gets `<UTellEv From Text Flags/>` |

Server events (no `seq`): `<FStart>` (friends snapshot), `<FriendStat Name Mode Location/>` (a
friend's mode changed), `<UTellEv From Text Flags/>`.

Any request before a successful ticket is answered `<NameRes hr="0xC112080B"/>` (`ZT_E_NOT_TICKETED`).
Mode bits: 1 logged in, 2 away, 4 do not disturb, 8 in game, 16 in chat. A session's mode is its
`SetLoc` flags plus 1; a character in the world adds 1 and 8.
Error codes used: `ZT_E_ALREADY_IN_LIST` 0xC1121009, `ZT_E_NOT_IN_LIST` 0xC1121401,
`ZT_E_AUTH_INVALID_TICKET` 0xC1120806, `ZT_E_ALREADY_TICKETED` 0xC112080C, `ZT_E_INVALID_USER`
0xC1120012 (tell to someone not here), `ZT_E_INVALID_NAME_LENGTH` 0xC1120019, `ZT_E_TOO_MANY`
0xC112001A, `ZT_E_INVALID_ARG` 0xC1120013.

## Identity and presence

A ticket is the opaque bytes the client presents. Developer mode (no account store, loopback bind
only): the account name. With `--auth-data` or `--local-accounts`: `account:token`, a token from the
account store (`tickets/`), spent once and bound to the connecting IP.

Friends are keyed by the part of `Name@World` before the `@`, case-insensitive; names come back as
stored. A name is online when a GUN session of that account exists, or a character of that name is
in the world on this server (then its owner's sessions supply away/do-not-disturb). Lists persist in
`gun-users.json` in the data directory.

## Not implemented

Chat rooms (`JoinRoom`, `SendToRoomEx`, ...), accounts, news, personas, support and
matchmaking messages. Turbine room chat does not use GUN.
