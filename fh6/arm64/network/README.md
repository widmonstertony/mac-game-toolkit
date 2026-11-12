# FH6 ARM64 network fixes / 地平线6联网修复

This is a **Forza Horizon 6-only** module overlay for the existing Highball
`arm64-kk-wine11.18-r4` bottle and native Steam bridge in this repository.
It does not replace the shared engine or install MetalHorizon.

## Confirmed gaps and changes

The existing local Winsock overlay already supported `SIO_UDP_NETRESET`.
That patch is retained. The matching Wine 11.18 + Highball source and the
local executable regression exposed two further gaps:

1. A WebSocket shared its upgraded connection with the old HTTP request.
   Draining that HTTP response could return the live WebSocket to the HTTP
   keep-alive pool. Six concurrent upgrade / HTTP / delayed-echo tests failed
   on the old module. The port transfers ownership to the WebSocket, retains
   partial TLS records during liveness probes, and waits rather than spinning
   when a supposedly blocking receive reports would-block. The existing
   receive timeout remains in effect.
2. `IP_DONTFRAGMENT` and `IP_TTL` on an IPv6 dual-stack UDP socket both returned
   WSAEINVAL (10022) locally. The port retries the corresponding IPv6 options
   only for a dual-stack socket and only for host compatibility errors.
   It also covers the corresponding multicast TTL/loop options. Invalid
   values, invalid sockets and a failed mapped operation still fail. In
   particular, the port does **not** copy upstream's unconditional success
   when a mapped operation fails, or its no-op success for IP_TOS.

These come from [MetalHorizon](https://github.com/alexandreunitypro/MetalHorizon-FH6-Mac-Launcher)
at commit `37158bb6f55e26554955fb446221687959d1fa56`:

- [WebSocket patch](https://github.com/alexandreunitypro/MetalHorizon-FH6-Mac-Launcher/blob/37158bb6f55e26554955fb446221687959d1fa56/patches/wine/0002-winhttp-websocket-owns-connection-blocking-recv.patch)
- [Dual-stack UDP patch](https://github.com/alexandreunitypro/MetalHorizon-FH6-Mac-Launcher/blob/37158bb6f55e26554955fb446221687959d1fa56/patches/wine/0006-ws2_32-dual-stack-ipv4-options.patch)

Upstream's CrossOver source puts the upgrade in `request.c`; Wine 11.18 puts
it in `socket.c`. The checked-in patches are ports against the actual source,
not fuzzy application of an incompatible patch. They do not include Xbox
edition authentication changes, credential handling, executable patches,
GPU capability changes or frame-rate settings.

## Install / 安装

退出地平线6；原生 Steam 可以保持运行。从仓库根目录执行：

```sh
python3 fh6/arm64/network/install.py
python3 fh6/arm64/network/install.py --apply
```

安装器核对共享 Wine 模块、两份网络 DLL 和压缩包的 SHA-256，拒绝未知版本。
它将 DLL 放在地平线容器的 `.fh6-network`，只在地平线的原生 Steam
启动桥中优先加载。现有图形、音频、Wine DLL 路径作为后续搜索路径保留。
容器设置、画质 XML、OptiScaler 设置、渲染模块和音频模块在写入前后核对摘要。
不更换原生 Steam 启动选项，不启动游戏，不关闭超分或 2× 插帧，不清空缓存。

下一轮直接从原生 Steam 启动地平线，在自由漫游重试加入 Horizon Play。
**本机回环回归通过不代表微软服务器上的实际加入会话已成功，也不证明
`0x8060046B` 只有这两个原因。最终联机、实际帧率和重影由实机游戏验证。**

停用时退出游戏，然后执行：

```sh
python3 fh6/arm64/network/install.py --disable --apply
```

它保留文件，仅停用下一轮的网络覆盖层。原模块始终留在原位置，
不需要还原整个容器或其他游戏。启动桥安装前版本保存在本机
`protonish/launchers/2483190/network-install-records/`，不包含账户配置或存档。

## Build and reproduce

Only the two PE network modules are rebuilt. They use the current engine's
unmodified Unix-side modules, including its existing TLS provider; configuring
the build without GnuTLS does not replace or disable that provider.

Inputs:

- Wine 11.18 source tarball, SHA-256
  `c6282f6d4daecf11f3ab9f9a2b6c598f67be7e28c363ba2cae8a3c44b611e7a4`.
- Highball engine patches `8b315e7`, source archive SHA-256
  `1718492b3891c938a4236f467797a20cfbcf364c3c41661c12b359a26f2b9fc8`.
- This repository's existing `src/wine-11.18-udp-netreset.patch`, then the two
  `network/wine-11.18-*.patch` files, applied with zero fuzz.
- LLVM MinGW 20261006 universal macOS toolchain; its official archive SHA-256 is
  `a7a19b11048e89372637d26279cdb7c430ed3cf8c1e224310421312bf986ebf6`.
  macOS CLT and Bison 3.8.2 were used locally.

```sh
LLVM_MINGW_BIN=/path/to/llvm-mingw/bin \
NETWORK_BUILD_DIR=/path/to/empty-build \
BISON_BIN=/path/to/bison/bin bash fh6/arm64/network/build.sh
```

`payload.zip` contains only the rebuilt Wine `winhttp.dll` and `ws2_32.dll`,
including ARM64EC support. Both DLL hashes and the archive hash are pinned in
`install.py`. Wine source and patches retain **LGPL-2.1-or-later**; see
[COPYING.LIB](../src/COPYING.LIB). The installer and loopback probe use this
repository's MIT license. No proprietary game or engine binary is included.

The loopback harness uses the actual FH6 runtime without starting the game or
Windows Steam. It contacts only 127.0.0.1 and IPv4-mapped loopback addresses:

```sh
python3 -m unittest discover -s fh6/arm64/network -p 'test_*.py'
python3 fh6/arm64/network/verify.py --probe /path/to/network-probe.exe \
  --output /path/to/network-check.log
```

Before deployment, `--overlay /path/to/network-overlay` selects staged DLLs
for that probe process alone. `--trace` enables detailed logs only for the
local regression; it is not enabled for normal game play. `validation.json`
records the sanitized comparison, not account data or raw game traffic.

2026-11-12 local result: the old modules had nine failing checks; the staged
modules passed. One first installed run exceeded the 15-second six-worker
join with one echo not yet reported. Its cause was not established. A
stage-instrumented run and eight subsequent installed-route runs all passed,
with all workers reaching cleanup completion. This anomaly is retained in
`validation.json`; repeat passes do not prove that it has been fixed.
