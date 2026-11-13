# FH6 frame-generation chain evidence / 插帧链路取证

Status: the audit DLLs remain **diagnosis only**. The separate native-UI candidate
passed synthetic real-GPU contrast, but the subsequent actual FH6 test
**still shows car-tail ghosting despite that path executing**. It is not a
completed repair. The current candidate adds explicitly armed pixel captures
to locate the remaining fault; it does not select another SR/FG algorithm.
Keep the same native
Steam entry, Rosetta/D3DMetal route, native output, SR/2x-FG selections, audio,
saves and shader caches. Do not copy the Windows NVIDIA driver/MFG settings
into an FSR configuration.

旧日志核对了 28,548 次准备提交，帧号差均为零；28,546 次回调均请求一个
中间帧。它没有保留游戏传入的原始常量、区域偏移、完整资源绑定和像素内容，
因此不能由这些数字断言“同一帧的数据都正确”或“已显示正确的 2×”。

The pinned OptiScaler source (`2fff542e78e257a1e4ff5a1e21a69a166c78c335`)
does not forward resource extent left/top in `Sl_Inputs::reportResource` and
sets constants through the current FG slot rather than the resolved tagged
slot. These are code-inspection findings, **not proof the FH6 run triggers
either condition**. They must be correlated with real inputs before a repair.

## What is recorded

`streamline-audit.cpp` observes the main program's public `GetProcAddress`
import, forwarding to the existing resolver and returning wrappers only for
four named public Streamline exports. It preserves the exact original
constants, resource tags and return values. It does not patch an NVIDIA
module, Apple library, game executable, hardware capability check or DRM.
It must be loaded before the game resolves the exports. Missing export
coverage is reported as missing evidence, not success.

The bounded frame table is keyed by the actual token. It records depth,
motion vectors, hudless color, UI color/alpha, SR input/output color, pointer
identity, declared lifetime, resource state, format, dimensions and original
extent offsets. Constants include jitter, normalization scale, depth/MV
conventions, reset, camera values and exact matrix float bits. A TLS token
ties the nested SR call to the actual Streamline evaluation; it never guesses
identity from an 80 ms pointer/jitter matching window. Old and cancelled
tags do not become current-frame evidence.

`provider-audit.cpp` forwards all five public FidelityFX functions to the
**same existing provider**. Original contexts, descriptor pointers, linked
extensions, callbacks, resources and errors are preserved. Logs record SR
input/output, FG context flags and queue identity, configured hudless/UI
inputs, prepare parameters, generation rectangle and requested outputs.
CPU trace sequence/QPC values allow ordering; they are not GPU completion
timestamps. There are no added GPU commands in the installed observer.

No textures are silently replaced, no MV axis is guessed, no frame-generation
request is disabled, and no synthetic success is returned. A different resource
pointer can be a legitimate copy; the analyzer does not label it stale without
provenance. This observer does **not** read actual motion/depth pixels or measure
the final displayed generated/base-frame ratio. Those may require a subsequent
targeted GPU capture if the metadata is consistent.

## Build and check

Run `python3 build.py --cc PATH_TO_GCC_MINGW --out BUILD_DIRECTORY`.
`--deps DIRECTORY` reuses a header cache. Public MIT headers are pinned to the
same OptiScaler commit and its FidelityFX-v2 submodule
`7539ae8b6568ad29ed78a753404351daf3b5cca7`. Their hashes and source pins are
recorded in `build-manifest.json`. No NVIDIA/AMD game DLL or proprietary model
is distributed here. See `NOTICE.md` for header notices.

The authored `probe.exe` uses a small fixture named `sl.interposer.dll` and a
fixture FFX provider. It verifies unchanged public input descriptors, context
and failure forwarding, same-frame capture, late frames, untagging and scoped
evaluation tokens. It also executes a real D3DMetal texture copy/fence/readback
of 12,288 R32 pixels. **The fixture does not execute the real FG algorithm.**
The fixture DLLs are test artifacts and are never installed into the game.

`provider-versions.exe ORIGINAL_DLL OBSERVER_DLL sr|fg` uses an actual D3D12
device to compare the real providers' public version enumeration. On this Mac
both routes returned the same SR providers 3.1.6/2.3.4 and FG provider 3.1.6.
That proves forwarding identity, not image quality. Small outputs and the prior
game log digest/counts are under `../diagnostics/fg-chain-20261112/`.

## Install, capture, recover

With FH6 stopped, `python3 install.py --artifacts BUILD_DIRECTORY` validates the
build hashes and current configuration. `--apply` adds the observer ASI to the
existing FH6 plugin directory and two provider proxies under the **private FH6
prefix** at `C:\highball\fg-chain`. Symlinks point at the existing official
providers. Only the two `Libraries.FfxDx12SRPath/FGPath` keys change; all other
sections must compare equal. There is no second game download or cache copy.

The log is `OptiScaler-FH6-SDK/fh6-fg-chain.jsonl`. Run
`python3 analyze.py LOG` after or during the same game process. It checks the
last observed process only, reports coverage first, and compares native jitter,
signed MV normalization, exact SR/FG frame identity and generation bounds.
Do not call a partial or absent capture a verified repair.

Recovery: stop FH6, restore only the two library path values from the private
`rosetta-profile/fg-chain-audit.json` receipt, and remove only the matching
`fh6-fg-chain.asi` observer. The original provider DLLs were never overwritten.
Retain the source commit and build manifest; no other game's files, display
settings, Steam sessions, cloud settings, audio or caches need changing.

## Native UI candidate / 原生 UI 合成修复候选

The captured game process (Windows PID 228) supplied 1,349 sampled prepare
records with matching native depth/MV pointers, jitter and signed motion scale.
The same-frame SR/FG comparisons had no mismatches. In 410 sampled frames the
game supplied **both** HUDless scene color and premultiplied UI color/alpha.
The pinned OptiScaler `FSRFG_Dx12::Dispatch` nevertheless registers an empty UI
resource and relies on FSR's image-difference UI extraction. These counts are
sampled calls, not displayed-frame counts or a complete visual correctness test.

The pinned FFX inpainting shader computes a static factor from HUDless-vs-final
color differences and blends final color back into the generated image. A
non-UI scene/postprocess difference can therefore restore un-interpolated scene
pixels. NVIDIA's Streamline guide specifies that the native UI texture instead
has zero alpha outside UI and uses premultiplied composition. See:

- [Streamline native UI resource contract](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md)
- [Pinned FFX image-difference composition](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/7539ae8b6568ad29ed78a753404351daf3b5cca7/Kits/FidelityFX/framegeneration/fsr3/include/gpu/frameinterpolation/ffx_frameinterpolation_inpainting.h)
- [Pinned OptiScaler FSR FG integration](https://github.com/optiscaler/OptiScaler/blob/2fff542e78e257a1e4ff5a1e21a69a166c78c335/OptiScaler/framegen/ffx/FSRFG_Dx12.cpp)

`framegeneration-native-ui.dll` is a separate build of the owned provider proxy.
For eligible **same-frame** inputs, it passes a local descriptor copy containing
the native HUDless scene as `presentColor` to the unchanged official FSR 3.1.6
provider. This removes the scene/final difference from automatic UI extraction.
After the real interpolation dispatch succeeds, a compute pass composites the
native premultiplied UI onto the **generated output only**. The caller's final
backbuffer, original descriptor, real-frame presentation and swapchain callbacks
stay unchanged. This is not an FSR4/MetalFX substitution or a replacement of the
official optical-flow/interpolation algorithm.

Guards require the exact frame token, viewport 0, native valid-until-present
resources, matching full-frame RGBA8 UNORM dimensions, shader-readable UI,
matching HUDless pointer/state, one generated frame, UAV output and a unique
matching direct game queue with `allowAsyncWorkloads=false`. Unknown extension
chains, missing/untagged/stale resources, offsets/formats or asynchronous queues
pass through the original route and report that choice. No motion sign, hardware
feature bit or successful return code is fabricated. Descriptor heaps are
immutable, and referenced textures are retained in a bounded 32-binding cache
per context. They are released only after context teardown and queue drain;
GPU timeout retains the bounded slot rather than freeing in-flight objects.

The validation uses the installed **official FFX 3.1.6 provider**, actual D3DMetal
compute, a real DXGI swapchain/queue and the full candidate provider. An authored
Streamline fixture supplies controlled native tags; it is never installed into
the game. Three contexts process 12 synthetic moving-scene frames: old difference
extraction, clean scene plus native-UI reference, and the actual candidate.
The old path differs in 3,072 non-UI color components; candidate/reference error
is zero. Generated output differs from the current base image in 192 components,
so this is not a duplicate-base-frame test. Separate 127×95 GPU composition
checks 12,065 pixels with transparent/half/opaque UI and zero errors. The caller's
descriptors remain byte-identical. **This reproduces the failure mechanism in a
controlled scene, but does not yet prove FH6's actual car pixels are corrected.**

Both-zero generation dimensions are a documented behavior of this pinned SDK
implementation: they expand to the full backbuffer and ignore left/top. The
analyzer now handles that rule. The game's 58 early zero-sized requests were
not evidence of an invalid rectangle. View-space-to-meters 0 is also normalized
to 1 by the SDK and was not changed.

Build as above, then run:

```sh
python3 run_native_ui_probe.py --artifacts BUILD_DIRECTORY
python3 install_native_ui.py --artifacts BUILD_DIRECTORY
```

With FH6 stopped, append `--apply` to select the verified candidate. Only
`Libraries.FfxDx12FGPath` changes to `C:\highball\fg-native-ui`; SR stays in the
existing `fg-chain` directory. The original provider is an unchanged symlink.
No test fixture is installed. The installer checks source/artifact hashes,
successful GPU evidence, original-provider hash and the known observer ABI. It
refuses an active game or an unknown existing candidate. It preserves native
Steam/profile selection, resolution, SR settings, 2× FG, audio, saves/cloud and
all caches. `fg-native-ui.json` records the exact previous FG path and hashes.
Recovery requires only stopping FH6 and restoring that one FG path; no game
files, cache rebuild or re-download is required. Evidence and deployment receipt
are in `../diagnostics/fg-native-ui-20261112/`.

## Startup lifecycle correction / 开场初始化修正

After the first candidate was selected, a new game process remained at
"Loading, please wait". Its chain trace never dispatched SR/FG/native UI;
only a disabled 1378×900 10-bit FG context existed. A native sample showed
two threads in abort/TLS/malloc cleanup and the launcher output repeatedly
reported `RtlLeaveCriticalSection` ownership errors. **These observations do
not prove the owner-error cause or that our eager initialization caused it.**
Context creation took about 90 ms; the approximately 49-second gap between
swapchain creation and that call was game startup, not UI shader compile time.

The initial candidate nevertheless introduced an avoidable startup difference:
it imported D3D12 and the Wine HLSL compiler and built a compositor even for
disabled/10-bit opening contexts. The revised candidate has exactly the same
runtime DLL import set as the observation-only proxy: kernel/UCRT, **no newly
preloaded graphics/compiler DLL**. The owned 1,204-byte shader is precompiled
from `native-ui.hlsl` into `native-ui-bytecode.h`; its source hash is checked.
`compile-native-ui.cpp` is a build tool, never installed or loaded by the game.
To regenerate the bytecode, compile that tool with the MinGW compiler and
`-municode -ld3dcompiler`, run it under Wine with input HLSL/output CSO paths,
and emit the owned CSO bytes and HLSL SHA256 into the checked-in bytecode header.

FG context creation now records only context/device ownership. Queue selection
waits until FG is enabled; root/PSO initialization waits for a validated active
RGBA8 scene/UI frame. Root serialization resolves the already-loaded D3D12
export at that point instead of importing a new graphics DLL at startup.
The real-provider GPU regression adds a 10-bit disabled opening-context
create/configure/destroy lifecycle and verifies that it creates no compositor
pipeline. The active scene still executes the native UI fix with the same zero
reference error and actual generated/base difference. The runner rejects any
new runtime import relative to the audit proxy.

The hung process was ended under the user's standing explicit kill instruction;
no Steam or other game process was ended. The revised candidate was deployed
with the same settings (INI before/after hashes equal). Evidence, raw compressed
thread sample, import comparison, GPU regression and installed hash are under
`../diagnostics/fg-native-ui-startup-20261112/`. **Actual startup recovery and car
ghosting remain pending the user's next native-Steam run.** This correction is
not a claim that the Wine lock fault itself has been fully diagnosed or fixed.

## Actual-pixel capture / 实际 GPU 像素取证

The user's new car screenshot reproduces the ghosting. The captured process
(Windows PID 232) recorded 188 sampled `native_ui_dispatch` events before it
exited. This rules out treating "the new UI path never ran" as the explanation;
the earlier controlled GPU reproduction did not cover all actual-game inputs.

`gpu-capture.h` adds **two explicitly armed, same-frame snapshots**, containing:
depth plane 0, raw RG16-float motion vectors, native HUDless scene, native UI,
the original present image, generated output before UI, and generated output
after UI. It also saves the exact owned native-frame ABI, native camera/matrix
values and tags, and the matching FFX prepare frame/time/render size/jitter/MV
scale. Copies are encoded in the *actual interpolation command list*, at the
correct side of UI composition; all source states are restored. No algorithm,
motion sign, display resolution, feature bit or return code is changed.

Readback does not use an arbitrary delay as GPU completion. For this pinned
FFX swapchain and async=false, output reuse first queues the SDK composition
availability-fence wait on gameQueue. The next dispatch for that same output
then signals the capture fence after that wait. A worker maps/writes only after
the fence completes. Context teardown is a second drain path. Source textures,
readback buffer, device, provider module and reporter module remain retained
until the job finishes. Timeout retains the bounded pending objects and reports
failure rather than releasing in-flight GPU memory. There are at most two jobs
per context and at most 256 MiB per job. Do not interpret the capture frames as
unperturbed performance samples: copies have one-time bandwidth cost.

There is no always-running capture service. In the private FH6 provider directory
`C:\highball\fg-native-ui`, creating `capture-request` arms two non-reset eligible
frames. Wait until **actual free roam** before triggering it, so menu/login
resources are not captured as car evidence. The marker is consumed; later
requests can be triggered in the **same process** without swapping DLLs.
Completed folders under `captures` contain `capture.json`, seven pixel files
and `native-frame.bin`. `gpu_fence_completed` and `pixel_files_complete` must both
be true. Game pixels stay private and are not committed to this repository.

`analyze_pixels.py CAPTURE_FOLDER --png` uses only the Python standard library.
It checks frame provenance, actual device row layout, native-UI composition
error, RGB under transparent UI, non-UI scene/final differences, and finite
depth/motion distributions. PNGs are exports of the captured RGBA8 buffers for
inspection. Differences by themselves do not prove a wrong MV sign, a ghosting
root cause or a presentation fault. Inspect the scene/present and before/after
UI images first, then compare both captured frames' geometry and motion.

`run_native_ui_probe.py --artifacts BUILD_DIRECTORY --capture` validates this
route with the real official FFX provider, a real GPU and two synthetic frames.
The same generated/reference checks still have zero error. All fourteen pixel
files complete behind fences, have correct lengths and matching frame/prepare
identities; native-UI pixel analysis has zero mismatches. The game-native depth
format (DXGI 19) is separately exercised: D3DMetal reports a 4-byte plane-0 copy
layout (copy format 39); 12,288 depth values round-trip with zero errors.

The first new depth *test harness* incorrectly inferred an 8-byte stride from
the DXGI name and overran its mapped upload buffer. That failure log is retained.
The test now uses `GetCopyableFootprints` row bytes, as the capture code already
did. It was not evidence of a game, hardware or driver failure. The corrected
GPU test and capture summaries are in `../diagnostics/fg-pixels-20261112/`.


Loading/exit exception evidence
-------------------------------

`fh6-first-fault.jsonl` is separate from the capped graphics trace. The pinned
ASI records access-violation metadata through 32 site counters and a prefaulted,
1024-record ring. Exception callbacks only publish fixed data; a worker writes
JSON, with no faulting-pointer reads, formatting, file I/O, stack unwinding, or
frame/log locks in the callback. This neither handles exceptions nor changes
SR, FG, audio, resolution, or cache selection. Storage exhaustion skips evidence.

**First chance does not mean fatal.** The actual private Wine successfully
registers vectored-continue callbacks but did not invoke them for any of the 64
handled fixture exceptions. Missing `continue` records must not be used to infer
an unhandled exception. Correlate with persistent exception/exit stacks and the
last graphics events instead. Abrupt process termination can lose the final
100 ms of buffered records; the worker does not block process shutdown.

Use `install.py --observer-only` to update only the ASI while preserving the
selected, separately verified FG provider. `first-fault-probe.exe` raises 64 owned
continuable fixture exceptions across eight threads and verifies execution
continues. `probes/wine-wait-stack.cpp` (one directory up) reads saved Wine
syscall frames and bounded stack metadata without debugger attachment, thread
suspension, process writes, or a full memory dump. None is a game repair claim.

Saved-frame GPU replay and visibility control (2026-11-13)
---------------------------------------------------------

The rear-car duplicate now has an independent pixel reproduction using the
unchanged official 3.1.6 FG provider and two adjacent, fenced game captures.
`replay-capture.exe` creates no game/Steam process and touches no live game
resources. `prepare_replay.py FIRST_CAPTURE SECOND_CAPTURE --out DIRECTORY`
links private pixel files and records hashes and row layouts. It rejects HDR,
unknown depth formats, unfenced captures and nonadjacent frames. Four frozen
warmups reconstruct history; they do **not** recreate the game's full history.

Replay arguments are `ORIGINAL_FG_DLL INPUT_0 INPUT_1 OUTPUT_DIRECTORY [trace]
[tight-max-render] [mask-control]`. Default maximum render dimensions match the
captured display-sized context. The optional `trace` is limited to the actual
3472×2266 dataset: owned COM forwarding objects record public PSO names, AMD's
submitted compute-shader inputs and 33 output stages. No Apple code, driver
object, vtable, protected executable or binary shader input is patched. At most
64 output-region readbacks and 512 MiB of selected field readbacks are retained.
Resources transition back to their prior state and are mapped only after the
owned replay queue's fence completes. Stage crops evaluate where the duplicate
starts; they do not constrain the visibility rule to a car or screen region.

The duplicate is already in `INTERPOLATION`, before native UI composition.
Previous-source RGB exactly matches the preceding saved scene. Actual mask
shader scalar math matches all 15,537 sampled valid-vector interior GPU points
(`verify_mask_math.py TRACE_DIRECTORY`), while real previous-frame depth shows
some supposedly valid background samples were hidden by the car. This identifies
a visibility-error contribution, not a universal verdict on FSR or D3DMetal.

**Discarded suspicion:** actual 3.1.6 interpolation intentionally copies red to
output alpha and writes inpainting weight into a separate UAV. A different
cached source revision had a different layout. Alpha=red is not the defect.

`history-visibility.hlsl` is an **experimental saved-frame GPU control**, not an
installed repair. It clears only the previous-color permission when current
motion agrees, previous motion disagrees, camera reprojection confirms a static
world point, and previous visible depth proves an occluder. It retains the
current branch's half-frame reprojection and official 2× generation. It uses
conservative experimental thresholds, not game names, car positions or fixed
screen rectangles. `prepare_visibility.py PREVIOUS CURRENT TRACE OUT` validates
provenance and preserves actual, potentially different, depth/motion row pitches.

Build `history-visibility-probe.exe` with `build.py`; compile the HLSL using the
owned `compile-native-ui.exe SOURCE_HLSL OUTPUT_DIRECTORY/history-visibility.cso`.
Run the probe as `INPUT_DIRECTORY SHADER_DIRECTORY OUTPUT_MASK_FILE` in the
existing profile. A queue-completed GPU readback is saved with the original
mask layout. To evaluate it inside official FG, put that file at
`OUTPUT_DIRECTORY/override-mask.bin` and invoke the independent replay with
`trace mask-control`. This explicit experiment affects only owned replay textures.

On the saved car-tail case, the GPU rule corrected 56,086 render pixels, changed
zero unsupported/outside pixels, and declined all changes for an incompatible
camera-matrix negative control. In 21,174 interior ghost-region samples, RGB
error against the visible current-warp reference dropped from 11.65 to 0.246;
19,800 corrected samples still differ from the base frame, confirming this did
not merely replace the generated frame with the original. These results are
limited to this captured pair. Raw game pixels remain local; receipts are in
`../diagnostics/fg-replay-20261113/`.

**Not ready for game deployment:** safe runtime previous-depth/MV ownership,
queue ordering, scene changes and performance still require integration and
validation. No game settings, provider DLLs, caches or native Steam session were
changed during these independent replay experiments.
