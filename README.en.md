# Sky China PC on macOS through Tencent YYB

An unofficial repair tool that installs and launches the NetEase China PC edition of *Sky: Children of the Light* through Tencent YYB's bundled Windows-game engine on verified M2, M4, and Intel Macs.

It preserves the required Fever Games login flow, enables true Retina backing, repairs per-process DPI awareness and pointer scaling, and applies 60 FPS/no-motion-blur/windowed preferences. The windowed setting keeps the native macOS close, minimize, and fullscreen titlebar controls available. The M2-tested native window fix also removes Fever's unusable hidden top inset while keeping the rendered surface, Wine hit testing, and drag coordinates synchronized; the window can be placed at the top edge and dragged normally without a position watchdog. Verified M2 and M4 systems support hash-pinned `winevulkan.dll` transforms for the original YYB 1.10.41 engine and the later YYB 1.2.3 (Build 690) and 1.2.4 (Build 713) engine packages. After an engine update, the installer also synchronizes the 64-bit engine, `system32`, and game-local copies so an obsolete game-local DLL cannot take precedence. All supported builds apply the same Vulkan interoperability fix: report the feature and desktop-GPU identity required by Sky's startup checks, then remove the unsupported geometry-shader request before `vkCreateDevice` reaches MoltenVK. The original DLL, every replaced instruction sequence, and the transformed result are verified before writing, and the local original is backed up. If a YYB self-update leaves macOS reporting `kLSNoExecutableErr`, the installer backs up and locally re-signs the invalid app and its external service before refreshing LaunchServices. No game, launcher, Wine, token, or copyrighted third-party binary is included.

See the [Chinese README](README.md) for the verified configuration, one-click instructions, limitations, safety notes, and troubleshooting.

Quick start: install and open [Tencent YYB for macOS](https://sj.qq.com/download) once (choose the Apple-silicon Mac build), download this repository, then double-click `install.command`. On M2/M4, open NetEase Fever normally and press **Start Game** for Sky. `launch.command` opens that verified path. After Sky has created its full preferences file, rerun `install.command` with the game closed to make windowed mode the default. On the verified M4 setup, the standalone Launchpad Sky icon is wrapped so it opens or focuses Fever instead of invoking YYB's broken child shortcut and returning `errCode:-1`; the original executable is backed up and restorable.

On M2/M4, the installer also compiles and locally signs an owner-scoped AppKit hook for Fever. It synchronizes Wine's cached frame through `setFrameAndWineFrame:` and bypasses AppKit menu-bar avoidance only for the real Fever `WineWindow`. The bundled PE helper is built from the included MIT-licensed source; Tencent's installed wineloader is never modified, and only an exact hash-checked private copy is patched for injection.

The binary patch is applied only when the chip is exactly Apple M2 or Apple M4 and the local DLL exactly matches the verified build. M2/M4 variants and unknown YYB engine builds are rejected rather than patched by fixed offset.

On Intel, the same `install.command` automatically uses Tencent's SHA-256-pinned legacy x86_64 engine, installs Windows Steam and Fever Games, enables true Retina rendering, and mirrors installed games into macOS Launchpad. It also installs a native **Windows Games** library app, with launcher/game cards, manual Launchpad sync, and one-click Retina/DPI/60 FPS re-application; its application structure is adapted from the MIT-licensed [Mac Wine Launcher](https://github.com/MrBurge2000/mac-wine-launcher). Native launcher hosts subscribe directly to YYB Wine window events, so games opened inside Steam or Fever acquire their own macOS App/Dock lifecycle without a resident process-polling daemon; file events update newly installed Launchpad entries. Because the legacy engine's Fever download IPC stalls on current macOS, an explicit Sky download click is resumed from NetEase's public official manifest/CDN with per-file MD5 verification. No proprietary binary is stored in this repository. MetalFX/frame generation is not available in this Intel engine.

This project is not affiliated with Tencent, NetEase, or thatgamecompany. It does not bypass authentication, anti-cheat, purchases, or server checks. Use your own legitimate account.

## Extra: Stellar Blade on Apple M4

If Stellar Blade is already installed through the Windows Steam client inside
YYB, run `python3 stellar_blade_fix.py`. This applies 3840x2160 output and
internal rendering, 2x Retina backing and native Gen4 TAA,
with a 120 FPS cap. Character textures, material quality and skeletal-mesh LOD
are prioritized while expensive environment settings are reduced. Used textures
stay resident and the artificial 8 GiB UE streaming-pool limit is removed. Depth
of field, motion blur, chromatic aberration, and film grain are force-disabled
for a clearer subject. Unlike the earlier profiles, neither Performance
upscaling nor FSR Native AA temporally reconstructs the character. Stellar
Blade forces frame interpolation off when FSR is disabled, so this
clarity-first profile does not claim frame generation.
This clarity-first profile is substantially more GPU-intensive. The script backs
up every changed file and does not alter the Sky Vulkan patch or save data.
