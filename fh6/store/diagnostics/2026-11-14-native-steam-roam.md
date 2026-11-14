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
specific thread dependency and cause of the repeated hitch are still open.
The player reports that the Store run hitches more frequently than their
last Steam run. The two observations were not measured over a matched route.

The same run logged 221 pipelines marked no-op, including 148
`sv_shadingrate0` failures and 73 fragment compilation errors. The
[upstream MetalHorizon notes](https://github.com/alexandreunitypro/MetalHorizon-FH6-Mac-Launcher/blob/2b8a57c126c51efcb2adefcd275c2d08ef621e3e/docs/PASSATION-IA.md)
describe these signatures. On this machine, the upstream simplified VRS
probe returned the same GPU pixel on both isolated shim variants. This is
insufficient to adopt a replacement or to attribute the measured stall to
the compilation failures. The raw thread sample and Metal record stay local.

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
and WebSocket regressions failed. No new network module was installed.

To reproduce the validated frame provider, build with
`fh6/rosetta/fg-chain/build.py`. Run the two `run_native_ui_probe.py`
commands in the [previous procedure](2026-11-13-nexus-and-playback.md),
then `history-post-probe.exe` with adjacent private captured inputs.
Install with `fh6/store/install_fg_capture.py` after FH6 closes. To revert,
rebuild the previous Git revision and restore its matching provider and
profile digest while the game is closed. Keep the native Steam shortcut,
resolution, scaling, generation, audio device, cloud data and caches.
