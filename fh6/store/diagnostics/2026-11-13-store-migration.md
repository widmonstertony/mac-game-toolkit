# FH6 Store migration / 商店版修复接续

Store build `3.483.970.0`, Highball `x64-crossover26.3-r28` / Wine 11,
Rosetta and D3DMetal. The user reached free roam and reported frequent stalls
and UI/car ghosting. No full game launch was performed after this change.
The native Steam non-Steam shortcut registration and its Xbox launch target
were kept; the launch after this round still needs an end-to-end check.

| Previous work | Store status and evidence |
| --- | --- |
| Rosetta exception chaining, resume and initializer reentry | Installed in the private Wine layer. The Steam and Store PE headers differ; enable only after matching the normally materialized Store executable's full SHA-256 and exact x64 headers. The owned exception probe ran with 393,202 handler calls and no failures. It does not prove the one-second stalls are gone. |
| D3DMetal, timestamp/shader shim, and NvAPI | Installed the validated five-file set with UDP below. Actual D3DMetal provider loaded; submit, fence and all 64 readback values matched. NvAPI initialization and two interface queries passed. |
| Dual-stack UDP | Installed. Mapped IPv4 datagrams passed both ways over a real local socket; invalid TTL and invalid sockets still failed. This does not prove Xbox matchmaking works. |
| Earlier WebSocket fix | The current Store runtime uses Nexus's HTTP/2 and GDK WinHTTP behavior. The Steam Wine patch does not apply cleanly to it. The installed Nexus WinHTTP and XGameRuntime hashes were unchanged; do not replace them with the older Steam DLL. |
| Chinese dialogs and audio | Existing UTF-8 dialog helper and CJK font retained; re-enabled the prior Wine dialog flag in the private launcher. The audio shim still uses the Highball built-in output; the 12-channel routing probe submitted 24,000 frames. |
| Resolution, SR and 2× FG | Kept Retina 3472×2266, FSR 3.1.6 Quality (1.5×), FSR 2× generation, and existing caches. The Store trace recorded a request for one generated frame per base frame. This path does not use FSR 4 or MetalFX. |
| Earlier history/UI composition candidate | Retained. Across 715 observed PREPARE samples, the supplied tags were depth, motion, SR input/output, with no HUDless image; 40 UI registration calls supplied no separate UI. The native UI composition gate could not run. A successful synthetic composition test cannot establish clean HUD or car output here. |
| Cloud, online and DLC | PlayFab auto-sync retained. The user entered the game after Retry; an authenticated cloud transfer was not independently confirmed. Join still reports `0x8060046B` and region `(0x0)`. No server-side join reason has been observed. |
| FEX, KosmicKrisp and Windows Steam bridge | Separate CPU, rendering or licensing routes. They are not merged into this Rosetta/D3DMetal Xbox launcher. No changes were made to the shared engine, game saves or other game profiles. |

Car Pass ownership was confirmed as active on this Xbox account. The public
Microsoft catalog classified the pass as a bundle with 26 listed durable
vehicle entries and returned no separate package for any of those entries.
That is the catalog returned for the current market, not a claim that every
vehicle in the advertised 30-car schedule is available yet. The installed base
manifest supplies no `RelatedProducts`; the current MACxus add-on discovery and
XStore entitlement implementation use those declarations for their related
queries. This identifies a gap to trace in the Store compatibility path, not
proof that Forza used that specific query. No package metadata or license data
was changed. In-game unclaimed cars can be redeemed via the official Car Pass
menu. Cross-save sync can show the cars on Steam, but unlocking Steam play
requires the matching Steam DLC license.

- [Forza DLC redemption and storefront rules](https://support.forza.net/hc/en-us/articles/51642642677139-How-to-Add-Your-DLC-Cars-to-Your-Garage-on-FH6)
- [Microsoft Store Car Pass catalog](https://www.xbox.com/en-US/games/store/forza-horizon-6-car-pass/9NTBFBZMB84T)

The code and probe entry point are in `../prior_fixes.py`. Build the private
Wine layer using the prior Rosetta Wine patches and
`../wine11-store-executable-guard.patch`, stage with
`prior_fixes.py WINE_BUILD LAYER`, validate with `--validate PROBE_DIR`, and
only then install with `--install` once FH6 has exited. The installed profile
receipt is `MACxus/external-highball/fh6-prior-fixes/prior-fixes.json`. A subsequent
`runner.mjs check` selected the layer and reported `priorFixes: true` without
running FH6. The installed Nexus DLLs were rehashed after the check and still
matched. No gameplay claim is made from those checks.
