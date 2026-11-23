# Native DAT audio

`Bridge` exposes the pinned `References/AC2_Rusted/crates/ac2-audio` engine through
a standalone renderer. Unreal starts one audio process per game world. It sends playback commands and receives stereo PCM over private pipes. The source is AGPL-3.0-only;
the bridge reads local game archives and does not bundle retail recordings.

Run `Build-AC2Audio.ps1`, or launch normally to build it with the client. Initialize
the pinned submodule first with `git submodule update --init --recursive`.
Audio bridge ABI 2 exposes native mixed stereo PCM through an Unreal procedural
sound. Unreal owns the output device, so Windows default-speaker changes now
apply to DAT effects and music as well. The previous independent CPAL/WASAPI
stream remained on the old device after Unreal switched. Rebuild the audio executable
and client together. `-nosound` disables initialization; a missing or incompatible
renderer is logged and does not block login.

Offline coverage includes native music/effect PCM rendering and the DLL-to-Unreal
procedural audio queue. `AC2.Data.InteractionAudio` verifies audible pickup samples,
bounded buffering, acknowledgement-gated cues and the Tumerok reach animation
without playing through speakers or connecting to a server.

The native music synthesizer loads DLS instruments from the DATs. Where a segment
uses General MIDI instruments, it also reads Windows' installed
`%SystemRoot%/System32/drivers/gm.dls`. Set `AC2_GM_DLS` to a different local bank if
needed. The Windows bank is never copied into the repository or distributed.

Implemented routes:

- DAT UI state sounds and screen music, with a two-second music transition fade.
  Press sounds are dispatched with input, including clicks completed between
  frames. Native loot and conversion buttons keep their own authored cues.
- Incremental quest events dispatch CQuestSystem's bestow/advance/complete/fail
  FX, respecting the server's silent-update flag and suppressing snapshots and
  duplicates. Single-item loot uses the native loot-button cue on successful
  acknowledgement, without sounds for rejections or repeated replies.
- Animation/FX sounds, their native wave formats, loops, pitch variation and spatial
  attenuation; one-shot sounds continue across a stop-FX event.
- Scenery and model startup sounds at the authored object origin and scalar,
  respecting day/night, player-only, channel preferences and visible interior
  cells. Repeated streaming updates do not replay finished one-shots. Inventory
  and character previews remain silent.
- Looping FX scalar changes use the recovered constant-rate ramp and update the
  existing sound context. Startup sounds begin at their authored scalar without
  a fade-in. MusicInfo flags select Music or Player Music independently of Main;
  ordinary SoundInfo and native UI cues retain their Main route.
- Terrain-dependent footstep variants and day/night/player-only FX gates.
- Region sound descriptions weighted by nearby authored land points and visible
  room origins; native continuous music and intermittent environmental sounds.
- Native music intensity from nearby objects' networked GrooveLevel quality,
  with the original radar range and signed-byte accumulation.
- Persistent volume controls in the existing client options page.

The installed Legions sound descriptions contain no intermittent entries; the
loader supports them but does not invent birds or other environmental recordings.
Some native music profiles deliberately contain rests or silent cues at low
intensity. Missing server GrooveLevel values currently stay at zero.
Region re-entry retains valid dominant music ownership after removing faded
emitters; the build applies `Bridge/patches/ambient-reentry.patch` to the pinned
audio dependency. An empty environment initiates the normal fade-out.

The native completion FX `410004e1` references SoundInfo `2a0008be` / DBWave
`0c00088f`, absent from both installed Legions and Icefire portal archives. The
completion event is wired, but that sound cannot play until those assets are
available; it is not replaced with an unrelated recording.

Remaining work includes exact UI pause/media timelines, indoor floor-specific
footsteps, environmental reverb, voice/chat audio, and a listening audit
of the complete retail combat/tutorial sequence. This is not an assertion that
every sound in the supplied gameplay video is reproduced.

To verify real assets without playing through the speakers:

```powershell
$env:AC2_DAT_DIR = "C:/dev/ac2e/Asheron's Call 2 - Legions"
$env:AC2_AUDIO_TEST_IDS = '2a0000ec,2c00039a,2c00044a'
$env:AC2_GM_DLS = 'C:/Windows/System32/drivers/gm.dls'
./Build-AC2Audio.ps1 -Test
```

The six bridge tests decode UI effects, selection/Empyrean music and a Human region music
profile at its authored danger intensity, render them through the native software
mixer and reject silent PCM. They also check environment cleanup, independent
Music/Player Music volume, volume changes on an existing loop, and more than
4,096 emitter positions. `AC2.Data.SceneryStartupAudio` additionally verifies
installed outdoor/interior sources, lifecycle, preview suppression and scalar
updates through the real bridge in both DAT profiles. These checks
verify decoding and mixing; they do not measure loudspeaker output or retail parity.
