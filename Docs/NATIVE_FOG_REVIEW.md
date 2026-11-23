# Native fog review

This review compares the Admin 2.0.20.7148, Legions 2.0.11.7160, and client0930 decompiles against installed Legions and Icefire DATs. Decompiled C is not original source. Where the decompiler drops constant-buffer writes, client0930 executable instructions and its shipped `fragment.dll` supply the evidence.

## Distance, blending, and units

`WorldRender::SetupEnvironment` (Admin `0x0057CB50`) selects only NONE or LINEAR. `RenderDeviceD3D::SetFogProperties` (Admin `0x005C68E0`) writes D3D fog color, near, and far. Distances are metres. `RenderDeviceD3D::SetDefaultD3DStates` (client0930 `0x00563C10`) selects linear vertex fog and enables range fog on the fixed-function path. It disables both when the programmable vertex path supplies fog itself.

The programmable path is independently confirmed by `SetupStaticVertexShaderConstants` (client0930 `0x005646B0`): c1 contains `(near, far, 1/(far-near), 255)`. The shipped `fragment.dll`'s `f_CalculateLinearFog` fragment at file offset `0x596c` computes the reciprocal of reciprocal view-distance, subtracts that distance from far, and multiplies by inverse range. `f_OutputFog_Fog` (`0x5c18`) sends it to oFog. Thus visibility is `saturate((far - radialDistance) / (far - near))`; final display RGB is `source * visibility + fogColor * (1 - visibility)`.

Native evaluates this in vertices and interpolates the result across triangles. The replacement UE pass evaluates radial distance per pixel; this is a remaining fidelity difference on coarse triangles. It must not be described as pixel-identical native rendering. DAT negative near distances are intentional and must survive parsing. Fog density is serialized but is not consumed by this linear world-render path.

## Regional and underwater data

Region record `0x0f000000`, entry eight, references `CFogDesc` `0x17000000`. Both installed profiles have the same 752-byte description with 14 fog types and distance map `0x19000002`: 255 by 255 bytes, column-major `x * height + y`, with 3,558 nonzero entries in Legions and 3,599 in Icefire. The descriptor is identical, but the distance map is not: Icefire adds 41 type-12 cells (724 versus 683), with every other type count unchanged. Both DAT headers report engine104/game3. Types include negative near values down to -450m. Volume/top/bottom fog map IDs are zero in these data; the audited `GetFogCorners` route uses the distance map.

`LScapeLighting::GetFogCorners` (Admin `0x00590670`), `CalcCombineFog` (`0x0058FAC0`), and `CornerInterp` (`0x0058F230`) combine each southwest/northwest/southeast/northeast LightFogInfo with the current daylight before bilinear interpolation over the 160m block. All four NONE corners preserve daylight unchanged. Any active corner yields linear fog and interpolated ambient/directional lighting, near/far, sky amount, and RGB. `BlockMap::GetValue` (`0x006CF2C0`) returns the first map byte for out-of-bounds X/Y, and zero only when its map pointer is null.

Combination selectors are multiply, weighted addition, first, second, maximum, and minimum. All color combinations use authored display RGB. `CalcDayLight` (`0x0058F9A0`) uses directional color times direction-vector length plus ambient color times brightness, normalizing invalid RGB by its largest component. Its combineDayLight argument is not consulted. Directional color alpha is retained.

`CalcWaterLight` (`0x0058FC10`) combines each depth key with the already region-adjusted environment and then interpolates depth keys. Interpolation forces LINEAR; an endpoint preserves its authored type. `UpdateLighting` (`0x005901F0`, final underwater branch) copies only fog min/max/type/sky/color into the final environment. It does not overwrite daylight or sunlight with water-key lighting.

## Interiors, sky, and reflection

Interior `CLandBlockInfo` properties 0x40000056/57/58/59 supply fog type/color/near/far; 0x4000005a supplies ambient light. Type 0x40000001 means NONE and 0x40000002 LINEAR. `WorldRender::SetupEnvironment` applies these indoors instead of outdoor fog. The unrelated fog fields in UI scene descriptions are not a source of dungeon fog.

`LScapeSky::SetSkyFog` disables depth fog. `LScapeSky::Draw` (Admin `0x00585980`, Legions `0x00555070`) renders before-sky objects, overlays fog RGB using the separate fog_sky alpha, then draws after-sky objects. Before-sky objects are omitted when fog_sky is approximately one. Sky is not a distant opaque world surface for range fog.

`WorldRender::RenderReflectionMap` (Admin `0x0057E270`) calls SetupEnvironment for sky, then for outside terrain/dressing. Reflected world geometry therefore needs world fog evaluated from the reflected camera; reflected sky uses the separate sky overlay.

## Material flags and alpha fade

`RenderDeviceD3D::SetMaterial` (`Saved/admin_renderdeviced3d.c`, around line3096) enables RGB depth fog only when UseDepthFog is true and UseDiffuseFogAmount is false. Authored additive materials may opt out; they must not automatically be fogged to black. In installed high-detail class-two selections, 55 Legions and 51 Icefire ONE/ZERO templates opt out, but their world-versus-UI usage is not fully classified. There are no selected UseDiffuseFogAmount templates in either installed profile.

`fragment.dll` fragment `f_ModulateColor0WithFog` (`0x4524`) multiplies diffuse **alpha**, not RGB, by world fog visibility. `f_ModulateColor0AlphaWithAlphaFog` (`0x4560`) similarly multiplies diffuse alpha by a separate alpha-fog visibility. Its range comes from c2, not c1. `FrillRender::Render` sets this independent range through RenderDevice virtual offset0x48: `(farFade - 8m, farFade)`, where farFade is current landscape radius times square_length and is multiplied by .65 for LandscapeFrillDistance preference1. Reusing world fog distances for this flag would be incorrect.

The selected DepthAlphaFog template is Legions `0x2b0000d5` / Icefire `0x2b0000cf`. Foam material `0x31000362` -> `0x2b00006c` has DepthFog/DepthAlphaFog/DiffuseFog = 1/0/0 and SRCALPHA/INVSRCALPHA. Water `0x3100002d` -> `0x2b000054` has the same fog flags and DESTALPHA/INVDESTALPHA. Both profiles agree, so both water and foam require world RGB fog.

## Verification and remaining limits

`AC2.Render.RegionFogInterpolation` tests column order, negative near values, native corner-combination order, inactive corners, native map boundaries, and malformed-data reset. `AC2.Data.NativeRegionFog` validates the installed map census and authored type-four distance/sky settings. Render tests and final build validation are tracked by the main implementation pass.

The review establishes the native mechanisms above; it does not establish complete pixel parity for every material, room portal, or weather transition. Remaining specific differences to assess include per-vertex versus per-pixel fog interpolation, mixed indoor/outdoor portal views, and source-specific fade ranges for any future DepthAlphaFog material beyond frills.


## Unreal implementation and validation, 2026-11-18

Material revision 46 replaces the exponential-height-fog actor and outdoor 70%
opacity cap with the authored radial linear ramp. Negative near distances are
preserved. A post-opaque pass blends the lit world in display RGB; transparent
surfaces apply fog at their own depth. Water fogs the authored surface and foam
before composing the already-fogged bed/reflection, avoiding duplicate fog.

The region descriptor is resolved through the active DAT region, including
Icefire's additional fog-map cells. Dungeon LBI properties override outdoor fog
when the camera is inside a room. Underwater key boundaries preserve the native
mode selection; water changes fog while retaining the region's world lighting.

Material fog switches are decoded instead of skipped. Exempt opaque sections
use depth-matched stencil masking; mixed procedural meshes use a depth-only
proxy for exempt sections. Moving geometry and hidden/revealed sections update
that mask. Diffuse-fog and depth-alpha-fog affect opacity using their respective
ranges. Frill alpha fading occurs before coverage dithering, with an eight-metre
fade band at this client's 160-metre frill draw distance.

Fog state is isolated by world and captured per view family; UI captures can
disable it. World cleanup and sky teardown remove that state. Neither this
change nor its tests require a server or a persistent character login.
Module reload also restores original mesh stencil settings and destroys
mixed-section exemption proxies before discarding their tracked state.

Validation:

- Editor and game Development targets compile successfully.
- `AC2.Rendering.NativeFog` passes 47 GPU cases: range endpoints, display-RGB
  midpoint, radial distance, world altitude/large coordinates, alpha/additive
  blending, sky/UI exclusions, disabled captures, lifecycle, independent worlds,
  water composition, material exemptions, hidden/mixed/animated sections, and
  independent alpha-fog mode, and grass shader/color/coverage at its fade band.
- Region interpolation, real DAT map/definition checks for both profiles,
  underwater lighting retention, water-key boundaries, and existing water
  compositing tests pass.
- Sky-lighting/lifetime checks pass separately with both explicit DAT profiles.
  The first combined run omitted that test's required DAT argument; the two
  profile-specific reruns resolved that setup failure.
- `AC2.Rendering.NativeFogModuleLifecycle` passes two shutdown/restart cycles
  with surviving world meshes: both original custom-depth enable states,
  stencil values/write masks, proxy destruction, and clean re-registration.
- Actual daytime game views pass shoreline streaming, ripple advancement,
  underwater weather suppression, and resurface checks. Nighttime views pass
  at the reported Icefire riverbank with cached patched DATs. Captures were
  visually checked; no material compilation failures occur in the final run.
  The earlier v45 smoke exposed discarded grass shader inputs; revision46
  fixes their binding order, with four grass coverage regression cases added.

Logs/reports: `Saved/NativeFogReview/`. The earlier v44/v45 combined reports are
retained, including initial failed probes and their subsequent corrections.

Remaining fidelity limits: the radial ramp is evaluated per pixel rather than
interpolating a native vertex-fog scalar. Fractional sky-fog overlays still use
Unreal's linear-color alpha composition; their authored fields, exclusion from
distance fog, and before/after sky ordering are preserved. Mixed-room portal
views and source-specific alpha-fade ranges beyond landscape frills need further
matched native-client comparisons. These tests do not establish pixel-identical
retail rendering or live-server validation.

Exact sky composition would require a sky-only display-RGB intermediate target
containing the before group, fog overlay, and after group, followed by one
display-to-linear conversion. Exact vertex fog requires a geometry fog-factor
pass or buffer channel; scene depth alone does not retain native vertex fog
interpolants. Neither difference can be corrected by changing fog density.
