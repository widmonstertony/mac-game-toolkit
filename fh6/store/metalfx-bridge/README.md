# Isolated MetalFX frame interpolation bridge

Experimental, **not installed in FH6**. The game still uses its existing FSR
quality upscaling and 2x generation. This prototype does not yet implement the
game's staging hook, UI composition or ordered swapchain presentation. A separate
headless D3DMetal test now verifies real D3D12 staging buffers across the bridge.
Passing these tests does not establish live 2x presentation or solve ghosting.

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
