# FH6 XeSS / XeFG live pacing investigation

The native Steam launch at 09:52 on November 16 reached the title, loading
sequence and garage cinematic. It did not complete free-roam verification.
XeSS 2.0.2 initialized at 2314×1510 → 3472×2266. XeFG 1.3.1 activated with one
interpolation. The read-only observer's in-memory counters reached 1,744 queries
reporting two frames, interpolation success and enabled state, with zero query
errors. These are status-query counts, **not unique presented frames or FPS**.
The bounded 100-line log filled during menus; its disabled/warmup records alone
would have incorrectly suggested that interpolation never activated.

## Live CPU and Metal evidence

The rendering thread waited inside Intel's XeFG present path. An XeFG worker's
saved stack was after a D3D12 fence GetCompletedValue / SetEventOnCompletion /
WaitForSingleObject sequence. A five-second Metal System Trace attached to the
same process contained 177 application command-buffer submissions. Several
submission-to-completion intervals exceeded 1.1 seconds. For example:

| Command buffer | App submission | Driver mapping | Final completion | Total |
| --- | --- | --- | --- | --- |
| `0xe346bf24` | 5.268380 s | 5.911714 s | 6.398770 s | 1130.390 ms |
| `0xe346bb96` | 2.435338 s | 3.075681 s | 3.550435 s | 1115.097 ms |

The terminal GPU execution slice for each of these buffers was only about two
microseconds. Thus the complete delay must not be attributed to that terminal
command's computation. Earlier submissions, driver scheduling and dependencies
contribute. The trace did not provide usable shader execution intervals to assign
every preceding pass to scene rendering, upscaling or interpolation.
No command-buffer error was recorded in this bounded trace.

The 60-second Wine critical-section warning was not captured while held; the
subsequent inspection found owner zero and recursion zero. This remains distinct
from the measured GPU completion delays. Neither it nor the historical 271 ms
event has been proven to share the same cause.

## Full-resolution isolation

After FH6 exited, separate programs used the same renderer and an isolated prefix.
See `../xefg-validation/2026-11-16-full-resolution-timing.json` for all samples.

- XeSS alone: warmed CPU encoding took approximately 0.16–0.25 ms, but submission
  and fence completion took 288–300 ms. Output readback was included in the latter.
- XeFG alone, replaying the saved real road pair: 12 presents reported successful
  two-frame output after the warmup. Subsequent upload/tag/fence stages took
  424–456 ms. XeLL sleep was below 0.04 ms. Pixel capture/writeback hooks were not
  armed for this timing run. These wall times include dependencies on preceding
  asynchronous interpolation, not just the current upload or one inference kernel.
- The isolated Metal 4 comparison reduced those stages to approximately 248–252 ms
  for SR and 375–400 ms for FG. That is still not an acceptable gameplay result;
  Metal 4 was **not** enabled in the live game profile.
- D3D12 timestamp intervals closely tracked CPU encoding time rather than the
  completion waits. They are not accepted as GPU duration evidence on this route.

These tests establish a severe full-resolution slow path independently of the
game scene and the OptiScaler integration. They do not identify the exact slow
shader or establish that every historical stutter has this cause. Earlier small
GPU success tests and frozen-pair image comparisons were insufficient to validate
full-resolution gameplay performance.

## Native Apple NGX / MetalFX control

The installed Apple NGX DLL and Unix companion were paired in an isolated staging
directory according to Apple's bundled instructions. Init and SR capability
queries succeeded. Frame-generation capability queries and feature-11 creation
failed with `0xBAD00000`; D3DMetal logged:

> Unsupported: Frame interpolation not present on this OS: 11

Both Metal 3 and Metal 4 paths returned the same result on this machine. This
does not mean that native arm64 MetalFX interpolation is unsupported: the earlier
native replay succeeded. This was not sufficient to identify a bridge limitation. Later correction:
the isolated probe lacked the `nvngx_dlssg.dll` application file marker. Running
it from the existing game directory resolved the gate; feature-11 creation
and real intermediate pixels then passed. See the
[follow-up evidence](2026-11-16-metalfx-upstream.md).
The [WineForge maintainer's corrected test](https://github.com/Alien4042x/WineForge/issues/15#issuecomment-6639934342)
reports the same GPTK 4 beta 3 limitation. A library containing DLSSG symbols or
an upstream feature list does not establish that the installed bridge can run it.

No new graphics backend, resolution, texture setting, audio, network, save or
cache change was installed during these comparisons. The existing XeSS/XeFG
selection remains installed, **not certified as fixed or playable**. Vehicle
ghosting, free-roam pacing and long-session stability remain unverified on it.
Raw system traces stay local; they are not included in the repository.
