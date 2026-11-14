# FH6 Xbox build 3.483.970.0 · Nexus and playback

The native Steam library entry still launches the licensed Xbox package through
Highball Wine 11, Rosetta, and D3DMetal. The next launch keeps 3472×2266,
FSR 3.1.6 Quality at a 1.5× render ratio, 2× generation, audio, existing
caches, and the Nexus/PlayFab cloud configuration. The Steam edition's last
acceptable playback is the comparison.

## What changed

- The FH6-only Nexus WinHTTP rebuild retains the MACxus GDK and HTTP/2 patches
  and adds the WebSocket receive/transport ownership changes used by
  MetalHorizon. In the same isolated Wine prefix, the old native WinHTTP
  module returned 12152 in all six concurrent echo workers; the new module
  passed all six with zero failures. The 155 ms configured HTTP timeout and
  real mapped IPv4 datagram round trip passed. A separate call to the public
  Microsoft product catalog returned HTTP 200 over HTTP/2 with the updated
  module and the original native nghttp2. The separate dual-stack UDP repair
  carried from the previous round remains in use.
- The Car Pass offers have no separate package and the base XML lists no
  related products. For this title and package identity, the new code supplies
  the catalog's Car Pass candidates if the manifest list is absent and
  implements durable-license acquisition using the existing authenticated
  Store receipt validator. Candidates do not grant ownership. The offline
  target-title probe got `0x800700cb` with no license handle; an unrelated
  title still got `E_NOTIMPL`. Signed-in positive discovery, the game's
  exact XStore call, and vehicle redemption need a real launch. The separate
  `XStoreQueryAddOnLicensesAsync` stub still returns an empty set.
- The first migration set preferences under a generic Wine title path while
  the Xbox edition used `XodusPersistentLocalStorage`. The actual Xbox XML
  had reset `DLSSGMode` to 0. The new setup locates that file, sets 2×,
  and matches the retained usable Steam presentation choices
  (`FrameRate=4`, `VSync=0`, and `AllowScreenTearing=1`). It keeps the same
  `PresentInterval`, graphics detail, scaling and resolution. OptiScaler
  file logging now uses warning level; the old debug file reached
  317,501,900 bytes. The prior title-gated Rosetta exception patch is still
  installed. No system-wide or D3DMetal frame cap was added.
- In the final 25 seconds of the old OptiScaler logs, the Xbox run had 19
  upscaler input frame intervals above 150 ms, spaced a median 1.30 seconds
  apart. The retained Steam run had 4 such intervals, clustered in its last
  seven seconds. The spans include different player activity and the end of
  each log; they isolate the repeated symptom without offering a benchmark.
  In the Xbox log the same long gap is already visible at upscaler entry,
  before the affected frame's FG dispatch. This does not rule out
  backpressure from a previous frame or identify the thread responsible.
- The old Xbox trace logged 1,242 successful sampled upscales and 1,123
  successful requests for one intermediate frame. Actual presentation and
  even pacing cannot be derived from a successful dispatch. In every one of
  1,124 sampled Xbox Prepare calls the two native HUDless/UI tags were null;
  the old Steam trace includes frames with both. The provider's existing
  UI/history correction requires the actual same-frame resources. The
  upscaled scene has a different format and precedes final composition, so
  using it as a replacement would be unverified. Whether the restored 2×
  selection makes the title submit both tags needs checking in the next run.
  In captured Steam replays the car duplicate also starts before UI composition,
  so restored HUD tags alone would not prove that the tail is clean.

MetalHorizon's maintainer reports successful Horizon Play with the title auth
chain and dual-stack UDP on their machine. Their log separates those findings
from WebSocket repair. For this profile, the game joining a real Xbox session
and a positive Xbox Car Pass license receipt remain to be verified. The
MacGameFix Rosetta/exception route still documents potential stutters and is
not evidence that the periodic hitch has been eliminated here.

- [MetalHorizon maintainer notes and real online test](https://github.com/alexandreunitypro/MetalHorizon-FH6-Mac-Launcher/blob/main/docs/PASSATION-IA.md)
- [MacGameFix 1.6.2 notes](https://macgamefix.app/blog/macgamefix-1-6-2)
- [Nexus Runtime public release notes](https://github.com/domsvoboda/nexus-runtime/releases)
- [Microsoft Store Car Pass](https://www.xbox.com/en-US/games/store/forza-horizon-6-car-pass/9NTBFBZMB84T)
- [Microsoft's durable content contract](https://learn.microsoft.com/en-us/xbox/gdk/docs/store/commerce/fundamentals/xstore-dwobs?view=gdk-2604)

## Build and deploy

Use the installed MACxus v0.3.11-mac.12 release manifest for
`xodus-nexus-runtime-11.0-20261022-gf4adc821`. The Wine source is
[b1dd32734](https://github.com/xodus-gaming/wine/tree/b1dd32734a34472a28eb5be9922df06e07ac0834),
XGameRuntime source is
[64aebcabb](https://github.com/xodus-gaming/xgameruntime/tree/64aebcabb8c66121eae25d3bf0ace4b582ebb0da),
and the nghttp2 headers are
[85e300c7](https://github.com/nghttp2/nghttp2/tree/85e300c79fb6dbcfa9c1013215c8710c1c2cd3d2).

Prepare Wine with the GDK overlay and manifest-listed patches in upstream
order. Apply the MACxus GDK option, HTTP/2, response and private-diagnostics
patches, including `wine-macos-gs-last-error.patch` from its runtime assets.
Then apply [the WebSocket patch](../../rosetta/wine11-websocket.patch) and
[the FH6 status patch](../wine-winhttp-fh6-status.patch) to WinHTTP, and
[the FH6 durable patch](../xgameruntime-fh6-car-pass.patch) to XGameRuntime,
with `patch -p1`.
From a source export, regenerate Wine's configure inputs, spec files and
winevulkan stubs before configuring. Use an x86_64 MinGW compiler, archiver and
headers, including nghttp2's public headers. Build Wine's x86_64 Windows
`dlls/winhttp` and `dlls/xgameruntime` targets. Use MACxus's existing native
DLL conversion in `crossover.js` when staging the PEs in an isolated probe
prefix. The test load trace must show `native` for each candidate.

Run the [network regression](../../arm64/network/README.md) and compile
[`nexus-probe.c`](../nexus-probe.c) with Wine's source and generated
XGameRuntime headers and `-lwinhttp`. Its `http2`, `durable`, and
`unrelated` modes need no account. Once those checks pass, use the tested
isolated prefix's `system32` directory:

```sh
python3 fh6/store/install_nexus_repairs.py /absolute/probe-prefix/drive_c/windows/system32
python3 fh6/store/install_nexus_repairs.py /absolute/probe-prefix/drive_c/windows/system32 --apply
python3 fh6/store/store_settings.py --apply
python3 fh6/store/check_session.py
```

Installation verifies the selected runtime and the loaded repair files against
the probe copies, checks the other Nexus modules by hash, and leaves the game
closed. To restore official Nexus DLLs, close the game, move the private
`external-highball/fh6-nexus-repairs` directory aside, and run
`node runner.mjs check` in `external-highball`. The official module set is
then reinstalled in the same bottle.

The next title launch writes HTTP responses/errors, WebSocket upgrades and
close/error codes for allowlisted services, plus title authentication and
licensing status. The two private status files are each bounded to 32 KiB.
They retain only fixed phase/service labels and numbers. The public catalog
probe verified the HTTP 200 status record; the six-echo regression also
confirmed that an unlisted loopback host leaves no record. `check_session.py`
extracts status counts and graphics summaries without printing identifiers,
URLs, headers, payloads, or resource addresses. This does not decode any
matchmaking response body or prove remote acceptance or displayed pixels.
