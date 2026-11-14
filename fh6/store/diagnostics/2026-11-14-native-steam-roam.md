# November 14 Xbox free-roam validation

Native macOS Steam launched the existing licensed Store package. The profile
used 3472×2266 output, a 2314×1510 internal render for FSR 3.1.6 Quality,
and requested one intermediate frame per interpolated frame. The player
confirmed the UI ghost was gone and the car-tail ghost, exhaust loss and
frequent stutter remained. The game stayed open for this analysis.

## Car-tail history

The live run produced 12 sampled successful history dispatches, then 12
sampled `binding-budget` skips. A 32-entry descriptor cache used a tuple of
history slots, current scene and output resources for its key. It held every
tuple until the context was destroyed. The eight history slots now have
reusable descriptor heaps. Scene/output references are retired with their
flight and a slot is rewritten only after the existing direct-queue fence
completes. The generated frame and native UI composition are still used.

A local D3D12 regression used saved game inputs at native output through
the isolated Store renderer. It passed 43 frames with five rotating outputs,
42 corrections, no remaining references and the in-flight overwrite
guard. This exercises 40 distinct combinations that exhausted the old
32-entry pool. The normal and absent-UI real FFX 3.1.6 checks completed with
zero UI errors and two fenced captures each. The new provider SHA-256 is
`0f6b43999a68e77bfd318d6a16cfcc340c881be41d76ae4e228c1c0f4f77f773`.
The selected live provider SHA-256 at capture time was
`eb7fd784ee4f40e16293b72f9c1233555a8cc9c5940821c2ff8d8286a1a56713`.
The two captured game frames were adjacent but the car was stationary.
Final car-tail validation requires a moving in-game scene after the new
provider is loaded.

## Present stalls

A six-second Metal trace matched all 331 present requests to command
submissions and completions. Three request gaps were 139.6, 351.0 and
457.9 ms. In the longest gap, the previous present completed in 0.695 ms,
the next present command was submitted 456.949 ms after that completion,
and then completed in 0.443 ms. Across all title queues only 29.544 ms of
the long gap had recorded active GPU work. The trace had no drawable wait
intervals for the title and no usable drawable presentation timestamps.
One sampled stack from the submitter lacked resolved symbols. These
measurements implicate the interval before presentation submission. The
three gaps had the same Metal queue worker as submitter. In a separate
six-second System Trace on that PID, this worker showed 4.1 ms runnable,
120.3 ms running, and multiple blocked spans including 43.8–48.0 ms.
Most identified wakeups came from one in-process Wine thread. The
independent recordings cannot match the 457.9 ms present gap to a
precise wakeup or stack, so the original blocking dependency remains open.
The player reports that the Store run hitches more frequently than their
last Steam run. The two observations were not measured over a matched route.

The same run logged 221 pipelines marked no-op, including 148
`sv_shadingrate0` failures and 73 fragment compilation errors. The
[upstream MetalHorizon notes](https://github.com/alexandreunitypro/MetalHorizon-FH6-Mac-Launcher/blob/2b8a57c126c51efcb2adefcd275c2d08ef621e3e/docs/PASSATION-IA.md)
describe these signatures. In a corrected A/B, the owned probe executable was separated from the candidate
DLL so Windows search order could not silently load the same shim twice.
Both variants used the installed D3DMetal `apd12.dll`. The installed shim
reproduced the `sv_shadingrate0` refusal and read back `00000000` through
both the descriptor and pipeline-stream APIs. The pinned MetalHorizon
shim restored `00ff7f7f` through both APIs. Disabling the upstream shading
rate transform reproduced the refusal and zero pixel on the same shim.
The combined shim and recycled FFX provider then passed on the Store's
isolated D3DMetal setup, with zero reference-pixel errors and zero UI
errors. Correlation between these shader failures and the measured stalls
remains open.
The raw thread sample and Metal record stay local.

## Exhaust, Xbox Live and Car Pass

The running game loaded Wine's existing 8- and 12-channel spatial mixer on
the two-channel built-in speakers. It had no mapping for the existing
Highball audio-buffer dylib. An isolated WASAPI probe through the same
system shell reproduced the missing dylib. Passing the verified path under
a normal environment name and exporting `DYLD_INSERT_LIBRARIES` in the final
FH6 Wine wrapper restored the module and submitted 24,000 audio frames.
This verifies the launcher change. It does not identify the game sound
source or establish that exhaust audio is audible again.

XSTS title authentication succeeded and Xbox Live returned a WebSocket
101 upgrade. Three Xbox Live requests then timed out (WinHTTP 12002).
A socket snapshot showed no connected UDP peer in the dedicated session
port range. Joining a real session remains unconfirmed. The 32 KiB network
log filled, limiting our view after initial connection.

Car Pass-related inventory calls and signed receipt checks succeeded.
The add-on enumeration returned success, and the game's license-token
result was read. That instrumentation did not distinguish zero returned
Car Pass add-ons from a populated list. The follow-up source patch adds
an empty/nonempty event after signed checks, without logging IDs or tokens.
The offline catalog, durable and add-on exercises returned their expected
results. A paired transport candidate was not accepted after isolated HTTP/2
and WebSocket regressions failed. The installed WinHTTP passed the same
six-worker loopback test, while the rebuilt candidate faulted on execute
access before that test completed. The network module was left in place.

To reproduce the validated frame provider, build with
`fh6/rosetta/fg-chain/build.py`. Run the two `run_native_ui_probe.py`
commands in the [previous procedure](2026-11-13-nexus-and-playback.md),
then `history-post-probe.exe` with adjacent private captured inputs.
To build the optional renderer shim, check out the pinned MetalHorizon
commit linked above and use an x64 LLVM MinGW toolchain:

```sh
"$CC" -shared -O2 -Wall -o /path/to/d3d12.dll \
  "$UPSTREAM/tools/tsshim/d3d12.c" \
  "$UPSTREAM/tools/tsshim/dxil_shadingrate.c" \
  "$UPSTREAM/tools/tsshim/inspect.c" "$UPSTREAM/tools/tsshim/d3d12.def"
python3 fh6/store/install_fg_capture.py "$FG_BUILD" --renderer /path/to/d3d12.dll
```

Run installation after FH6 closes. The installer checks the locally
tested renderer build hash. It updates only the Store-only D3D12 copies
and receipt alongside the generated-frame wrapper. The signed-off
baseline D3D12 shim remains in the shared engine and can be restored to
those two Store-only paths with its corresponding receipt digest. The
audio script update first deployed during this run takes effect on the
next launch. To revert the frame provider,
rebuild the previous Git revision and restore its matching provider and
profile digest while the game is closed. Keep the native Steam shortcut,
resolution, scaling, generation, audio device, cloud data and caches.

## Deployment after closing the captured process

Stopped only the captured FH6 process. The native Steam client stayed open.
The combined GPU-tested frame provider and shading-rate shim are installed
for the Store profile. Also verified the existing FH6 audio-buffer handoff
and installed the updated, title-only WinHTTP and Nexus game-runtime modules.
The launcher configuration check passed with 3472×2266, FSR 3.1.6 Quality,
2× generation and official Nexus/PlayFab cloud sync. No game was relaunched.

The WinHTTP candidate initially failed its transport regression with the
LLVM-built PE. Rebuilding with the same GCC-based toolchain as the deployed
module passed all six concurrent WebSocket workers against a local server
and a public HTTPS request with HTTP/2 actually negotiated. The updated
status logger identifies Xbox session-directory, matchmaking and RTA hosts,
reserves space for late failures, and omits successful catalog responses.
It records service classes, response status and WinHTTP error numbers.
The game-runtime probe covered expected catalog/add-on requests and unrelated
requests, including the no-credential case. The new marker records whether
an authenticated add-on result is empty. Both changes preserve the game's
normal network and genuine entitlement checks. Joining an online session
and retrieving a specific owned car remain unverified in game.

The live Metal trace has no submissions on either observed worker for
451.6 ms inside its longest 457.9 ms gap. A different gap has 321.1 ms of
submission silence before its first batch. The most frequent present-worker
waker in the separate scheduler trace also appears in normal frame-pacing
stacks, but has no CPU samples during the three long gaps in the original
Metal trace. The longest associated CPU sample sequence in the 351.0 ms
interval is on another Wine thread whose frames are unresolved. Driver
per-thread events in the earlier Metal trace are empty. Consequently the
existing captures cannot identify the dependency responsible; changing FFX
pacing based on the clock-polling samples would be speculative.

`metal_presents.py --record` now records Thread Activity on the same clock
as Metal submission and completion. It reports the submitter's scheduler
states, recorded wakers, and a nearby stack from the profiler when one is
available. A nearby sample documents proximity in time, not ownership of
any fence. The combined capture matched 194 of 194 Metal requests to
submissions and completions on an isolated D3D12 GPU probe with correct
64-word readback. It found six intentionally induced 236–240 ms gaps and
showed the corresponding submitter blocked for almost the entire gap.
Arm this capture against the existing FH6 PID during the next user-started
free-roam test, alongside the provider, Nexus and audio checks. Keep the raw
trace and any per-thread identifiers local.

## FHE11 after the following native Steam launch

The user's next launch reached the loading screen, then displayed FHE11 after
an access violation (`c0000005`) inside the real FSR 3.1.6 frame-generation
provider. Win32 window enumeration recovered the original error text, then
UTF-16 inspection confirmed the title, SysLink body and custom button held
widened UTF-8 bytes (`00e3 0080 008a` for `300a`). The materialized executable
imports `TaskDialogIndirect` by COMCTL32 ordinal 345. The previous plugin did
not intercept this API. The new plugin repairs valid UTF-8 containing CJK in
TaskDialog fields and button labels, including byte-widened and CP1252 forms.
The ordinal-345 regression verified the display strings and CJK glyphs in
both cases, and retained already-correct Unicode.

The first fault was at `framegeneration-original.dll+0x7c25`, dereferencing
an invalid context value while setting the AMD Anti-Lag 2 frame type. The
[SDK presenter](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/Kits/FidelityFX/framegeneration/fsr3/dx12/FrameInterpolationSwapchainDX12.cpp)
checks `GetPrivateData` success, but does not check the returned size before
reading the enable flag. The isolated D3DMetal swapchain reproduced `S_OK`
with zero bytes after clearing the key and with eight bytes for a short
record. The flag was left untouched in both cases. The trace does not show
which writer put the record into the live swapchain or its exact length.

The FH6 Store-only provider now treats the AMD driver key as unavailable
on the D3DMetal swapchain created for FSR. Other DXGI private data round-trips
unchanged. The isolated swapchain passed the malformed-data controls and
four presents. Both real FSR GPU regressions passed with tagged UI and with
no HUDless/UI input, and the fenced captures still showed generated output.
The candidate and dialog plugin were installed together after the crashed
process exited. The launcher check verified the existing native Steam entry,
3472×2266, FSR 3.1.6 Quality, 2× frame generation and the Nexus repair layer.
No game was relaunched after deployment. In-game verification is pending.

Rebuild using `fh6/rosetta/fg-chain/build.py`; run the two existing real-GPU
capture commands with `--latency-guard`. Build the dialog plugin with
`fh6/arm64/src/dialog.c` and `fh6/arm64/src/version.def` as x64 PE. The dialog
fixture is `fh6/rosetta/probes/taskdialog-utf8.c`, with its ordinal import
library, and its manifest embedded as resource type 24, ID 1. The installer
pairs the two GPU receipts and dialog verification with the candidate hashes
using `install_fg_capture.py ARTIFACTS --dialog PATH_TO_ASI`.

## Live validation after FHE11 and TaskDialog repair

The next launch from native Steam reached online free roam and was still
running after 38 minutes. The FH6/D3DMetal Anti-Lag guard logged installation.
The first-fault log retained its pre-launch timestamp; no new FHE11 was
observed in this run. This run did not produce a fatal dialog to retest the
Chinese text in-game. The player reported restored exhaust audio and online
play, and confirmed residual car-tail ghosting and stutter. Xbox title auth
and the RTA WebSocket 101 upgrade were recorded. The generic Xbox host still
logged three timeouts; the instrument did not return a session-join result.
The signed add-on query returned a nonempty result. No particular Car Pass
car was tested during this run.

All 778 interpolation calls in the first snapshot of the new launch segment
returned success requesting one generated frame. Four native-resolution
images of generated frames were also read back after GPU fences. In the
moving wet-road pair, the generated output differed from the rendered frame
at 45,422 and 45,770 of 123,256 sampled pixels. The extra bumper/exhaust
contour was visible before native UI composition and persisted afterward;
the corresponding HUDless and normal rendered inputs did not contain that
contour. There were no sampled native UI composition mismatches. The existing
history correction was active for the moving pair. Replaying its actual
adjacent inputs through the official FFX provider and the installed candidate
reproduced a residual trail. The current candidate changed 320 of 5,162
trail-region samples relative to unmodified replay and reduced mean absolute
RGB difference from the current rendered image there from 3.31 to 2.82.
That comparison is a diagnostic, since the current base image is not a
reference for all correct intermediate pixels. Raw image evidence stays local.

A combined Metal/scheduler capture matched 234 present requests with
submissions and GPU completions. Its 1,832, 1,106 and 201 ms gaps occurred
mostly before the next present submission. During the three gaps the same
D3DMetal submitter was blocked for 1,831, 1,106 and 201 ms respectively;
recorded GPU work covered 27.2, 3.6 and 25.0 ms. There were no recorded
drawable-wait intervals. The 1,106 ms gap ended with a wake by a Wine thread
whose nearby address is Rosetta JIT and unresolved; it and a second
clock-polling thread were running for nearly the full gap. The other long
gaps ended with a different Wine thread seen encoding D3DMetal commands,
and overlapped Metal resource tracing on a separate thread. Some gaps may
therefore include profiler overhead. Wakeups identify when submission
resumed; they do not identify the fence or producer responsible. The
presented-handler timestamp domain was incompatible with the trace clock
(253 seconds versus 6 seconds for the same callbacks), so the analyzer now
rejects that display FPS instead of publishing it. The recording timeout was
also raised because Instruments took over a minute to save this live trace.
No game or graphics settings were changed. No restart was required.

## Moving car-tail replay and FFX fence correlation

The player also uses OptiScaler with FSR 3 frame generation on Windows and
reports clean output there. The two runs have not been matched by DLL hash,
FG input route, scene and texture contents. The Mac evidence locates where
this trail first appears. It does not isolate a Windows/macOS difference
or establish that D3DMetal executes a shader incorrectly.

In the previously captured wet-road pair, projected depth and previous
visible depth both described the road in the extra bumper contour. At one
sample they were about 0.0284 in reverse Z and the camera and motion
reprojections agreed within 0.2 render pixels. Previous motion still
varied by roughly 26 output pixels, and the two source lumas were roughly
10.1 and 2.0. The generated luma was about 5.0. The older correction
required a nearer previous surface, so it could not reject the stale
color in this example. Stage readbacks from the real FFX replay reproduced
the contour before UI composition. The disocclusion-mask arithmetic check
matched 13,536 sampled interior pixels on these inputs.

The history shader now also handles a narrow same-depth case. It waits for
consecutive history, camera and game-motion agreement on the current
surface, previous motion disagreement and a sufficiently darker previous
color that actually contributes to the generated pixel. Within those
constraints, it reduces the old color's weight in the already interpolated
output. The original geometry-occlusion correction is retained. In
13,268 qualifying samples in the wet-road region, average absolute luma
difference from the motion-reprojected current color was 6.508 for the
unmodified replay, 6.198 for the installed revision and 1.251 for the new
build. This is a diagnostic of a specific history error, not a whole-frame
score or a substitute for an intermediate-frame reference. Replay
reconstructs a short history, so final validation is still in free roam.

The 1,106 ms sample has about 3.6 ms of recorded GPU activity. Its wakeup
and sampled Rosetta address do not identify a named fence. The optional
FH6-only observer records persistent polling of named FFX fences and the
value that advances them, using the same QPC domain as the provider log.
It diagnoses sampled `GetCompletedValue` calls; event-only or unrelated
waits are outside its coverage. In the isolated D3DMetal probe it logged
an interpolation fence stuck at zero at 80 ms and advancing to one around
137 ms when a deliberately queued dependency was released. Real FFX 3.1.6
and pixel validation passed with both tagged and absent UI. That controlled
stall validates the observer, not the identity of the live dependency.

Run both `run_native_ui_probe.py` Store checks with `--latency-guard` and
`--fence-diagnostics` on a separate prefix. The title-only installer can
set `--fence-diagnostics` when both fresh GPU receipts verify the build; an
ordinary install leaves the observer disabled. No audio, networking, Steam
shortcut, resolution, scaling quality or cache changes are required for
this candidate. Keep the captured image buffers and raw scheduler data
local. The existing game was kept running and the candidate has not been
deployed in this session.
