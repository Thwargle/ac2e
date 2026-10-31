# Native DAT audio

`Bridge` exposes the pinned `References/AC2_Rusted/crates/ac2-audio` engine through
a small C ABI. Unreal owns one engine per game world. The source is AGPL-3.0-only;
the bridge reads local game archives and does not bundle retail recordings.

Run `Build-AC2Audio.ps1`, or launch normally to build it with the client. Initialize
the pinned submodule first with `git submodule update --init --recursive`.
Audio uses the Windows default output device through CPAL/WASAPI. `-nosound`
disables initialization. A missing device or DLL is logged and does not block login.

The native music synthesizer loads DLS instruments from the DATs. Where a segment
uses General MIDI instruments, it also reads Windows' installed
`%SystemRoot%/System32/drivers/gm.dls`. Set `AC2_GM_DLS` to a different local bank if
needed. The Windows bank is never copied into the repository or distributed.

Implemented routes:

- DAT UI state sounds and screen music, with a two-second music transition fade.
- Animation/FX sounds, their native wave formats, loops, pitch variation and spatial
  attenuation; one-shot sounds continue across a stop-FX event.
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

Remaining work includes static scenery sound emitters (such as fountains), exact
UI pause/media timelines, indoor floor-specific footsteps, all FX scalar ramps and
channel preferences, environmental reverb, voice/chat audio, and a listening audit
of the complete retail combat/tutorial sequence. This is not an assertion that
every sound in the supplied gameplay video is reproduced.

To verify real assets without playing through the speakers:

```powershell
$env:AC2_DAT_DIR = "C:/dev/ac2e/Asheron's Call 2 - Legions"
$env:AC2_AUDIO_TEST_IDS = '2a0000ec,2c00039a,2c00044a'
$env:AC2_GM_DLS = 'C:/Windows/System32/drivers/gm.dls'
./Build-AC2Audio.ps1 -Test
```

The tests decode UI effects, selection/Empyrean music and a Human region music
profile at its authored danger intensity, render them through the native software
mixer and reject silent PCM. They also check environment cleanup. These checks
verify decoding and mixing; they do not measure loudspeaker output or retail parity.
