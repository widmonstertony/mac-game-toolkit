# FH6 / Highball Wine 11.0 exception-handling port

地平线6专用的免费 Highball 移植。已完成本机兼容性验证，作为地平线6独占的 Rosetta＋D3DMetal 配置接入原生 Steam。**尚待用户验证实际游戏帧时间、重影和加入在线会话。**它不包含 CrossOver 应用，也不修改 Apple 闭源代码或游戏文件。

## Scope / 范围

Port MacGameFix's public Wine exception-chain and CPU-fidelity changes to the existing Highball Wine 11.0 / CrossOver 26.3 LGPL source. Keep the original native Steam client and external Proton-ish Steam bridge. Do not replace the shared Highball engine or change another game's configuration.

The dispatcher still executes the application's real exception handlers; unsupported instructions, incomplete contexts, nested/second-chance paths and memory faults retain normal dispatch. The FH6 fast path requires its expected executable headers, plus a deployment SHA256 guard. It is not a generic feature-bit override or an executable patch.

## Source pins / 来源

- [MacGameFix](https://github.com/MathiasKowoll/MacGameFix/tree/ddb2c25fbddaced6abae70f73ae17d408b9e1a08), commit `ddb2c25fbddaced6abae70f73ae17d408b9e1a08`: Wine 11.0 patches 0017–0020, Wine 11.21 patches 0021 and 0023–0025; authored exception-chain probes from its retained source overlay.
- Wine 11.0 LGPL source archive: MacGameFix v1.6.2 `wine-source-crossover-26.3.tar.xz`, SHA256 `cdbe7426f4247bc4a3a87444a30211e344112ec5d723e75d7141e56bfdb58d9d`.
- Highball's `highball-engine` commit `8b315e7`, patches 0001–0024 excluding held patch 0013, matching the r28 component series.
- Networking: MetalHorizon WebSocket fix plus this repository's strict dual-stack fallback. The IPv6 fallback must really succeed; invalid TTL and invalid sockets still fail.

## Port decisions / 移植差异

Wine 11.0 uses TLS thread state where Wine 11.21 passes an explicit thread-data pointer. Optional bounded native timing was adapted to the old API. The 11.21 alternate-stack diagnostic was omitted; normal exception-stack behavior was left unchanged. The upstream diagnostic-only NotProton loader detour was omitted because this installation already has an external Steam forwarder; a second detour must not silently replace it.

`wine11-highball-exception-chain.patch` applies after the Highball patch series and includes the CPU-fidelity changes, bounded exception chain, register-only ALU bridge, timer-overflow correction and optional diagnostics. `probes/` contains the upstream authored instruction/state tests. Wine-derived code is LGPL-2.1-or-later; see COPYING.LIB and each source's retained notices.

## Validation status / 验证状态

Five authored regression modes passed on this Mac: disabled, enabled, clean, quiet and no-resume. Each mode covers 1,090 instruction combinations and 132,160 full-state comparisons, plus multithreading, nested exceptions and genuine memory-fault fallbacks.

The installed profile passed native Steam online and main/worker/main interface queries, actual D3DMetal clear/copy/fence/readback (8,192 pixels, zero errors), real dual-stack UDP and six concurrent WebSocket workers, HTTP timeout preservation, and a 12-channel spatial mix (24,000 frames).

Two concrete compatibility defects were found rather than skipping failed checks:

1. The LLVM-built Wine 11.0 network module returned error 122 instead of a timeout; rebuilding the PE modules with GCC restored the real timeout (10060), without weakening the test.
2. lsteamclient intentionally allocates SteamClient interfaces inside steamclient64.dll's `.data` for API compatibility. A normal MinGW DLL places CRT state there too; thread attach overwrote the interface's vtable pointer. Direct lsteamclient queries and the original loader-detoured runner passed, whereas the rebuilt runner plus an ordinary forwarding DLL failed. The FH6-only forwarder now has a dedicated 64 KiB data arena and **no CRT startup or thread callbacks**. The same main/worker/main query passes with the interface remaining inside the forwarding module.

No shared Proton-ish or Highball modules were replaced. The old FEX profile remains available via the per-game selector; its existing game files and user-data/cache location are reused. There is no second game download. Current actual upscaling is **FSR 3.1 via OptiScaler**, with the game's existing DLSS-input/2× frame-generation settings; this port does not relabel that as FSR 4.

No gameplay performance improvement, online-session success or 2× frame output is claimed from these CPU tests. Those require the final compatible profile and the user's game test. Keep native resolution, existing upscaling/2× FG, audio and caches during that test.


## Installed route / 当前入口

The original Steam launch wrapper checks `~/Library/Application Support/protonish/launchers/2483190/rosetta-profile-enabled`. When present it calls `rosetta_profile.py`; otherwise it retains the existing ARM64 route. The private engine/prefix lives in the same launcher's `rosetta-profile` directory. A module checksum manifest prevents silently loading a different build. The executable SHA256 must match the pinned FH6 build before the optimization can launch.

The original FH6 user-data/cache directory is linked into the private x64 prefix, so saves and existing configuration still resolve to the same files. Original Steam and Highball engines, other game profiles, graphical settings and game binaries remain untouched. The normal native Steam runtime shim and API/Cloud behavior are preserved; actual cloud upload/download still needs the user's normal game test.

## Build / Recovery

Use `build.sh` with an existing GCC MinGW compiler, Bison and the macOS SDK. It fetches the pinned **public LGPL sources**, applies Highball and this port's source patches, and builds only the paired ntdll and two network modules. No CrossOver application is needed. `payload.zip` contains those modules and the CRT-free Steam forwarder; `validation.json` records their SHA256 hashes. To recover this installed version, extract only the listed profile-relative files into the private `rosetta-profile` directory and restore `profile.py` as the launcher's `rosetta_profile.py`. Never overwrite the shared engine. Removing only `rosetta-profile-enabled` selects the preserved ARM64 route for the next launch.
