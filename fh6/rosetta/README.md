# FH6 / Highball Wine 11.0 exception-handling port

地平线6专用的免费 Highball 移植。CPU、Steam 接口和独立 GPU 检查已通过，作为地平线6独占的 Rosetta＋D3DMetal 配置接入原生 Steam。**实际启动随后暴露了运行库和 NvAPI 初始化缺陷；独立检查通过不代表游戏兼容，仍待实际启动、帧时间、重影和加入在线会话验证。**它不包含 CrossOver 应用，也不修改 Apple 闭源代码或游戏文件。

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

### Startup integration repair, 2026-11-12

Actual native-Steam startup revealed two failures missed by the earlier probes:

1. The source container's `vcruntime140_1.dll` has PE Machine `0x8664` but contains ARM64EC code and nonzero `CHPEMetadataPointer`. Copying it by Machine value alone caused `c0000005` during DLL initialization. The existing official Microsoft `vc_redist.x64.exe` installed the x64 runtime into the **private FH6 prefix**. It initially retained the higher-version hybrid DLL; retaining that one file under another name and running the official `/repair /quiet /norestart` installed the correct x64 module. Five real DLL load checks passed afterward. `profile.py` now checks CHPE metadata and rejects that retained ARM64EC file in a real-file regression.
2. The real game then failed initializing OptiScaler `VERSION.dll`. Loader/exception tracing showed the D3DMetal builtin NvAPI being loaded before a null-code-pointer exception. A separate NvAPI probe also failed with that provider, while the existing game distribution's `fakenvapi.dll` passed initialization and interface queries. The private FH6 prefix now uses that existing provider as native `nvapi64.dll`; default-name loading, queries and initialization return success. This does not assert NVIDIA hardware exists. The game's graphics settings were not disabled or changed for this repair.

The startup diagnostic logs remain private on this machine. The installed private manifest records repaired runtime hashes. Do not redistribute Microsoft's runtime DLLs in this repository; use the official installer on another device. These integration checks still do not prove actual game rendering or frame generation. At this checkpoint Heat was running, so another GPU game was not launched concurrently.


## Installed route / 当前入口

The original Steam launch wrapper checks `~/Library/Application Support/protonish/launchers/2483190/rosetta-profile-enabled`. When present it calls `rosetta_profile.py`; otherwise it retains the existing ARM64 route. The private engine/prefix lives in the same launcher's `rosetta-profile` directory. A module checksum manifest prevents silently loading a different build. The executable SHA256 must match the pinned FH6 build before the optimization can launch.

The original FH6 user-data/cache directory is linked into the private x64 prefix, so saves and existing configuration still resolve to the same files. Original Steam and Highball engines, other game profiles, graphical settings and game binaries remain untouched. The normal native Steam runtime shim and API/Cloud behavior are preserved; actual cloud upload/download still needs the user's normal game test.

## Build / Recovery

Use `build.sh` with an existing GCC MinGW compiler, Bison and the macOS SDK. It fetches the pinned **public LGPL sources**, applies Highball and this port's source patches, and builds only the paired ntdll and two network modules. No CrossOver application is needed. `payload.zip` contains those modules and the CRT-free Steam forwarder; `validation.json` records their SHA256 hashes. To recover this installed version, extract only the listed profile-relative files into the private `rosetta-profile` directory and restore `profile.py` as the launcher's `rosetta_profile.py`. Never overwrite the shared engine. Removing only `rosetta-profile-enabled` selects the preserved ARM64 route for the next launch.


### Exception initializer reentry repair, 2026-11-12

The cold exception-policy initializer called `RtlImageNtHeader` before publishing its mode. If that call itself raises an exception, policy lookup could recursively enter the same initializer until the thread stack overflowed. `wine11-initializer-reentry.patch` adds a nonblocking initialization guard: nested or concurrent calls retain normal exception dispatch; only the initializer publishes the completed mode. It does not swallow faults or manufacture instruction results.

`probes/initializer-reentry.c` deliberately faults the authored test process's own Wine header function once, restores that function in its real exception handler, and verifies both the nested and outer handlers. The previous build overflows its stack; the patched build reports `header_faults=1 outer_faults=1 mode=1`, exit 0. All five existing CPU modes pass again with zero failures. Small original logs are in `diagnostics/initializer-reentry-20261112/`; `payload.zip` and its hash manifest include the repaired private ntdll.

**This is not the complete FH6 startup fix.** A later real game launch still overflows its stack. Its OptiScaler 0.9.4 log reports a failed `sl.dlss` detach (`10DD`) immediately followed by 5,856 repeated `slOnPluginLoad` resolutions. The plugin hook recursion is being treated separately. Do not infer successful gameplay, frame generation, online sessions or improved frame time from the initializer regression.


### Streamline hook / SDK architecture repair, 2026-11-12

The actual game log reports failed DLSS detach `10DD`, then thousands of recursive `slOnPluginLoad` resolutions. OptiScaler 0.9.4 unconditionally continues assigning and attaching the global original pointer after a failed detach. Upstream [4abfd7b](https://github.com/optiscaler/OptiScaler/commit/4abfd7b65d5eb002b44801421837c69e04edfd6b) replaces that lifecycle with per-instance original-function slots, an already-hooked check, and unloaded-module handling. The pinned official [2026-11-12 nightly](https://github.com/optiscaler/OptiScaler-nightly/releases/tag/nightly-20261112) includes it. This is a prerelease integration repair, not a claim of stable gameplay.

The retained SDK directory also contains an ARM64 Mach-O host bridge from the previous FEX route. Source inspection shows the old FG forwarding shim itself is x64 and does not load that host bridge, so merely finding the ARM64 file does not establish a crash cause. This Rosetta route now uses one pinned x64 FFX provider set; the proven startup defects are the recursive hook and actual Wine module search path. The selected providers are explicitly **FSR 3.1 SR and FSR 3.1 FG**, via the game's existing DLSS quality / 2x inputs. Nothing is relabeled as FSR 4. Native output resolution, audio, cache, saves, cloud and the native Steam entry are not changed.

`install_optiscaler.py ARCHIVE` verifies the official archive and DLL SHA256 and checks the current FH6 route; `--apply` installs only the graphics providers/config while FH6 is stopped. Download the archive from the URL in the script. The local receipt retains the prior small configuration and both DLL digests. It does not copy the game, replace shared engines, or change another AppID.

`probes/streamline-plugin-lifetime.c` is an authored two-plugin loader fixture, covering preloading, simultaneous copies, unloading and reloading. The pinned new module resolves all 2,005 hook calls in the test, and the driver returns success. The old module also exits successfully in this simplified fixture (only 1,203 hook calls), so **the fixture does not reproduce or prove fixed the game's recursive-detach failure**. That must be verified using the normal native-Steam launch and its new log.


### Private-prefix renderer and Chinese dialogs, 2026-11-12

The next actual startup no longer recursed but returned FHE21. Its loaded-image sample contained `libMoltenVK.dylib`, and Opti's device hook identified a vkd3d device: selecting D3DMetal in search paths had **not** displaced the native vkd3d/DXVK DLLs remaining in the cloned prefix's system32. `install_renderer.py --apply` copies the already installed Highball D3DMetal/Timestamp-shim x64 loader set into **only** this private prefix. `profile.py` verifies their hashes before launching, preventing silent renderer drift. The script does not modify or redistribute Apple's libraries.

The private prefix lacked an explicit dialog fallback that covered the ANSI-charset case, although this Mac already had a usable CJK font. `install_fonts.py` registers the Mac's existing Arial Unicode MS font through a local symlink and private Wine font substitutes. A real GDI glyph lookup for eight Chinese characters now reports `missing=0`. This solves glyph coverage, not UTF-8 decoding by itself: the existing UTF-8 ASI plugin is retained and its v10 plugin path is now absolute, avoiding resolution below the wrong new subfolder. GPU readback under the corrected loader set returns 8,192 pixels with zero errors. Small outputs are retained in `diagnostics/renderer-fonts-20261112/`.

Actual FH6 launch, stable rendering and 2x dispatch still need verification after this integration repair; a passed clear/readback or font probe is not a gameplay claim.


The final Wine search path is explicit in `WINEDLLPATH`. `WINEDLLPATH_PREPEND` alone is a frontend option and was insufficient for direct execution of this private Wine runner. Renderer directories now precede the generic engine DLL directory. Actual loading of `D3DMetal.framework` and a repeated 8,192-pixel zero-error readback were verified; MoltenVK can also be loaded for auxiliary calls, so simply finding its image in a process is not proof that the main D3D12 device uses it. The current game now identifies the configured virtual NVIDIA adapter instead of the previous vkd3d adapter and is presenting frames.

`wine11-utf8-dialog.patch` repairs the ANSI conversion APIs in private ntdll/kernelbase, explicitly enabled only by this profile's `WINE_UTF8_DIALOGS=1`. It only recognizes complete, valid UTF-8 containing CJK text. Normal ASCII, genuine GBK/1252, invalid UTF-8 and short-buffer error semantics are retained. It **does not change GetACP or the process-wide font charset**. A trial of changing the entire process ACP exposed a font-selection regression; that approach is not enabled in the final profile. The recovery archive now includes the paired kernelbase as a sixth public Wine module.

A font matrix exposed the remaining squares: common dialog faces with explicit ANSI charset selected fonts lacking Chinese glyphs, even when default-charset lookup succeeded. The font installer maps those private-prefix dialog faces to the existing `Arial Unicode MS`, which passed both charset cases. Existing controls retain their HFONT until recreated; no running game is killed to refresh fonts.

The current native-Steam launch passed the previous FHE21 point and presented native 3472x2266 output. FH302 is a separate driver-version identification warning with a continue option; no hardware-support check or driver capability was fabricated to remove it. Real free-roam stability and generated 2x output remain separate pending confirmations. No final FPS improvement is claimed from menu presents.


### Actual free-roam checkpoint, 2026-11-12

The user entered free roam in the same native-Steam process and described it as playable, with residual double images around the car. This is a startup/gameplay milestone, **not** a completed visual or performance repair. The log shows FFX SR initialization, successful FG dispatch and callbacks requesting one intermediate frame. The selected providers remain FSR 3.1 SR plus FSR 3.1 FG, at native 3472x2266 output. Success callbacks alone do not measure the displayed generated/base-frame ratio or prove correct image content.

Earlier preserved FG diagnostics show render/motion/depth 2480x1618 and MV scale 1240,-809, plus an initially empty centered interpolation rectangle. Those are historical inputs, not a measurement of the new provider's current parameters. Residual ghosting must be correlated with current per-frame dimensions, rect, camera/depth conventions, frame identity and resource lifetime before changing them. Keep the current running game; do not guess MV signs or label another upscaler a guaranteed cure.

The upstream Highball FH6 recipe confirms native MetalFX temporal SR through the DLSS menu, while saying native DLSS frame generation remains unavailable. Therefore the next preferred **candidate**, pending actual validation, is the existing Rosetta/D3DMetal route with native MetalFX Quality SR plus a correctly integrated 2x FSR 3.1 FG path. It is not proof that an older clean-looking run was already generating intermediate frames. The old 2026-10-08 M2 preset in the parent README is historical and is not this M5 Max's current profile.
