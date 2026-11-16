# Isolated MetalFX frame interpolation bridge

Experimental, **not installed in FH6**. The game still uses its existing FSR
quality upscaling and 2x generation. This prototype does not yet implement the
game's D3D12 resource staging, UI composition or ordered swapchain presentation.
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
