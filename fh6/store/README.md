# Microsoft Store FH6 / 微软商店版地平线 6

Status: **environment prepared; Store gameplay is not verified on this Mac**.
This does not replace or fix the Steam game's rendering/loading by itself.

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
才能接入原生 Steam 快捷入口。不会下载第二份游戏或删除 Steam 游戏来冒充验证。

## Upstream provenance

- [MACxus release](https://github.com/fladnaG86/MACxus/releases/tag/v0.3.11-mac.12)
- [FH6 compatibility report](https://github.com/fladnaG86/MACxus/blob/main/docs/compatibility/games/forza-horizon-6.md)
- [MACxus discoveries](https://github.com/fladnaG86/MACxus/blob/main/docs/DISCOVERIES.md)

MACxus's JavaScript is MIT licensed by its contributors; its runtime DLLs have
their own LGPL notices/source in the upstream distribution. This repository
stores integration source and evidence, not game files, account material,
Microsoft redistributables or Apple graphics binaries.
