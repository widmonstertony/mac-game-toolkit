# Microsoft Store FH6 / 微软商店版地平线 6

Status: **Store edition reached free roam; performance, UI ghosting, DLC
recognition, and online matchmaking still need verification.** The Steam
shortcut is a front end for the licensed Store game. It is not the Steam edition.

See [the Store migration record](diagnostics/2026-11-13-store-migration.md) for
the earlier fixes now installed for the next launch and the ones that do not
carry over.

## Downloaded-package profile (2026-11-13)

The owned Store package `3.483.970.0` is now present. The installer below verifies
its executable hash, the retained official OptiScaler/FSR modules, and the
GPU-validated generated-frame candidate before configuring the Store prefix:

```sh
python3 fh6/store/configure_game.py            # read-only plan
python3 fh6/store/configure_game.py --apply    # FH6 must be closed
python3 fh6/store/steam_shortcut.py            # read-only native Steam entry plan
python3 fh6/store/steam_shortcut.py --apply    # native Steam must be fully closed
```

This prepares a real **non-Steam** library entry, `Forza Horizon 6 (Xbox)`, which
runs the normal licensed Xodus package route through the native macOS Steam
client. It never starts Windows Steam or fabricates a Steam app manifest. Xbox
cloud saves use the official Nexus/PlayFab auto-sync proxy, not Steam Cloud;
Steam saves are not imported. Actual cloud download/upload and gameplay require
the user's normal signed-in game test.

The selected graphics route is existing Highball Wine 11 + Rosetta + D3DMetal,
native **3472×2266** output, FSR **3.1.6** with an explicit **1.5× Quality render
ratio**, and official **2×** frame generation plus the visibility/UI candidate.
The retained game's NGX input mode was 0, with a custom 1.4 ratio; the new
OptiScaler profile explicitly uses 1.5 for the non-native ratios instead of
mislabeling that old selection as a Quality preset. In-game DLSS text identifies
the input API, not a running NVIDIA DLSS implementation. FSR 4 and MetalFX are
not selected.

The public-interface observer, UTF-8 dialog helper and original providers live
in the Store prefix's `C:\\highball` directories, with no dependency on the
removed Steam installation. The existing universal Highball audio-buffer shim
is enabled only for this Store launch. The earlier Wine exception repair is
now ported to the distinct Store build, gated on the executable hash and PE
headers after normal Xodus materialization.
Existing graphics detail and audio selections are reused; no save, cache, host
audio setting or other game's prefix is rewritten.

An owned synthetic GPU contrast in the actual Store prefix passed official FSR
3.1.6 generation, native premultiplied UI and depth readback: zero reference/UI
pixel errors, and generated output differs from base. That is not proof that
live FH6 car ghosting or loading hangs have been eliminated. The user tests the
game from the new native Steam shortcut after registration.

中文：商店版配置使用原生分辨率＋FSR 3.1.6 质量比例超分＋2×插帧，保留现有
音频缓冲修复。Steam 仅提供非 Steam 游戏入口，微软授权和云存档继续走正式
Xodus/Nexus 链路，不启动 Windows Steam、不伪造安装、不覆盖存档。真实 GPU
对照通过；商店版实际画面、声音和云同步仍需进游戏确认。

This optional external runner uses the unmodified MACxus launcher for Microsoft
account sign-in, library access and official downloads. It reuses an existing
Highball Wine 11 engine, its D3DMetal renderer and already-installed shim.
MACxus's ASAR and signature are left byte-for-byte unchanged. No CrossOver
installation is claimed or manufactured. Other games and Steam are not edited.

## Prerequisites

- A Microsoft account that owns the Store game, or a valid applicable subscription.
- Official MACxus `v0.3.11-mac.12`, with original ASAR SHA-256
  `927cf9e8d91b293104ce621363f2141544cbd87b82ec66b201c519c41b36152d`.
- Its matching Nexus runtime `runtime-11.0-20261022-gf4adc821`, installed with
  the upstream runtime manager and verified with its published SHA-512 file.
- Existing Highball `x64-crossover26.3-r28`, D3DMetal and native ARM64 Node.js.
  The setup records and verifies actual engine/renderer hashes; it does not
  mislabel the engine as MACxus's different pinned Highball release.
- Enough space for the real Store package. Steam ownership/files are not
  substituted for a Store package or used to manufacture an installation.

## Setup

Install the official MACxus app from its release and sign in there yourself.
Install the matching Nexus runtime from Settings. Then, from this repository:

```sh
python3 fh6/store/setup.py --bootstrap
```

An existing **verified native x64 Microsoft VC++** system32 directory can be
specified with `--vc-source /absolute/system32`. Initialize the new prefix first.
The supplied four Microsoft DLLs must pass the existing PE/CHPE architecture
check; the setup changes native overrides only in the new Store prefix. A clean
machine should install the official VC++ redistributable instead of supplying
Wine's builtin or ARM64EC files.

The shared engine remains where Highball installed it; only the GDK-enabled
Store prefix and external helper files live under
`~/Library/Application Support/MACxus`. The helper extracts unmodified open-source
main/shared modules and bundled GDK resources from the verified official ASAR.
It never repacks that archive, re-signs the app or patches Apple binaries.

After downloading a real FH6 Store package in MACxus:

```sh
"$HOME/Library/Application Support/MACxus/Forza-Store.command" \
  "/absolute/path/to/installed/Store/package"
```

This invokes the normal Xodus package launch, executable from its manifest,
licence validation and temporary package view. It starts the upstream private
authenticated loopback bridge and enables the supported cloud-save runtime plus
PlayFab proxy. The Node event loop stays active while the game runs; a blocking
child wait would freeze this bridge. Credentials are handled by MACxus/Xodus;
the helper never reads or prints them. A native Steam non-Steam shortcut should
be added only after the actual package and working launch have been verified.

## Verified scope and limitations

On 2026-11-13 the environment returned Wine 11.0, the matching GDK runtime was
installed, and an owned D3D12 probe completed a GPU upload/default/readback copy
with a queue fence and 64 exact output words. The load trace includes the
existing D3DMetal `apd12.dll` provider. This verifies an environment and GPU path,
not FH6 launch, visual correctness, online play, cloud saves or performance.

The maintainer reports Store FH6 reaching the world/saving on M1 Max with
Highball/Rosetta/D3DMetal, but also periodic 150–300 ms freezes and a loading
crash. It is experimental, not a guaranteed cure for the Steam problems. The
local Store game has not been downloaded or tested. Existing Steam files are
retained until the user decides which installation to keep.

中文：商店环境已准备，复用真实 Highball/Wine 11 和 D3DMetal，不修改或重签
MACxus，也不影响 Steam。独立 GPU 队列及读回检查通过；这不等于商店版地平线
已经跑通。先在 MACxus 完成微软账号登录和正版下载，再验证游戏及云存档，之后
才能接入原生 Steam 快捷入口。不以下载或安装环境成功冒充游戏验证成功。

### Download and storage update, 2026-11-13

The user has now authorized downloading the owned Store edition. MACxus game
storage was selected as `~/Games/Windows/Xbox`. Its official PC/MSIXVC offer
reports a **159.9 GiB download**, with installed size unreported. At first
preflight the local volume had approximately **140 GiB free**. The user then
explicitly authorized removing Steam FH6 to make room. Only that game's
installation, library link and two manifests were removed; configuration, patch
modules, embedded graphics caches, prefixes and saves were retained. The private
official-FG dependency link points at the retained module.

The downloader transferred package metadata, then its real space check failed:
170,545,541,024 bytes required versus 140,953,026,560 available. The user separately
authorized deleting the two local Time Machine snapshots from 11:24 and 12:24.
Only these two snapshots were selected; external backups were not modified.
Available space rose to approximately 380 GiB after reclamation, allowing a download retry.
The retry passed the space check and was observed downloading/extracting at 1%
(962.4 MiB / 158.8 GiB, 8.97 MiB/s, application estimate about five hours).
Download completion and Store gameplay are not yet claimed.

`~/Games/Windows/Steam` is a symlink to the existing Steam `steamapps/common`
directory. This gives one management entry without copying or relocating game
data, changing launcher manifests, moving prefixes, or touching saves/caches.
It is not a claim that all game files have physically migrated to one directory.

中文：已获准下载商店版；统一入口为 `~/Games/Windows`，其中 `Steam` 指向
原有游戏库，`Xbox` 是新下载位置。官方 PC 包需下载 159.9 GiB，当前本机空间
不足。随后用户明确允许删除 Steam 版安装文件，并另行批准清理两份本机快照。
配置、补丁、存档和缓存保留；回收后可用空间约为 380 GiB，继续官方下载安装。
商店版尚未下载完成或进入游戏。

## Upstream provenance

- [MACxus release](https://github.com/fladnaG86/MACxus/releases/tag/v0.3.11-mac.12)
- [FH6 compatibility report](https://github.com/fladnaG86/MACxus/blob/main/docs/compatibility/games/forza-horizon-6.md)
- [MACxus discoveries](https://github.com/fladnaG86/MACxus/blob/main/docs/DISCOVERIES.md)

MACxus's JavaScript is MIT licensed by its contributors; its runtime DLLs have
their own LGPL notices/source in the upstream distribution. This repository
stores integration source and evidence, not game files, account material,
Microsoft redistributables or Apple graphics binaries.
