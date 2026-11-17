# Isolated MetalFX frame interpolation bridge

## Live FH6 FG trial installed, November 17

### Preserve native carrier identity

The next real game run passes Prepare (2,118 recorded snapshots at the sampled
checkpoint), but native FG initialization rejects the parameter ABI and still
has zero midpoint frames. A read-only process map shows both the original
`libd3dshared.dylib` and a second native image at the installed `nvngx.so` path.
The initial installer used `copytree(..., symlinks=False)`, dereferencing the
tested carrier's link into a separate file. Equal bytes do not preserve dlopen
image identity.

An isolated paired control using that actual installed layout fails with the
native assertion `Failed to dlopen D3DMetal`. Restoring the carrier link to the
unchanged original Apple library makes the same installed-path control pass:
32 ordered composition callbacks, 14 actual native midpoint pairs, no UI or
original-pixel errors. This establishes a packaging defect; it does not alone
prove that every live-game ABI rejection or the car ghosting is solved.
The installer now preserves and checks the link, records the native library
hash, and repairs the earlier copied layout with a backup. The repair is
installed; the current game trial was stopped and no agent relaunch follows.
See `carrier-identity-validation-20261117.json`.

### Complete OptiScaler Prepare chain, November 17 14:17 report

The user's next game run and screenshot still show the duplicated car. That
process records `unsupported Prepare extension`, with zero copied snapshots
and zero native midpoint images. Inspection of the local OptiScaler
`FSRFG_Dx12.cpp` identifies the missing case: Prepare V1 links camera-info to a
DX12 backend descriptor, or links the backend alone when all camera data is zero.
The preceding candidate accepted camera-info only when it was the last link.

The new candidate accepts at most two known, non-duplicate links. Camera values
must match the authoritative native frame; the backend device must match the
context. Unknown, duplicate, cyclic/overlong and foreign-device chains remain
rejected. The scheduler GPU fixture now alternates V2, V1 with camera/backend,
and V1 with backend only. All 32 ordered callbacks and 14 native midpoint pairs
pass without UI/original-pixel errors. Installed candidate SHA-256:
`6e869cebf46b56d8efa35e7c5caf7cf7b7b3e05c63cccf681308384cfe00a34c`.
The installation retains FSR quality SR and all prior game settings. No agent
game launch follows installation; live selection and ghosting remain pending
the user's next run. See `backend-chain-validation-20261117.json`.

### Earlier launch regression and first actual-game result

The first trial failed before game startup because the native entry regenerates
`fh6-store-wine` on every launch. Installing only a modified generated script
lost the carrier policy and then failed the profile's integrity check.
The generator and runner now carry the verified FH6-only MetalFX selection;
two consecutive complete `check` runs preserve its exact hash. The game then
started through the same Xbox app used by Steam, and the user confirmed reaching
the menu/scene. Integrity checks remain enabled.

That live run **did not produce any native MetalFX middle frames**: sampled
`apple_fg_dispatch` records report `native:false`, with `middle:0`. The supplied
car screenshot still has a pronounced duplicated rear. The combined decline
message did not distinguish missing snapshots from an unsupported output.
Do not report this run as successful MetalFX gameplay.

The bridge also rejected every Prepare extension, including the official V1
camera-info extension. The next installed candidate
`0b541b68fc7731f35aa0488292b10a24c327920564c139d16a691678b67e37d5`
accepts only that known, singly-linked extension and checks its 12 camera values
against the same native frame. A real GPU scheduler test alternates V1 plus
camera-info with V2: 32 ordered callbacks, 14 native midpoint pairs, no pixel or
UI errors. Actual game selection/quality remain pending. Separate Prepare and
dispatch decline reasons are recorded for the next user run. The agent-started
trial was stopped when the user asked to launch future tests themselves.

This supersedes the installed candidate hash in the preceding checkpoint below.

The FH6-only runtime now selects the MetalFX FG candidate through the existing
native Steam entry. The installed candidate SHA-256 is
`e9d8257e9d068d4b789d13bfcaf14cfe5de3bac017d9a1bd96c38d142d89977c`.
The rebuilt candidate passed the production D3D12 shim scheduler GPU check:
16 base frames, 32 ordered composition callbacks, 14 genuine native midpoint
pairs after two warmup samples, and zero UI/original-pixel errors. The native
NGX carrier is copied into the FH6 private support directory; the FH6 wrapper
selects it with `nvngx=b` and `D3DM_ENABLE_METALFX=1`. The previous provider,
observer, profile, wrapper and settings have a local backup. The heavy sequence
recorder is no longer the active FG provider. Installed file hashes and wrapper
shell syntax were checked after deployment.

**Actual game MetalFX selection, high-speed quality, stutter and crash stability
remain unverified.** Unsupported contracts and native warmup still use stock
FG, and `apple_fg_dispatch` reports whether a sample used native interpolation.
The profile explicitly records this as a trial. Temporal SR is still FSR Quality;
the standalone MetalFX SR operation has no live-game command-list adapter yet.
Resolution, quality ratio, Steam entry, audio and network settings are retained.
`install-live.py` performs this scoped, hash-checked installation.

The earlier experimental checkpoints below describe pre-installation results.
The prototype has temporal SR and FG
operations. The separate FFX candidate connects FG to Prepare/Dispatch,
native UI composition and the existing swapchain scheduler; its isolated
ordering test is described below. Temporal SR is not yet connected to the
game's actual upscaling calls. Passing these tests does not establish live
game 2x presentation or solve car ghosting.

## Full FFX candidate, not installed

`APPLE_FG_CANDIDATE` now connects the command-list backend to the existing
provider's Prepare/Dispatch lifecycle through `apple-ffx-stage.h`. It requires
same-frame native DLSSG scene/UI/depth/motion metadata, matching constants,
RGBA8 SDR output and the known non-async direct queue. A null FFX HUDLessColor
is accepted only when the full native scene and UI tags are present. It copies
depth/motion during Prepare, runs MetalFX into the scheduler's generated
output, and composes the premultiplied native UI afterward. The existing FFX
scheduler retains ownership of submission and presentation. No CPU readback,
queue submission or wait is added to the dispatch stage.

Unsupported frames use the original provider. On returning from native FG,
the copied FSR dispatch descriptor resets stale history without mutating the
caller's descriptor. All Prepare copies, including frames later declined by
the native path, need output retirement or a drained context before release.
The existing D3DMetal latency guard remains compiled into this candidate.

The full-provider test caught two misleading-success cases:

* The same native DLL returned `0xBAD00000` when the test ran outside the real
  game directory. This reproduces the already documented application-file
  capability gate, not a new OS incompatibility. The runner uses the existing
  game's directory and records the genuine `nvngx_dlssg.dll` hash; it creates
  no marker file and modifies no game file.
* A live MetalFX feature reset copies **three** samples, versus **two** on
  initial creation. The old counter falsely labeled the third post-reset
  current image as a middle image. Independent GPU pixels reproduced this:
  observed centroid 77.5 versus expected 75.5. The backend now distinguishes
  creation and live reset; the FFX adapter uses original FG during native
  warmup instead of presenting/counting those copies as native interpolation.

On this Mac, the complete real FFX 3.1.6 provider tests passed:

| Case | Measured result |
| --- | --- |
| 16 continuous frames, alternating explicit/null scene descriptor | 14 native midpoint images; 2 native warmup copies handled by FSR; UI errors 0 |
| One async-route fallback, return to native, then explicit reset | 8 native midpoint images; 7 native warmup samples; UI errors 0 |
| Missing native scene/UI | No native evaluations; final output byte-identical to original after 16 samples |

Every tested native middle image has exactly 1,024 foreground pixels and the
analytic half-step centroid. Context teardown drains and clears the native
counters. These tests use the hash-checked stock renderer shim in an isolated
prefix; they do not establish real
swapchain scheduling, high-speed car quality or game stutter. The game provider
has **not** been replaced. Before deployment, those remaining gates still apply.

A subsequent isolated run uses the actual production patched D3D12 shim
(`82387437…b8dc988`) with `HB_D3D12_TSSHIM=1`. The continuous, gap/reset and
missing-UI cases all pass with the same 14/2, 8/7 and 0/0 native counts.
The probe reports the loaded module path and the runner requires
`C:\windows\system32\d3d12.dll`, in addition to checking that file against
the production reference. This closes the tested shim-combination gap; it
does not exercise the game's additional ASI plugins or actual presentation.
See `apple-ffx-production-results-20261117.json`.

Build with `fg-chain/build.py --apple-only --cc ... --deps ... --out ...` and
run `run-apple-ffx.py --artifacts ... --carrier ... --prefix ... --output ...`.
`--gap-control` and `--missing-ui` select the additional cases. Each run uses
a new result directory, checks artifact/import hashes and writes a result
explicitly marking live presentation and game quality unverified.
Use `--production-shim` only with a separate prefix containing the verified
production shim. The runner does not copy or replace any prefix DLL itself.

### Actual FFX swapchain ordering, isolated fixture

`--present-control` drives the real FFX swapchain rather than dispatching
generation directly into a test texture. Its generation callback calls the
candidate. Its composition callback copies the provided image to the real
swapchain output and a separate GPU readback, preserving the supplied states.
Readbacks are inspected only after FFX `WaitForPresents` returns.

On the production shim, 16 base-frame presents produced 32 composition
callbacks in strict `(generated N, original N)` order. The first two native
samples warmed up through the existing FSR fallback; the following 14 pairs
contained actual MetalFX midpoint pixels. All non-UI rows were searched for
extra object pixels, not just a narrow region near the expected object. The
original images matched all expected RGB bytes and the translucent UI had no
errors. Context teardown completed. See `apple-present-results-20261117.json`.

This establishes ordering and pixels through the real scheduler for a
192×128 synthetic scene. It does **not** establish full-resolution game
quality, actual display scanout, or pacing with multiple game frames in
flight: this fixture intentionally waits after each base frame. No production
provider was installed as a result of this test.

## Command-list resource lifetime verification

`apple-command-backend.h` records through the installed, ABI-checked Apple NGX
provider into the caller's D3D12 command list. It does not submit or close that
list, or read pixels back to the CPU. `apple-frame-inputs.h` adds a bounded ring
of GPU copies of the Prepare depth/motion resources. The original resources and
generated outputs are retained until a completion fence; pointer identity alone
is not used as proof of lifetime. This code is still an isolated candidate,
not the installed game provider.

Run the compiled `test-apple-command.cpp` through `run-apple-command.py`
with `--snapshot-inputs`. In that mode the GPU first copies the real inputs,
then deliberately zeros the original resources before MetalFX evaluation. It
reads back both originals and snapshots to prove the overwrite happened and
that the saved depth/motion stayed byte-exact. Separate upload buffers prevent
the test from accidentally changing its earlier upload. Duplicate Prepare IDs
must be rejected. These test-only readbacks and waits are not the proposed
in-game path.

On this machine, both R32_FLOAT and the game's R32G8X24_TYPELESS depth layout
passed twelve samples: two startup copies followed by ten middle-position
outputs, each with the expected 1,024 foreground pixels. The eight saved
3472×2266 frames 27440–27447, with 2314×1510 depth/motion, also passed exact
snapshot/zeroed-original checks. All eight generated output files were
byte-identical to the earlier no-overwrite command-list replay. This proves
that these input copies preserve that replay, not that its high-speed visual
quality is solved. Output-vs-current comparisons exclude alpha, because alpha
normalization alone is not frame synthesis.

The ring's retirement requires the same direct queue and reuse of an output
whose earlier submission is already queued. The inspected FFX swapchain chooses
the game queue only with `allowAsyncWorkloads=false`. The game adapter must
enforce this and preserve UI composition, frame order, resets, skipped frames
and context destruction before installation. The standalone test serializes
submissions; it does not establish arbitrary asynchronous queue safety.

The PE/Unix ABI uses fixed-width structures. The native side consumes mapped,
page-aligned buffers, copies them into private Metal textures, interpolates,
copies the result back, and retains referenced resources through completion.
The caller must keep buffers mapped and alive until dispatch returns. A timed
out context is poisoned and retains its outstanding resources.

## Actual GPU startup defect fixed, November 16

The initial bridge returned `generated=1` immediately after the reset sample.
On this Mac's MetalFX implementation the following sample still copies the
current image. A 32x32 square moving four pixels per frame proved that the
second output centroid was 49.5 (current), not 47.5 (midpoint). Treating that
output as generated would incorrectly count a duplicated frame as interpolation.

The bridge now suppresses the generated flag for the first two successfully
submitted samples after reset, frame gaps or context creation. From the third
sample the GPU test verifies a midpoint centroid with exactly 1,024 foreground
pixels. Rejected and duplicate frame IDs do not consume history. This startup
behavior is measured on this implementation, not a universal MetalFX guarantee.

`test-native.mm` calls the same native functions on a real GPU and covers mapped
input/output buffers, twelve moving samples, duplicate rejection, frame-gap and
explicit resets, invalid dimensions, and context destruction. Ten successive
outputs passed the midpoint check on Apple M5 Max. No game launch is required.

Build and run from this directory:

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer xcrun clang++ \
  -std=c++17 -fobjc-arc -O2 -framework Foundation -framework Metal \
  -framework MetalFX test-native.mm -o /tmp/fh6-metalfx-bridge-test
/tmp/fh6-metalfx-bridge-test
```

## Real game pixels still fail the visual gate

The saved chronological frames 58244/58245 were tested directly through
MetalFX using 3472x2266 color and 2314x1510 depth/motion. The SDK specifies
motion vectors in previous-color pixel coordinates and vertical FOV in degrees.
The captured projection agrees with the recorded FOV (1.2060298 radians).
Reconstructing the camera transform from the recorded position/basis agrees
with the recorded clip-to-previous transform to about 0.0011 per matrix element;
this is a consistency check, not a pixel accuracy guarantee.

A one-initialization-sample replay was invalid as a test of interpolation:
output RGB matched the current source exactly. Repeating with two initialization
samples produced actual synthesized pixels. Supplying both camera matrices then
changed 10,771,107 RGB channel values, but substantial mirror and rear-body
artifacts remained in both variants. Therefore adding camera matrices alone
does not pass this saved-pair visual gate. Frozen initialization samples are not
a complete continuous driving history; a longer real sequence remains needed.

The saved frames and generated images stay local. No credentials, game files,
vendor binaries or raw game captures are distributed in this directory.

## Low-resolution input-grid correction

On Apple M5 Max / macOS 27.3 (26C5035r), the bridge's original use of
output-color pixel offsets was wrong when depth/motion were smaller than color.
The public bridge ABI continues to accept output-pixel offsets; the native
encoder now multiplies each axis by input-size/output-size. This matches the
render-dimension scaling in Apple's [frame-interpolation integration example](https://github.com/apple/game-porting-toolkit/blob/main/game-porting-skills/skills/using-metalfx-frame-interpolation/SKILL.md).
The installed SDK's previous-color-pixel wording is ambiguous for mixed input
sizes; the actual GPU controls, not a documentation assumption, establish the
behavior on this machine.

`test-occlusion.mm` renders a fixed red foreground over a moving gray background.
Its perspective mode uses finite reversed-Z depth and exact backward motion
for a camera advancing 2.6 units per frame. It counts foreground-colored pixels
outside the foreground, and separately checks synthesized background against
the analytic midpoint and the current original. Two warmup samples are excluded.
This tests pixels, not only successful API returns or `generated` flags.

| Input → output | Corrected: max extra foreground pixels | Uncorrected control |
| --- | ---: | ---: |
| 512×384 → 512×384 | 0 | 0 |
| 256×192 → 512×384 | 0 | 560 |
| 342×256 → 512×384 | 0 | 804 |

All fourteen generated samples passed with normalization. The first low-resolution
negative control also failed the midpoint check on all fourteen samples. The
quality-ratio negative control passed that coarse midpoint check but failed
foreground coverage, illustrating why both checks are required. Both deliberate
negative controls return 6; normal cases and the original contract test return 0.
The perspective checker has sampling/aliasing error; this is not a pixel-perfect
natural-scene oracle or a performance benchmark.

Build `test-occlusion.mm` with the same frameworks as `test-native.mm`, then run:

```sh
/tmp/fh6-metalfx-occlusion-test correct
/tmp/fh6-metalfx-occlusion-test perspective
/tmp/fh6-metalfx-occlusion-test perspective-lowres
/tmp/fh6-metalfx-occlusion-test perspective-lowres-quality
# Expected failing controls (exit 6):
/tmp/fh6-metalfx-occlusion-test perspective-lowres-uncorrected
/tmp/fh6-metalfx-occlusion-test perspective-lowres-quality-uncorrected
```

The saved road pair was repeated with render-pixel motion and two initialization
samples. Combining that scaling with the recorded camera matrices substantially
reduces the repeated rear body, but mirror/vehicle-edge fragments remain.
Therefore this fixes a candidate bridge defect, **not the existing production
FSR route or the complete live-game ghosting problem**. Do not deploy this
prototype as a verified replacement. Continuous game history, the remaining
camera/visibility discrepancies, resource staging and ordered 2× presentation
still require end-to-end validation.

## Camera ABI and actual D3DMetal transfer

Operation 5 adds `FxDispatchCamera` without changing the existing operation 2
layout. Its two matrices use column-major mathematical transforms. The bridge
rejects invalid flags, reserved bits, nonfinite elements and singular matrices
before consuming the frame ID. Adding/removing camera metadata resets history.
The original dispatch remains supported. Building this revision requires a
macOS 27 SDK (`DEVELOPER_DIR` must select the corresponding Xcode).

`replay-bridge.mm` passes the saved road pair through the actual bridge rather
than a separate MetalFX implementation. It verifies frame provenance, capture
completion and adjacent frame IDs. Render-grid scaling plus camera matrices
still leaves mirror/edge fragments. Compared with the independent reference,
the average RGB absolute difference was 0.06995 code values (8-bit scale);
188,855 of 23,602,656 channel values differed by more than one. This is close,
not identical, and does not pass the no-artifact visual requirement. Both
replays use frozen initialization samples, not continuous driving history.

`test-d3d12.cpp` exercises the real x86_64 PE → Wine Unix → MetalFX route:

1. Upload color/depth/motion to D3D12 textures, copy to readback resources, and
   wait for the D3D12 producer fence.
2. Pass those actual mapped resources to native MetalFX, with 96×64 depth/motion
   and 192×128 color/output. There is no CPU repacking between the APIs.
3. After native completion, copy the mapped output upload resource into a
   D3D12 texture, wait for the output fence, then read back its pixels.
4. Require exactly 1,024 foreground pixels at the expected midpoint for all
   ten generated samples; exclude both initialization copies.

The test requires the device vtable to come from Highball's `apd12.dll`, so a
different D3D12 provider cannot silently pass this check. The test prefix also
emits MoltenVK initialization messages; those alone do not identify the device
provider. The actual device method belongs to `apd12.dll`.

All twelve submissions and twenty-four D3D12 fence completions passed on this
Mac. This establishes resource transfer and midpoint pixels for this fixture,
**not live 2× presentation, full-resolution performance, or a ghosting fix**.
See `d3d12-results-20261116.json` for the measured output, without local paths
or game/account data.

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer python3 build.py \
  --cc /path/to/llvm-mingw/bin/x86_64-w64-mingw32-clang++ \
  --out /tmp/fh6-metalfx-camera-build --test-d3d12
python3 run-d3d12.py --build /tmp/fh6-metalfx-camera-build \
  --test-prefix /path/to/existing-isolated-test-prefix
```

The runner refuses MACxus game bottles and does not install into a game. Its
90-second process timeout bounds the standalone probe; the native GPU dispatch
has its own five-second timeout and retains pending resources on timeout.

## Input and occlusion audit (remaining defect is not fixed)

`check-camera-flow.py` checks the saved frame's GPU-completion/provenance fields
and compares raw backward motion with depth reprojection through the recorded
clip-to-previous matrix. At selected road points, both axes usually agree
within 0.2 output pixels (the largest selected residual was 0.43 pixels).
Car-body points have a large camera-only residual, as expected for a car moving
with the camera; an independent color-patch comparison agrees with their actual
roughly two-pixel motion. The newer frame's previous-depth and previous-motion
snapshots also byte-match the older frame's current snapshots. These checks
argue against a global sign inversion or completely unrelated camera data;
they do not establish every pixel's color/resource epoch or visibility.

The saved-pair replay now has explicitly named diagnostic controls:

- `jitter-zero-control`, `jitter-negate-control` and
  `jitter-output-grid-control` all produce byte-identical RGBA to recorded jitter.
- All 7,867,552 input scene alpha values are zero. `opaque-alpha-control`
  changes those to 255, but also produces byte-identical output.
- `fullres-nearest-control` resamples depth and motion at pixel centers into
  the full output grid, with correspondingly adjusted native motion scaling.
  It introduces a large repeated rear body and is **rejected**.

These controls therefore do not justify changing any production settings.
All still use frozen initialization followed by one real adjacent pair.

The occlusion fixture adds moving camera matrices (`-camera`) and textured
foreground geometry with mirrors, a thin spoiler and exhaust-like protrusions
(`-detailed`). Full-input-size detailed fixtures have zero extra foreground
pixels at both 512×384 and 3472×2266. Reduced-input fixtures fail the strict
zero-extra-pixel check: at 512×384, max extras are 2 (half) / 1 (Quality); at
3472×2266 with 2314×1510 inputs, max extras are 684. Every generated sample
still passes the separate background-midpoint check. Thus neither an API
success nor a passing midpoint metric is sufficient for image-quality approval.
The small reduced-resolution counts must not be confused with the large
duplicate body in real-pixel replay.

Native-size build parameters for `test-occlusion.mm`:

```sh
-DFX_TEST_WIDTH=3472 -DFX_TEST_HEIGHT=2266 \
-DFX_QUALITY_WIDTH=2314 -DFX_QUALITY_HEIGHT=1510
```

Run `perspective-camera-detailed` and
`perspective-lowres-quality-camera-detailed`. A return value of 6 is an observed
quality failure, not a broken test to waive. Measured point data, image hashes
and fixture outcomes are in `input-audit-results-20261116.json`; raw captures
remain local. Continuous driving history and precise per-resource GPU epochs
remain unresolved. This revision is not a production ghosting fix.

## Consecutive-history recorder (November 16)

`sequence/` adds an explicitly armed outer FFX provider. It forwards the
installed provider unchanged, retaining its exact bytes as
`framegeneration-sequence-inner.dll`. It records eight consecutive Prepare
depth/motion inputs, scene colors, generated outputs and native camera records.
Copy commands restore resource states; readbacks retain source references until
the verified producer-queue fence completes. Capture is bounded to 1 GiB and
does not run unless `sequence-request` exists beside the provider. A frame gap,
reset, unsupported format, asynchronous route or mismatched constants aborts
the sample instead of silently relabeling it.

The isolated real-D3DMetal tests passed: 491,520 fixture pixels, eight frames
with reused input/output allocations, partial-abort lifetime checks, twelve
byte-identical comparisons against the installed provider, and 393,216 pixels
checked again after serialization. `replay-sequence.mm` consumes the resulting
eight-frame history without repeating old input to initialize temporal state.
The first two outputs are initialization copies; the remaining six are actual
MetalFX-generated outputs. Invalid Streamline `FLT_MAX` camera sentinels are
rejected rather than accepted merely because they are finite.

The recorder was installed into the FH6-only runtime at 2026-11-16 23:53 UTC,
with the old DLL and profile backed up. No synthesis or game-quality settings
were changed. This is diagnostic deployment, **not a ghosting fix or live
MetalFX integration**. Full-resolution moving-game capture and visual approval
remain required before replacing the frame-generation backend.

### DLSSG-route capture correction, November 16

The first live recorder rejected the actual DLSSG input route because FFX
`HUDLessColor` is null even when a same-frame native scene tag is available.
This was a recording eligibility error, not evidence of the ghosting cause.
The outer recorder now accepts that case only with a full-size RGBA8 native
scene, the expected read state/lifetime, and matching native depth/motion
resources. A non-null explicit FFX scene still must match the native tag.

A real D3DMetal GPU probe alternated explicit and null FFX scene descriptors
over twelve frames. Direct original-provider and wrapped-provider outputs
were byte-identical. Eight consecutive fenced readbacks, reuse of three
outputs, gap-abort handling and serialized pixels also passed. The installer
now upgrades a proven existing outer recorder without wrapping it again;
it verifies both active hashes and preserves the exact inner DLL.

The corrected recorder was installed after a normal menu exit at
2026-11-17 00:24 UTC, then launched through the native Steam shortcut.
This changes capture eligibility only. It does not claim to fix ghosting,
stutter, or to install MetalFX in the game. See `sequence-results-20261116.json`.

### Actual continuous game captures

Two eight-frame game sequences now have completed producer fences and complete
pixel files: 17205–17212 and 27440–27447. Both replayed successfully through
native MetalFX at 3472×2266, with 2314×1510 depth/motion. Each yielded two startup
copies followed by six generated outputs. This is offline replay, not live 2×
presentation. The final captured HUD speeds were only 8 and 15 mph; the second
sequence was off-road in woodland. Neither establishes high-speed visual quality.

Fine edge breakup is visible in parts of the captured scene as well as the FSR
generated output. The scene readback is ordered after the inner FG dispatch,
so this observation alone cannot prove whether the original SR output was clean
before that dispatch. Do not classify all artifacts as an FG-only defect on this
evidence. The game was also showing a network-quality warning during capture;
this work does not establish current network stability.

### Temporal SR native operations

Operations 6/7/8 add temporal upscale create/dispatch/destroy while preserving
operations 0–5. SR uses a distinct context and explicit exposure/pre-exposure;
motion scales convert backward vectors into **render pixels**. Color and output
accept RGBA8 or RGBA16F, while the private MetalFX textures use RGBA16F. This
avoids forcing HDR intermediate data through 8-bit buffers. Dynamic resolution,
reactive masks and output-resolution motion inputs are not implemented by this
new interface; a future game hook must not silently discard those requirements.

`test-upscale-chain.mm` feeds twelve moving analytic images through actual
MetalFX SR at 128×96 → 192×144, then supplies those SR outputs directly to FG.
Its pixel oracle checks current-frame SR positions and ten actual midpoint FG
positions, finite RGB, foreground area and pixels outside a bounded foreground
region. The region permits a two-pixel reconstruction fringe; this is not an
assertion of perfect natural-scene edges. Duplicate IDs and invalid exposure
are rejected. A frame gap produces byte-identical output to a fresh reset SR
context. The test passes on this Mac, but does not certify the real game's
high-speed image quality or deploy the feature.

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer xcrun clang++ \
  -std=c++17 -fobjc-arc -O2 -framework Foundation -framework Metal \
  -framework MetalFX test-upscale-chain.mm -o /tmp/fh6-metalfx-upscale-chain
/tmp/fh6-metalfx-upscale-chain
```

`build.py --test-d3d12` also builds `test-upscale-d3d12.exe`. Invoke the isolated
runner with `--upscale-chain` to test PE → Unix SR → D3D12 intermediate texture
→ MetalFX FG → D3D12 readback. Both producer and intermediate queues must finish
before consuming mapped data. This test does not exercise UI composition or
swapchain presentation.
