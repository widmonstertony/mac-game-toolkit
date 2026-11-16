# FH6 XeSS SR / XeSS-FG candidate, 2026-11-15

The live FH6 Store configuration now selects XeSS 2.0.2 Quality upscaling and
XeSS-FG 1.3.1 with one interpolated frame (2×). It retains the game's DLSSG input,
3472×2266 output, the existing Steam shortcut, audio/network/cloud repairs and
caches. No shared renderer or other game was changed. This is a candidate awaiting
the user's game validation, not a claim that all ghosting or stutters are fixed.

## Evidence

- `../xess-gpu-verification.json`: the installed SR DLL successfully initialized
  and executed three GPU dispatches, completing the fence with nonzero output.
- `../xefg-gpu-verification.json`: official Intel FG 1.3.1 and XeLL 1.3.2 initialized
  on the local M5 Max through the existing D3DMetal route. After the first warmup
  present, 15 synthetic presents each reported two frames and success.
- A second test replayed captured frames 58244→58245 at full output resolution.
  The actual generated present was copied back before presentation; the following
  original present matched the captured current image byte-for-byte. The generated
  image had a different SHA-256, excluding merely presenting the input twice.
- Visual comparison of that pair did not reproduce the large duplicated/stretched
  vehicle features seen in the old FSR generated frame. This is one captured pair
  after frozen-history warmup. It does not establish high-speed/turning quality,
  live pacing, long-term stability, or zero artifacts.

The XeLL low-latency mode must be enabled before enabling XeFG. The first fixture
omitted that call and was rejected by the SDK; the corrected fixture follows
[Intel's sample](https://github.com/intel/xess/blob/main/samples/basic_sample_frame_generation/basic_sample.cpp).
It imposes no FPS cap (`minimumIntervalUs=0`).

## Reproduction sources

`xefg-present-probe.cpp` tests actual 2× presentation on a small synthetic scene.
`xefg-road-replay.cpp` reads two real captured scenes, warms history with the older
scene, then presents their chronological interpolation. Its readback hooks are
**isolated test-program instrumentation only**; they are never installed in the
game. `prepare-replay.py OLDER CURRENT OUTPUT` prepares the two inputs. Captures
must use the recorded 3472×2266 / 2314×1510 layout and include same-frame
`prepare-current-depth.bin` and `prepare-current-motion.bin`.

Obtain the DLLs and headers from the official [Intel SDK](https://github.com/intel/xess)
under its license. Headers and proprietary libraries are not redistributed here.
Place `xefg_swapchain*.h`, `xell*.h` and the two DLLs in a staging directory named
`xess-fg-sdk` beside the probe executable; add its parent to the compiler include
path. Build with an x86-64 MinGW C++ compiler, C++17, linking `kernel32`, `d3d12`
and `user32`. These freestanding probes use `-nostdlib -Wl,-e,entry`,
`-fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-stack-protector`.
Run them through the same D3DMetal/Wine engine in an isolated prefix, with the
working directory containing `xefg-replay/` for the captured-frame test.

## Install and rollback

First run `install_xess_quality.py --apply` for the SR selection, then
`install_xefg_quality.py --sdk SDK_DIRECTORY --apply`. Both require FH6 stopped;
the latter verifies exact SDK hashes, the actual OptiScaler's XeFG/XeLL support,
the GPU receipts, output size and Quality ratio. It changes only `FGOutput`,
`InterpolationCount` and `IgnoreInitChecks`, and adds the two local SDK libraries.

Restore `OptiScaler.ini.before-xefg-20261115` and
`fh6-profile.json.before-xefg-20261115` with the game stopped to return to the
preceding FSR FG configuration. Restore the earlier `before-xess-quality` backups
as well only if also reverting SR. Do not delete saves, caches, or game files.
