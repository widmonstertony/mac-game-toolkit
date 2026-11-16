# FH6 XeSS SR / XeSS-FG candidate, 2026-11-15

The live FH6 Store configuration now selects XeSS 2.0.2 Quality upscaling and
XeSS-FG 1.3.1 with one interpolated frame (2×). It retains the game's DLSSG input,
3472×2266 output, the existing Steam shortcut, audio/network/cloud repairs and
caches. No shared renderer or other game was changed. Live validation is still
pending; this is not a claim that all ghosting or stutters are fixed.

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

## Integrated NVAPI repair, 2026-11-16

The first live XeFG session loaded an old independent fakenvapi 1.4.1 from the
FH6 prefix's `system32`. OptiScaler logged `Using fakenvapi as nvapi64.dll, remove
it!`. This OptiScaler version already supplies NVAPI and routes latency calls to
its shared XeLL context. The external module is an unsupported combination;
its presence alone does not prove the cause of all loading stalls or ghosting.

`../install_xefg_pacing.py --apply` selects the integrated implementation only for
the FH6 child process, setting `nvapi64=d` in its Wine overrides and
`[fakenvapi] UseFakenvapi=true`. OptiScaler's loader hook supplies its own module
when the external DLL is unavailable. The pinned DLL on disk is preserved, and
no prefix-wide registry override or other game's configuration is changed.
INFO logging is enabled for validation.

`nvapi-route-probe.cpp` is an isolated loader regression. With the old DLL still
present in the fixture, it creates a D3D12 device, checks that the intercepted
NVAPI module is OptiScaler itself, and calls `NvAPI_Initialize`. It exited 0 with
`nvapi-owner=OptiScaler` and `nvapi-initialize=PASS`. This tests routing, not frame
generation or pacing.

The next native-Steam live launch reached the title screen. Its log selected
XeLL, created the XeFG context and swapchain, and reported one supported
interpolation. A process file inspection confirmed the game's XeSS, XeFG and
XeLL libraries and no external `nvapi64.dll`. The Mac was then found locked;
the login window owned focus, so no further game input was sent. Actual live
double-frame presents, free-roam quality and stutter resolution remain unverified.

The preceding session's 60-second critical-section warning was followed by a
read-only inspection showing that the named lock was already released. A later
render-thread snapshot waited inside XeFG's presentation path. Neither snapshot
identifies the original lock holder's blocking operation. An AGX compiled-variant
limit message and subsequent PSO compilation failures were also recorded in that
session; their causal relation to loading remains unresolved. These observations
must not be described as a diagnosed or repaired GPU deadlock.

Source references:
[OptiScaler loader routing](https://github.com/optiscaler/OptiScaler/blob/2fff542e/OptiScaler/hooks/LibraryLoad_Hooks.cpp),
[NVAPI hooks](https://github.com/optiscaler/OptiScaler/blob/2fff542e/OptiScaler/nvapi/NvApiHooks.cpp),
and [shared latency routing](https://github.com/optiscaler/OptiScaler/blob/2fff542e/OptiScaler/low_latency/input/input_common.cpp).

## Install and rollback

First run `install_xess_quality.py --apply` for the SR selection, then
`install_xefg_quality.py --sdk SDK_DIRECTORY --apply`. Both require FH6 stopped;
the latter verifies exact SDK hashes, the actual OptiScaler's XeFG/XeLL support,
the GPU receipts, output size and Quality ratio. It changes only `FGOutput`,
`InterpolationCount` and `IgnoreInitChecks`, and adds the two local SDK libraries.

Apply `install_xefg_pacing.py --apply` after the above selection, with FH6 stopped.
It requires the tested OptiScaler hash and existing native-resolution/Quality/2×
settings. To undo only the integrated-NVAPI change, restore the two
`before-integrated-nvapi-20261116` backups of `OptiScaler.ini` and
`fh6-profile.json` with the game stopped. Earlier graphics rollback instructions
below also undo this later selection.

Restore `OptiScaler.ini.before-xefg-20261115` and
`fh6-profile.json.before-xefg-20261115` with the game stopped to return to the
preceding FSR FG configuration. Restore the earlier `before-xess-quality` backups
as well only if also reverting SR. Do not delete saves, caches, or game files.
