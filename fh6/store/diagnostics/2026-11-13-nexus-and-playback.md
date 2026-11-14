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
free roam. Use its macOS PID and a new private output directory.
Keep the same process running for the straight and turn check, Horizon
Play join, and a known owned Car Pass vehicle.

```sh
python3 fh6/store/metal_presents.py /private/tmp/fh6-roam-trace --pid "$pid" --record
python3 fh6/store/check_session.py
```

The summary includes all title submissions and the union of the title's
recorded active GPU intervals across channels in each long gap. Where the
trace supplies data, the same interval includes drawable waits and CPU
samples for the next submitter thread. Compare the time from the previous
completion to the next submission with the next command's submission-to-
completion duration. A missing table or empty thread sample cannot identify
a wait owner. Address-only/private stacks remain in the local trace. Actual
drawable pacing requires nonzero drawable presentation times. Identifying
intermediate images still requires the frame-generation records and visual
evidence. Keep the raw Metal trace private.
# November 14 follow-up — Xbox interpolation color capture

The retained Xbox run had 1,124 successful frame-generation Prepare calls
with frame-matched original motion and depth textures. All 1,124 omitted
separate HUDless and UI textures in the original tags. The FFX configure
descriptor also left its HUDless texture null. The existing seven-image
capture therefore could not establish which stage first shows the duplicate.

The Store-only candidate now records the actual color input and the official
provider's generated output when both UI textures are absent. It uses only
the original synchronous direct-queue interpolation command list and waits
on its GPU fence. The normal provider descriptors and interpolation remain in
use. The marker is armed after reaching free roam, and only two eligible
frames are copied. Keep those frames out of frame-time comparisons.

The private renderer and the original FFX 3.1.6 provider passed both GPU
regressions on Apple M5 Max. The no-UI run captured two fenced RGBA8 input/output
pairs and the candidate's generated pixels matched the unwrapped
provider byte for byte over 12 synthetic frames. The tagged-UI run captured
two seven-image sets and the original native UI comparison had zero out-of-
tolerance pixels. These synthetic observations do not locate the live
ghosting or establish that the generated frame reached the screen.

A separate end-to-end run of the Metal recording command attached to the
owned calibration window. It correlated all 150 present requests to a
submission and completion. Five injected pauses had at least 233 ms between
the prior completion and the next submission, and at most 0.887 ms of
recorded GPU activity from that PID over each gap. The locked display still
yielded no drawable presentation timestamps. This calibrates collection and
producer-side gap detection; the Xbox game has not yet run with this capture.

Reproduce on a disposable Wine prefix with the Store D3D12, D3D12Core and
DXGI DLLs. Use the build directory made by `fh6/rosetta/fg-chain/build.py`.

```sh
python3 fh6/rosetta/fg-chain/run_native_ui_probe.py --artifacts "$build" --history --capture --store-test-prefix "$probe_prefix" --missing-ui
python3 fh6/rosetta/fg-chain/run_native_ui_probe.py --artifacts "$build" --history --capture --store-test-prefix "$probe_prefix"
python3 fh6/store/install_fg_capture.py "$build"
```

Applying requires FH6 to be closed and both receipts for the same build.
On this machine the previous provider SHA-256 was
`4b3b9a6ff9b21120ac17452f06b065d8bf245743a12224a68942a8bf4ab9e506`.
The verified candidate SHA-256 is
`9cdc49afcaf1e98bc25b363ef6faf908bf888d1507ba3524f41b02248d35cb0d`.
The Store runner validates the provider digest alongside the other components.
If restoring the original wrapper, restore the retained matching provider and
its profile digest together while FH6 is closed, then run `runner.mjs check`.

For the next normal launch from native Steam, verify the Car Pass vehicle
and retry online matchmaking with the same game process. Capture frame timing
in stable free roam before arming the color capture; compare the paired images
along with the previously installed Metal completion observation. Keep live
game pixels, service identifiers and authorization data out of the repository.

### Composited-color replay on the private Xbox renderer

The old Xbox trace explicitly set both Streamline tags to null in all 1,124
sampled Prepare calls. Forty sampled FFX UI registration calls also supplied
a null texture. The saved Steam pair 14902–14903 previously demonstrated a
car-tail disocclusion error before the UI was applied. To separate it from
the missing tags, the unchanged original FFX 3.1.6 DLL replayed that pair and
two others on the private Store D3DMetal path. Each pair ran both the original
scene-only control and `composited-input`, with identical motion, depth,
camera data, four warmup frames and native 3472×2266 output.

All six GPU replays completed. For 14902–14903, comparing the two variants
on that pair, 7,221 of 14,325 sampled pixels with UI alpha above one differed
by more than 3 RGB levels. In the lower-middle comparison box with
X=1215–2255 and Y=1586–2038, only 15 of 1,886,292 RGBA components changed.
This control indicates
that providing the original separate UI can address a different problem from
car-tail disocclusion. The Steam inputs and cold replay do not establish the
Xbox defect and do not justify fabricating HUDless color from the earlier
upscale result. Use the next live Xbox capture to establish whether the Xbox
artifact already exists at the provider output.

### Matched Prepare inputs for the next Xbox capture

All 1,124 old Xbox Prepare records use the exact original tagged depth and
motion pointers in pixel/compute-read state. The installed OptiScaler revision
records Prepare and submits it to the game queue. The capture therefore copies
depth and motion plane zero in that Prepare command list and restores their
states immediately. It collects the paired original and generated colors in
the matching interpolation command list. This avoids reading expired tag
resources from the later callback. A reset, unknown queue, incompatible
format or duplicate Prepare can prevent capture. Neither the interpolation
algorithm nor ordinary gameplay frames use this readback path unless the
two-frame request is armed. [Pinned OptiScaler code](https://github.com/optiscaler/OptiScaler/blob/2fff542e78e257a1e4ff5a1e21a69a166c78c335/OptiScaler/framegen/ffx/FSRFG_Dx12.cpp)

The isolated Store GPU checks used separate Prepare and interpolation
submissions, the actual typeless depth format, and FFX 3.1.6. Both four-image
captures had byte-exact expected depth and motion data at every sampled
pixel (24,576 depth and 24,576 motion pixels in total). The official
interpolation comparison and the seven-image native-UI regression both passed.
The four images from each adjacent frame replayed through the original
provider; the Wine module log confirmed the Store D3DMetal shim's `apd12.dll`.

The new verified Store wrapper SHA-256 is
`eb7fd784ee4f40e16293b72f9c1233555a8cc9c5940821c2ff8d8286a1a56713`.
The same installation command above requires the four-image and seven-image
receipts for one build. The previously installed two-image diagnostic is
recoverable from commit `8f290e4` and the profile digest recorded above. Run
`prepare_replay.py` on two adjacent four-image captures and pass
`composited-input` to `replay-capture.exe`. These GPU checks exercise the
capture and replay path; determining and fixing the Xbox artifact requires
the user's in-game frames.
