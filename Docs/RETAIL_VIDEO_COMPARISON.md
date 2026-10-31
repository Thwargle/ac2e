# Empyrean starter reference pass

Reference: the user's local `AC2StarterGameplay.mp4`, corresponding to
https://www.youtube.com/watch?v=ZRFnhp9UNAw. Sampled frames from the local recording
were compared with rendered client screenshots. The recording and extracted
retail images stay in ignored local directories.

## Visible differences addressed

- At 00:00, selection has a star/nebula background. Our opaque capture hid the
  DAT page art behind a black rectangle. SceneColor HDR plus a premultiplied UI
  compositor now preserves the background and additive particle colors, without
  applying display gamma twice to the character.
- At 00:05, the lineup uses warm room lighting and animated scenery. At 00:15,
  the creator includes lit wall torches and an animated purple platform. The
  portal/torch viewports were omitted and preview materials were not ticking.
  These now use their DAT scenes, camera projection and particle sources. The
  overlay camera conversion preserves native screen handedness.
- The video moves through blue, yellow, orange and dark red tutorial rooms; the
  outdoor frame near 10:00 is nighttime. These are different environments, not
  evidence for applying a single darkening preset. Sky HSB interpolation now
  operates on native packed RGB before converting colors for Unreal output.
- The lineup used appearance index zero and unit body scale for every avatar.
  The native script instead randomizes supported physique choices in bit order,
  then selects `ceil(choice count / 2) - 1` for height and build. This now drives
  both the lineup and creator. Each race/sex retains its edited draft while
  switching avatars within a creation session; a new session gets fresh drafts.
- The 00:05 lineup comparison exposed reversed horizontal projection: the Human
  male appeared on the left rather than the right. Preview compositing now
  restores the native screen direction, with matching selection hit coordinates.
  This replaces the earlier overlay-only correction; it does not flip DAT mesh
  geometry, texture UVs, or world placement.
- Avatar hover now changes the native animation mode (`41000006` inactive,
  `41000007` active). Clicking plays behavior `41000005` in the active mode and
  keeps the lineup visible until that track ends. All ten authored sequences
  resolve; durations range from 1.25 to 3.633 seconds. Repeated clicks do not
  restart the track, and customization opens only after completion.
- Static scenery now applies appearance-table FX overrides after explicit FX
  overrides, matching `PartGroupDataDesc::FillFXTable`. The lineup braziers select
  the four blue particle layers from appearance table `20000054`, rather than
  default fire from their static FX table. Part-level startup effects also inherit
  global appearance selectors. No scene-specific tint is added.
- Idle and movement tracks now dispatch their DAT animation effects, including
  cycle-start cues and events crossed while a cycle wraps. Attached heads and
  weapons share their parent's clock without dispatching duplicate cues. Gait
  blends sum native effect timing, probability and intensity by effect name;
  changing walk/run weights cannot replay the same cue within one stride.
- Particle scale now follows `CPartArray::QueueParticleSystems` and
  `ParticleSystem::Render`: all types inherit object scale, and type 2 also
  applies the visual's particle scale. The previous rule dropped object scale
  for type 2 and applied visual scale to the other types. FX strength no longer
  resizes sprites; the native `CreateParticleSystem` ignores that scalar for
  geometry. Animated startup emitters now retain the visual's particle scale.

The rendered smoke character uses different appearance choices from the video's
female Empyrean. Its colors are not a pixel-for-pixel reference match. The checks
verify that the same DAT appearance stays consistent across creator and selection.

## Audio implementation and checks

The client previously discarded UI sound media and animation sound IDs. It now
uses the pinned Rust native sound engine for local DAT effects, DirectMusic/DLS
music, animation/FX sound events, and region music from authored scenery/room
sound points. Networked GrooveLevel values drive the native music intensity.
Saved volumes apply before front-end music starts.

Software-mixer tests produce nonzero PCM for UI, selection and Empyrean music,
and a Human environment profile at an audible authored groove. A rendered UI
smoke opens the default output device and verifies native playback requests.
These are decoding/routing checks; no claim of a subjective listening comparison
or measured loudspeaker output is made. See [audio scope](../Audio/README.md).

## Remaining gaps

- Additional native sky material layers, exact fog/shadow behavior and matching
  server time/weather are required for an exact sky comparison.
- Creator camera framing, effect placement and UI media pause/timeline behavior
  need further comparison across all races and both sexes.
- The lineup's blue flame family, selection transition and particle scale rule
  are verified. Particle prewarming, emitter-wide fades and startup sound
  strengths still need alignment. Movement triggers now reach the existing
  terrain-aware sound resolver, but a listening comparison and complete native
  effect-track lifetime handling remain outstanding. Preview animation audio
  remains suppressed. The installed DAT script reports empty held-object
  lists for all ten avatars; no weapons or other props are invented for them.
- Static world sound emitters, environmental reverb and the full tutorial/combat
  sound sequence remain incomplete. The current DAT region descriptions contain
  no intermittent sound entries; none are invented.
- Server-authored quests, vendors and capture placements remain separate content
  reconstruction work. This pass does not fill missing server records.

Exact DAT foliage placement is preserved, including authored empty areas. No
supplemental grass, replacement textures or audio extracted from the video is used.

## Character-generation reference method

[Tools/RetailProbe](../Tools/RetailProbe/README.md) runs the installed script
library in the pinned Rust VM independently of the Unreal port. The comparison
covers every valid physique value and initial selection for all ten avatars.
The native call order was checked in both the Legions and admin decompiles.
Generated data stays under ignored `Saved/`; no DAT-derived fixture is bundled.

Creation uses a private instance of the native random algorithm. This verifies
the defaults and palette selection rules, not global retail random-call timing
or the exact randomized clothes in the recording. Existing saved appearances
are not re-randomized.

## Validation

- UE 5.8 Editor and game Development targets built successfully.
- The alignment pass ran 47 `AC2.Data` tests plus
  `AC2.Retail.CharacterDefaults`: all 48 passed (43 clean, five with known missing
  SoundInfo `2A0008BE` warnings). Coverage includes each avatar's hover pose at
  the same animation clock as an inactive control, actual selection deformation,
  native duration/completion, and the lineup's appearance-selected blue FX.
- Loop tests cover phase-zero, end-of-cycle and wrapped events, weighted gait
  cues, no replay during speed changes, and a stationary animation clock. A
  rendered multipart Human model dispatches native running cues. Particle tests
  distinguish type 1/object-only and type 2/object-times-visual scale.
- The independent DAT-script probe verifies physics objects and held-object
  lists as well as every physique choice/default for all ten avatars. Empty
  native held-object lists are preserved and checked explicitly.
- The rendered UI smoke passed with retained draft/lineup appearance and native
  screen-direction picking checks. Lineup, head preview and character selection
  screenshots were inspected; the lineup order now agrees with the video.
- `Network/test_login_ui.py` passed against disposable account/world stores,
  including real pointer selection in the carousel and lineup, editing a
  randomized skin tone, creation, and persistence after a fresh login. Recall,
  combat, loot and vitals also passed in that integration run.
- Both DAT software-audio tests passed with nonzero PCM.
- The audio-enabled UI smoke opened the native output device and issued eight
  DAT playback requests. The final UI smoke and environment smoke passed.
- Rendered selection, creator, lineup, Empyrean head, foliage and sign screenshots
  were inspected. Runtime records unavailable in the local DATs remain reported;
  for example, SoundInfo `2A0008BE` is unavailable in the tested dataset.

## Targeting, compass, journal and portal-audio follow-up

The target reticle uses the DAT's northwest-pointing tip. Its four instances now
point inward in screen-corner order. The radar projects contacts onto the same
right/forward axes as the Unreal view, removing the horizontal reflection. Each
cardinal mark is drawn from the native compass atlas in that frame without
mirroring the lettering. Heading still follows the character, independently of
camera orbit. Tests cover all four directions and heading wraparound; rendered
map travel, targeting and heading checks passed.

Quest tabs now use the native `panel_page` open/closed states. Tests inspect the
actual selected/unselected border artwork and route clicks through Slate; the
Underway and Story screenshots show the selected tab joined to the panel.

Portal audio now resolves EnumIDMap `28000003` keys `40000018`, `4000001C`,
`4000001D` and `4000001E`. The native mapping assigns entry `2A000316`, exit
`2A000317`, and tunnel loops `2A000318`/`2A000319`. Previously the exit played
`2A000318` and discarded its handle, leaving a permanent tunnel loop. Portal space
now retains and stops its contexts at exit and teardown. The DAT mixer test runs
three transitions and verifies that no portal voice or PCM remains after exit.
This does not replace the outstanding listening comparison of all world sounds.

Four focused UE data tests and the rendered journal/map checks passed. See
[capture population verification](CAPTURE_REFERENCE_IMPORT.md#outdoor-runtime-verification)
for the outdoor creature check and remaining coverage limits.

## Material finish and color-space follow-up (2026-10-30)

The material decoder previously retained `m_bUseSpecularHighlights` but skipped
`m_cSpecular` and `m_wSpecularPower`. World objects consequently received a generic
Unreal specular value of 0.5, while unlit preview materials received no highlight.
An installed Legions DAT audit found 351 template layers, all with initial power
zero; 146 nevertheless had the highlight flag enabled. In retail,
`RenderDeviceD3D::SetupState` disables the fixed-function highlight when power is
zero. Material modifiers supply the actual colored finishes and power waveforms.

The decoder now reads those fields and applies `MaterialProperty::Apply` fields
2/3 (`RMFIELD_LAYER_SPECULAR` / `RMFIELD_LAYER_SPECULARPOWER`). The fixed-function
fallback renders interpolated specular COLOR1 and adds it after the texture
cascade in native RGB, before conversion to the linear UE framebuffer. This
preserves the authored color, exponent, zero-power gate and animated power. The
extra UE PBR lobe is disabled for object materials. World and preview paths share
the same calculation. Directional half vectors and point-light viewer vectors
follow `RenderDeviceD3D::SetupVertexShaderConstants`; point specular uses white
light and squared attenuation, as recovered from the fragment programs.

The terrain atlas also mixed native RGB tints with sRGB-decoded samples, then
encoded them again. CPU terrain filtering, surface tinting and the distant
palette now operate on the native byte RGB values. DAT textures and placement
records are unchanged. This corrects color-space handling within the current
atlas implementation; it does not replace its terrain blending with retail's
full layer-mask renderer.

Verification is in `AC2.Data.NativeMaterialFinish` and
`AC2.Render.NativeMaterialFinish`: actual DAT defaults/modifiers, a moving power
waveform, tint/filtering samples, and fourteen GPU cases covering matte, colored
highlights, broad/tight exponents, back lighting, point lights and ignored point
lights across world/preview materials. The GPU oracle reads linear HDR before
the display transform, then compares native RGB within four byte values.
Generated shader template version is 18.

Both Development client targets built successfully. The regression run completed
56 tests with zero failures (five tests retain the known missing SoundInfo
`2A0008BE` warnings), including the material GPU oracle and tent interior renders.
The environment smoke also passed without shader errors; sign, foliage, cave
torch and character screenshots were inspected after rebuilding the materials.

Remaining fidelity limits include authored programmable pixel shaders and
generated texture coordinates, full multipass composition, terrain layer masks,
and the separate water/terrain physical-light fallback. Those are not established
as retail-equivalent by the fixed-function checks above.
