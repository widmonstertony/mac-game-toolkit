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
  exact XStore call, and vehicle redemption need a real launch. The follow-up
  also implements `XStoreQueryAddOnLicensesAsync` for this exact Store title.
  It reports only owned, unexpired, non-trial digital durables without a
  package, caps expiration at the signed receipt's end time, and retains the
  previous behavior for other titles. The isolated native DLL returned
  `0x800700cb` and zero results without the authenticated service; the same
  empty count remained available successfully for an unrelated title.
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
- A finer comparison used the raw log timestamp immediately after the
  original Present returned success and before the next upscaler-settings
  callback. The final 30 seconds of the Xbox log contain 23 such gaps
  above 120 ms (131–433 ms, median 223 ms, median spacing 1.296 s).
  The retained Steam log contains three. The period is consistent with
  the [Highball Rosetta report](https://gethighball.com/docs/rosetta-feedback/),
  but this trace cannot distinguish a stalled producer thread from GPU
  backpressure carried into the next frame. Successful return from Present
  alone does not mean the GPU has completed or displayed the frame.
  Reproduce this comparison locally with `python3 fh6/store/present_gaps.py
  /absolute/path/to/OptiScaler.log`. The tool reads a bounded log tail and
  reports timing statistics without exporting log lines.
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

The local Nexus implementation already requests both the default and the
authenticated title endpoint tables. Its default endpoints kept pointers into
a policy array that the title response could reallocate. The Xbox-only build
now copies each policy from its own document's table into the endpoint and
clears a partial endpoint table before a handshake retry. The parser
regression ran under Wine with the same Windows Web JSON module and checked
that default policy values survived 32 title-table appends, title policies
stayed distinct, and negative or fractional indexes were rejected. This
repairs a potential invalid signature policy read. Successful title
authentication and joining still require the signed-in game.

The pinned backend supplies sign-in tickets. The runtime itself uses SISU
and requests the authenticated current-title endpoint table, so MetalHorizon's
backend-specific title-auth implementation is unnecessary for this architecture.
The current public default table does not list the Forza service host. The
runtime's fallback used borrowed WinRT string headers after their C block
ended, including on the fallback path for an Xbox Live WebSocket. The fix
keeps both headers in the enclosing work block throughout authorization
and signature calculation. The rebuilt native x64 DLL retains the same nine
exports. In isolated Wine, the endpoint-policy parser and all four offline
Car Pass and unrelated-title checks passed. These checks do not reproduce a
signed-in fallback authorization or establish the cause of the join error.
[WinRT string reference lifetime](https://learn.microsoft.com/en-us/windows/win32/api/winstring/nf-winstring-windowscreatestringreference)

- [MetalHorizon maintainer notes and real online test](https://github.com/alexandreunitypro/MetalHorizon-FH6-Mac-Launcher/blob/main/docs/PASSATION-IA.md)
- [MacGameFix 1.6.2 notes](https://macgamefix.app/blog/macgamefix-1-6-2)
- [Nexus Runtime public release notes](https://github.com/domsvoboda/nexus-runtime/releases)
- [Microsoft Store Car Pass](https://www.xbox.com/en-US/games/store/forza-horizon-6-car-pass/9NTBFBZMB84T)
- [Microsoft's durable content contract](https://learn.microsoft.com/en-us/xbox/gdk/docs/store/commerce/fundamentals/xstore-dwobs?view=gdk-2604)
- [Microsoft's digital add-on enumeration contract](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/system/xstore/functions/xstorequeryaddonlicensesasync?view=gdk-2604)

The additional `--fh6-udp-controls` mode of
[the loopback probe](../../arm64/network/probe.c) exercised the deployed
Store Wine's matching `ws2_32.dll` in an isolated prefix. Both reset controls
returned success with zero output bytes on a bound dual-stack UDP socket.
The same socket set IPv4 fragmentation and TTL options, read back the
IPv6 hop limit, rejected invalid input, and exchanged real mapped IPv4
datagrams. This covers the socket setup sequence described by MetalHorizon.
It does not prove receipt of TTL-expiry ICMP notifications, remote QoS
responses, or successful Xbox matchmaking. A title-signed service request
and the actual join response are still needed to diagnose `0x8060046B`.

## Build and deploy

Use the installed MACxus v0.3.11-mac.12 release manifest for
`xodus-nexus-runtime-11.0-20261022-gf4adc821`. The Wine source is
[b1dd32734](https://github.com/xodus-gaming/wine/tree/b1dd32734a34472a28eb5be9922df06e07ac0834),
XGameRuntime source is
[64aebcabb](https://github.com/xodus-gaming/xgameruntime/tree/64aebcabb8c66121eae25d3bf0ace4b582ebb0da),
and the nghttp2 headers are
[85e300c7](https://github.com/nghttp2/nghttp2/tree/85e300c79fb6dbcfa9c1013215c8710c1c2cd3d2).

Prepare Wine with the GDK overlay and patches in the installed runtime
manifest's order. The [pinned Nexus launcher release](https://github.com/domsvoboda/nexus-launcher/releases/tag/runtime-11.0-20261022-gf4adc821)
contains the listed patches under `assets/`. Apply MACxus's
`xgameruntime-xstore-queries.patch` and `wine-macos-gs-last-error.patch`
from the matching MACxus runtime assets before applying the local patches.
Then apply [the WebSocket patch](../../rosetta/wine11-websocket.patch) and
[the FH6 status patch](../wine-winhttp-fh6-status.patch) to WinHTTP, and
[the FH6 durable patch](../xgameruntime-fh6-car-pass.patch) and
[the add-on enumeration patch](../xgameruntime-fh6-addon-enumeration.patch)
to XGameRuntime, one `git apply` per patch in that order. Apply
[the XUser policy lifetime patch](../xgameruntime-endpoint-policy-lifetime.patch)
and then [the fallback string lifetime patch](../xgameruntime-fh6-auth-string-lifetime.patch)
to the same XGameRuntime tree.
From a source export, regenerate Wine's configure inputs, spec files and
winevulkan stubs before configuring. Use an x86_64 MinGW compiler, archiver and
headers, including nghttp2's public headers. Build Wine's x86_64 Windows
`dlls/winhttp` and `dlls/xgameruntime` targets. Use MACxus's existing native
DLL conversion in `crossover.js` when staging the PEs in an isolated probe
prefix. The test load trace must show `native` for each candidate.

Run the [network regression](../../arm64/network/README.md) and compile
[`nexus-probe.c`](../nexus-probe.c) with Wine's source and generated
XGameRuntime headers, `-D__WINESRC__`, and `-lwinhttp -lole32`. Its `http2`, `durable`, and
`unrelated` modes and the `addons` and `unrelated-addons` checks need no
account. Once those checks pass, use the tested
isolated prefix's `system32` directory:

```sh
python3 fh6/store/install_nexus_repairs.py /absolute/probe-prefix/drive_c/windows/system32
python3 fh6/store/install_nexus_repairs.py /absolute/probe-prefix/drive_c/windows/system32 --apply
python3 fh6/store/store_settings.py --apply
python3 fh6/store/check_session.py
```

For the endpoint parser regression, apply
[`xgameruntime-endpoint-policy-probe.patch`](../xgameruntime-endpoint-policy-probe.patch)
only in an isolated test build. Run `nexus-probe.exe endpoint-policy` there with
the matching native `windows.web.dll`; expect `result=00000001`. Reverse the
probe patch and rebuild before installing. The deployed DLL keeps the upstream
export list.

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
The launcher also records the graphics log's byte offset before starting the
account service. The summary counts only subsequent records and explicitly
reports when there has been no launch with this diagnostic, preventing a
previous run's generation requests from being attributed to the current build.

## Measuring actual presentation

The optional `--present` mode of [the isolated GPU probe](../environment-probe.cpp)
uses the same private Highball/D3DMetal path and an owned 256×144 flip-model
swap chain. The 64-word GPU copy passed, all 12 Present calls succeeded, and
both DXGI frame-stat counters advanced from zero to 12. The accompanying
`SyncQPCTime` remained zero for every sample. This profile therefore did
not supply display timing through DXGI, even though the API returned success.
The counters alone cannot identify displayed frames or distinguish real from
generated images. The next in-game check needs a macOS presentation trace
alongside the existing FidelityFX dispatch log and visual check. Build the
probe with x64 MinGW and link `-ld3d12 -ldxgi -ldxguid`; the default invocation
remains the headless GPU copy. [DXGI frame-statistics contract](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ns-dxgi-dxgi_frame_statistics)

An eight-second Metal System Trace attached to an isolated Wine PID recorded
182 present requests and six controlled gaps of 235–244 ms. A second
calibration used an owned Metal window with a programmed 200 ms pause every
30 frames. It correlated all 223 requests with command submissions and GPU
completions. The seven long gaps were 234–236 ms. In each case the next
submission arrived 233–236 ms after the previous completion and completed
within 1.1 ms. This establishes that the analysis detects producer-side
pauses from the trace. The machine was locked, and the Metal window's
presented callbacks reported zero timestamps. Global WindowServer surface
swaps did not identify the Wine swap chain. The measured sample has no
drawable presentation rate. [Apple Metal performance analysis](https://developer.apple.com/documentation/xcode/analyzing-the-performance-of-your-metal-app/)

With the display unlocked, attach to the running title after entering
free roam. Use its macOS PID and run these commands in a private directory.
Keep the same process running for the straight and turn check, Horizon
Play join, and a known owned Car Pass vehicle.

```sh
xcrun xctrace record --template 'Metal System Trace' --attach "$pid" --time-limit 15s --no-prompt --output roam.trace
for schema in ca-client-present-request metal-application-command-buffer-submissions metal-command-buffer-completed ca-client-presented-handler
do
    xcrun xctrace export --input roam.trace --xpath "/trace-toc/run[@number='1']/data/table[@schema='$schema']" --output "$schema.xml"
done
python3 /absolute/path/to/fh6/store/metal_presents.py . --pid "$pid"
python3 /absolute/path/to/fh6/store/check_session.py
```

Compare the time from the previous completion to the next submission with
the next command's submission-to-completion duration. Inspect other queues
and producer waits over the same gap before assigning a cause. Actual
drawable pacing requires nonzero drawable presentation times. Identifying
intermediate images still requires the frame-generation records and visual
evidence. Keep the raw Metal trace private.
