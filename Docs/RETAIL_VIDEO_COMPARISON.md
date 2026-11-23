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

## Water, shoreline and submerged-camera review (2026-11-13)

This pass investigates the reported foam seams and underwater view around
47.4N, 12.6E (8FBC), using the local retail/admin decompiles and both installed
DAT profiles. The user's retail reference shows the bank texture continuing
below the surface; that must survive the water compositor. A screenshot alone
cannot establish the reflection/transmission coefficients, so the coefficients
below come from the recovered draw calls and material stages.

Recovered behavior:

- `LScapeTerrainMesh::UpdateNearWater` builds sixteen 40 m quads per block,
  each with 0..1 base UVs. `WaterType::m_nTileWidth` is not a texture divisor.
- `GenerateShore` intersects the terrain with the water plane, joins the
  resulting contour, averages its endpoint normals, and extends a ribbon
  12 m times the vertical normal component. The inner edge retreats 5 cm.
  Along-shore coordinates use accumulated distance; the two foam layers share
  a per-contour phase between 0 and 0.8. Their motion remains independent.
- `DrawShore(false)` writes the transmission texture's alpha times 0.5. Base water and
  the reflection pass use destination alpha / inverse destination alpha.
  Consequently, riverbed color contributes to the final surface even away
  from the immediate bank.
- Both profiles use shore material `31000362`. Its first two stages sample the
  same wave image, followed by a clamped mask. In Legions the wave, mask and
  transmission texture IDs are `40000791`, `40000792` and `40000793`.
  In Icefire the corresponding IDs are `4000080A`, `4000080B` and `4000080C`.
  The alpha chain is signed-add followed by multiply-by-two. The foam has its own white diffuse lighting.
- Above-water `3100002D` and below-water `3100002E` have opposite culling.
  The below-water instance overrides the template texture with `40000056`,
  a blue ripple image. `40000771` is the template's UV checker, not water.
  The authored underside has diffuse alpha 1 and no animated UV modifier.
- `LScape::CalcWaterLight` interpolates `WaterLighting` keys using camera depth.
  Their combine modes affect ambient/direct colors and brightness and fog;
  moving the camera out of the water restores the ordinary daylight keys.

Changes made:

- Resolve connected shore contours from the DAT beyond the streaming window,
  then cache/bin their segments. Loading a new center block no longer restarts
  the wave phase or changes the normals at that window's edge. Cache identity
  separates DAT paths, header revisions and landscape height tables.
- Clip the joined ribbons to their owning block and remove folded overlap.
  Subtract their footprint from open water so every surface pixel is composed
  once. Previously, overlapping coplanar SceneColor composites could overwrite
  foam and reveal rectangular/triangular seams. Degenerate clipping polygons
  are discarded to prevent unbounded subdivision.
- Preserve phase while removing only whole UV repeats, keeping half-float UVs
  precise even on long coastlines. Foam phase and static shoreline-mask UVs
  are separate channels; the mask clamps across the shore and wraps along it.
- Composite riverbed, water, sky and foam in native display RGB before
  converting to the Unreal linear framebuffer. The GPU oracle checks actual
  transmission of a two-color riverbed rather than judging a reflective-looking
  screenshot. The scene-depth contact term only softens the final 2 cm edge.
- Render a separate underside across both near and distant water. Face gating
  prevents above-water foam/reflection from appearing on the underside.
  Use the instance's native texture and lighting and compute fog per pixel
  rather than interpolating it across 40 m water quads.
- Parse the variable-length water lighting keys, including weighted combine
  operands, and select them from the actual camera location/depth. Reuse
  underside material instances when the distant terrain rebuilds.

Validation: both Development targets build. Seven focused automation tests
pass with each DAT profile: `AC2.Data.NativeShoreMask`,
`AC2.Data.NativeWaterContinuity`, `AC2.Data.RepairedShoreline74A9`,
`AC2.Rendering.WaterBlend`, `AC2.Data.NativeMaterialFinish`,
`AC2.Render.NativeMaterialFinish` and `AC2.Data.AppearanceOpacity`.
The water GPU test covers sixteen composition/culling cases. Geometry checks
verify block area coverage without doubled/missing area, bounded vertex counts,
phase agreement across real shared edges, and equivalent near/far positions.
The offline water-distance smoke captures the reported bank, a streamed block
transition, shallow riverbed and two submerged views while checking animated UVs.
The final run passed with 545 water tiles in two batches after moving from 8FBC
to 8EBC. Captures use the editor play path used by `Launch-AC2.ps1`. A direct
launch of the uncooked standalone executable stopped during plugin loading
(`PlatformCrypto` module missing); standalone runtime/package validation is
therefore not established by the successful game-target build.
Generated material template version: 34.

Limits: this is not a claim of complete retail water parity. The reflection
source is currently a crossfaded sky capture, and its distortion approximates
the native generated bump/reflection pass. The exponential fog limitation
recorded here was superseded by material revision46; see
[Native fog review](NATIVE_FOG_REVIEW.md) for the radial linear implementation,
verification, and the remaining vertex-interpolation/sky-compositing limits.
The geometry partition removes overlap seams, but unusually folded banks and closed
contour phase joins still need broader visual comparison. Validation used local
data and offscreen rendering, not a live Icefire session or a matched retail
camera/time capture.

### Shore motion and surface weight (2026-11-14)

The Legions `GenerateShore` and Admin `RenderDeviceD3D::SetupState` paths agree
with shore material `31000362`. The material translates two wave layers at
+0.15 and +0.10 V per second. V increases from the bank toward open water,
so both crests travel toward the bank. U follows the accumulated bank length
with a ten-metre repeat. Across sharp bends, sampling these coordinates by
distance to the closest point caused visible pinching. Version 38 draws the
original pair of affine triangles per segment as a separate foam pass, clipped
to landblock boundaries. At the reported 8FBB bank, the waterward edges
of two concave segments cross before reaching their 12 m slope-scaled width.
Their outer triangles had the opposite winding and drew back over the wave.
The Unreal mesh stops those edges at their intersection, shares the endpoint
limit with adjacent segments and interpolates the narrowing strip. This is
a geometry correction for folded quads. Straight banks retain the DAT width,
UV scale and scroll rates. The renderer suppresses waves on closed loops under 20 m.
The water transmission mask still covers those loops.

The above-surface base pass now takes its blend weight directly from half of
the shoreline mask alpha. The previous extra diffuse-alpha multiplication
reduced the weight from 0.5 to 0.4 at full mask coverage. The sky reflection
is still an approximation, so overall perceived opacity also depends on view
angle, lighting, and the visible riverbed.

### Surface tint, submerged sky and precipitation (2026-11-14)

Legions `LScapeRender` constructs a bump stage for the reflected scene. The
stage offsets the reflection sample by up to 0.04. Version 39 removes the
extra blue ripple color that Unreal painted over the riverbed for ordinary
lake material `3100002D`. Its ripples still perturb the reflected sky. Other
liquid materials keep their authored color. The sky-only reflection and its
angle-dependent weight are approximations of retail's reflected scene.

The reverse face now uses the `3100002E` texture and its authored alpha.
The previous refraction cone projected a disk of sky into the submerged
view. `LScapeSky::Draw` also checks the sky object's water-only flag against
the viewer's environment. Unreal now applies that test to each sky object.
`WorldRender::Draw` requires an outdoor viewer above the water and rain
volume above 0.4. Precipitation now follows the active camera, clears its
airborne drops and splashes upon submersion, and resumes after resurfacing.
Falling drops over water stop at the water surface.

Five automated checks passed on each local DAT profile (Legions and Icefire),
including the GPU transmission/underside test and the material selection for
the real river. An offscreen game run at 8FBB crossed into 8EBB with 540
water tiles. With rain forced on, the active camera recorded precipitation
above the surface, zero airborne drops underwater, and rain again after
resurfacing. Upward and horizontal underwater captures showed the authored
ripple underside throughout the view.

### Reflected scenery, ripple coordinates and shoreline timing (2026-11-16)

In the [retail footage at 38:43](https://www.youtube.com/watch?v=_emUJwVgGk4&t=2323s),
the far bank and trees appear upside down across the stream and the bed is still visible.
The Legions `WorldRender::RenderReflectionMap` mirrors the camera about the water
plane, clips the scene and renders sky, landscape and outdoor objects.
`LScapeTerrainMesh::DrawWater` samples this image in projected screen coordinates
and composites it with destination alpha. Version 41 captures the visible lake
plane at reduced resolution and blends it with the scene behind the surface.
When separate lake elevations are visible, the closest visible elevation gets
the reflected view. Water at the other elevations continues to transmit the bed.

`CreateLocalMemBumpMap` constructs a 256 by 256 signed two-channel field from
a damped radial wave and two diagonal wave trains. Its signed RG displacement
is applied at 0.04 screen scale using the native scrolling UV transform.
The shoreline draw retains its two overlapping mask scrolls. Retail assigns a
random phase between 0 and 0.8 to each locally assembled strip. The streamed
mesh uses stable local phases with continuous interpolation across tile joins.

The Unreal capture reserves a margin outside the viewport for the signed ripple
offset. The projected lookup uses the same margin so near-edge samples still
see the reflected scene. Partial shoreline crests use the retail display-RGB
blend before conversion to the linear framebuffer.

### Ordinary lakes and Icefire shoreline assets

`DrawWaterMesh` draws the two animated, lit base stages of ordinary lake material
`3100002D` over the riverbed. `DrawWater` then blends the projected reflection
over that result. Version 43 restores the base pass even when a reflection is
unavailable for the current water plane. The underside uses its own material.

The shoreline wave and transmission materials come from entries `40000017` and
`40000024` in the active archive's `40000002` data-ID table. Their texture stages
resolve to identical image data across the two profiles. Hard-coding Legions'
wave texture ID selected opaque brown artwork on Icefire and exposed the shape
of the entire shore ribbon at 44.5 N, 12.1 E.
