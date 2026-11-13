# FH6 frame-generation chain evidence / 插帧链路取证

Status: the audit DLLs remain **diagnosis only**. A separate native-UI candidate
has passed real D3DMetal/official FFX GPU contrast and was selected for FH6;
**the game's visual ghosting result is still pending user testing**. Keep the same native
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
