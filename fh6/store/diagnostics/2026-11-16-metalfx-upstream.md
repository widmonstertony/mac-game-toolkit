# MetalFX upstream integration investigation

Status: candidate located and downloaded; NOT installed or validated in FH6.
The production profile still uses quality FSR SR and FSR 2x FG. Ghosting is not
resolved. Do not interpret API support, an enabled setting, or successful NGX
initialization as evidence of generated/presented intermediate frames.

## Preferred candidate

https://github.com/vm99cs8fnw/OptiScaler-witcher3-skipspoof/releases/tag/v0.9.4-witcher3-metalfx

Pinned source: `308613ede32ef858498d62593c80e884ff895f4d`.
Essentials archive SHA-256, independently checked after download:
`ed775e658101a0501d6c17c92a1bb3268dfda9eba506c71306890f5a577968dd`.
Archive contains OptiScaler.dll, OptiScaler.ini, INSTALL-Witcher3-Mac.txt.
Files are staged outside the game in the local upstream-metalfx test directory.

The author reports Witcher 3 Remastered on CrossOver Preview / D3DMetal.
This is not an FH6 compatibility claim. The release is explicitly experimental.

Source changes retain the game's Streamline DLSS-G swapchain hooks, forward NGX
feature 11 creation/evaluation to the real provider, temporarily alter Streamline
capability checks without globally spoofing DXGI, and repair a missing latency
frame identifier. D3DMetal supplies the MetalFX implementation. This avoids
building another texture-sharing and presentation bridge if compatible with FH6.

Upstream settings are D3DM_ENABLE_METALFX=1, Dx12Upscaler=dlss,
[DLSS] PassThrough=true, FGInput=nofg, FGOutput=nofg. Here nofg disables
OptiScaler's replacement FG; game-native DLSS-G must still be enabled and its
actual generated frames verified. These settings alone do not enable 2x FG.

## FH6-specific checks still required

- The latency hook hardcodes parameter vtable slots 2 and 9 based on Witcher
  disassembly. Its own comment warns that the public header layout differs.
  Validate FH6's actual interposer ABI before using the release DLL in-game.
- FH6 sl.interposer.dll and sl.dlss_g.dll both report 2.11.3.0. Hashes:
  interposer `4599ece37d534e665fc61413349e8ce060cdf9b88fd64f4d1807c8b8a65b6311`;
  dlss_g `efdac80902b06f4c4b0eac05386767c0ce9bd9f6fa1802eb24ec3c2530ba63eb`.
  nvngx_dlssg.dll reports 310.6.0.0, hash
  `17e00fc66d32f348fa16d387d635ac1b994aa7317b3e6d968867d1ee77af3564`.
- Local Xbox bottle NGXCore registry key exists but has no FullPath value;
  the inspected system.reg has no NGXPath entry. MetalSharp's ngx_core.c
  explicitly configures these provider lookup paths. Check the actual loaded
  provider, not only registry presence. No registry changes made in this audit.
- Earlier standalone native NGX SR evaluation returned BAD00000 despite
  successful initialization/creation. The tiny feature-11 probe lacked a full
  DLSS-G resource contract. Neither result proves a complete game path works
  or that MetalFX FG is unsupported.
- Retain FH6-only launcher, ASI fixes, audio/network configuration, caches,
  3472x2266 output and quality settings. Do not install the Witcher ini wholesale.
- Validate actual intermediate pixels, order and 2x presentation, then road
  ghosting and real Metal GPU timings. Successful capability queries are
  insufficient; this fork explicitly overrides some unsupported results.

## Other candidates

- https://github.com/metalsharp/MetalSharp : useful native NGX provider routing
  and D3DMetal configuration; not a separately verified FH6 FG fix.
- https://github.com/UzenUPoZiTiV4ik/fsr4-native-macos : documents use of
  D3DMetal's native DLSS-G/MetalFX path; its MetalFX interception is not a
  replacement for establishing that game path.
- https://github.com/MaximKotliar/lightscale : useful Wine/Metal presentation
  reference, but its FG is LSFG and requires the user's Lossless Scaling files;
  it is not the requested depth/motion-aware MetalFX FG bridge.
- https://github.com/nimbold/FG-Metal : experimental game-specific integration;
  not a verified drop-in FH6 solution.

Decision: prioritize adapting the existing NGX passthrough route. Keep the
custom Wine/MetalFX bridge isolated as fallback; its successful ping is not
resource-sharing or in-game correctness validation.

## FH6 priority and corrected Apple NGX probe (2026-11-16)

User explicitly reprioritized FH6 over Heat/Unbound/The Crew. Work on those
integrations is paused. This investigation did not change any production game
file, launcher, resolution, audio, network or cache setting.

A concrete defect was found in the earlier standalone native SR probe: MinGW
compiled the overloaded NVSDK_NGX_Parameter methods in header order, whereas
Apple's MS ABI dispatch uses reversed overload order. This explains the earlier
SR evaluation failure; it does **not** establish the in-game ghosting cause.
The corrected isolated probe calls explicit typed slots. Both PE wrapper table
and all 16 native dispatch symbol offsets were checked against the installed
D3DMetal binary before calling them. Native parameter round trips passed for
unsigned (192), signed (-3), and float (0.375).

Real GPU SR verification with D3DM_MTL4=0:

- Input 2314x1510, output 3472x2266, HDR constant-color input, inverted depth,
  low-resolution zero motion vectors, reset on first frame.
- Init, creation and all 12 evaluations returned success; 14 fences completed.
- Readback checked 23,760 interior RGB channel samples against expected half
  values, with tolerance of 8 half-value bit steps: 0 errors.
- Center RGBA: 3801/3401/3001/3c00, expected 3800/3400/3000/3c00.
- Warm submission + fence + readback measurements were approximately 7–9 ms.
  These are CPU elapsed intervals including copies/waits, **not pure GPU time**.
- First evaluation in the initial run took about 862 ms CPU; a subsequent run
  took about 30 ms. This is an isolated-probe observation, not an explanation
  of FH6's previously observed 1.11-second hitch.

The corrected FG capability/creation probe was then run with D3DM_MTL4=0 and
1. Both returned BAD00000 for feature 11 and the provider diagnostic:
`Unsupported: Frame interpolation not present on this OS: 11`.
No FG handle was created. The tiny probe does not supply the complete game's
DLSS-G contract, but this explicit runtime gate must be resolved before the
upstream passthrough can be represented as a working replacement. Do not force
success or disable replacement FG in production based on SR success alone.

Local evidence (outside the tracked repo):
`../.fh6-rosetta-build-20261112/postreboot-ghost/ngx-native-sr-abi.cpp`,
`run-ngx-native-sr-abi.py`, `ngx-native-sr-abi.log`,
`ngx-native-caps-abi.cpp`, `ngx-native-caps-abi.log`,
`ngx-native-caps-abi-mtl4.log`.

The runtime-gate finding above is superseded by the following investigation.


## Native FG availability and real intermediate pixels

Read-only inspection of this installed D3DMetal identified a file-marker gate:
the `nvngx_dlssg.dll` marker sets the application capability byte consumed by
`ngx::frameInterpolationRuntimeCheck`. Running the **same isolated probe** with
the existing FH6 game directory as its working directory changed capability to
available and feature-11 creation to success. No Apple binary was patched, no
fake capability result was injected, and no game file was changed. Therefore
the earlier message was not sufficient evidence of an OS/bridge limitation.

The corrected probe subsequently evaluated a moving-square sequence on the
real GPU. All 12 evaluations completed. After two initial frames, all 10
remaining outputs placed the moving square exactly between the previous and
current positions (32x32 square, 4-pixel/frame movement). Example: previous
centroid 49.5, current 53.5, output 51.5. The output preserved 1,024 foreground
pixels. This proves generated intermediate pixels, **not live 2x presentation**.

## Saved FH6 road-pair controls: still failing visual validation

The chronological, GPU-fenced pair 58244/58245 was replayed through the Apple
NGX provider at 3472x2266 with 2314x1510 depth and motion. Each replay uses
three frozen-history warmup frames and one actual next frame. These controls
are not equivalent to an uninterrupted live sequence.

- Explicit depth/MV subrect extents remove the earlier unbounded/default-extent
  corruption, but vehicle edge artifacts remain.
- Render-pixel versus display-pixel motion scale changes do not remove them.
- Disabling `run_lowres_mvec_pass` removes its unsupported-feature warning,
  but gives the same erroneous road output. Removing that warning is not a fix.
- Full-resolution nearest-neighbor depth/MV and a closest-depth 3x3 dilation
  control still reproduce artifacts. Neither was deployed.
- Converting reversed depth to forward depth, with the matching flag change,
  does not eliminate the fault.
- Zero-motion **diagnostic control only** greatly reduces vehicle fragments.
  It is not a valid repair: it discards actual motion and can impair other
  moving content. It has not been applied to the game.
- Supplying the recorded projection and all twelve camera-vector components
  changes the artifact pattern but still fails the visual gate.
- Direct visualization of captured color, depth and motion shows broadly
  aligned vehicle contours. This does not establish correctness of every
  history/occlusion sample or resource lifetime in the live game.

The evidence narrows the replay issue to motion-guided reconstruction and its
visibility/history contract; it does not yet establish the production root
cause. Do not describe MetalFX as ghost-free or install it as a verified fix.
The production game's SR/FG, native output, audio, networking and caches have
not been modified by these controls. Native Steam's UI is accessible again via
the actual Steam Helper window, without alternative automation mechanisms.

Local evidence: `ngx-native-fg-pixels.*`, `ngx-native-road-{render,nolowres,
fullres,dilated,forwarddepth,zeromotion,camera}.*` and their runner scripts under
the existing postreboot-ghost workspace. Raw captures remain local.
