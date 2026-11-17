# Motorfest private-prefix startup failures

Scope: native Steam AppID 2698940, existing Motorfest prefix, private signed
MetalSharp Wine 7.0 arm64 engine. Other game routes, graphics settings, accounts,
game files, audio settings and caches were not modified.

## Findings and corrections

1. Initial prefix migration stalled at a Wine Mono installer following failure
   of old Mono MSI maintenance. A clone finished `wineboot -u` in about 5.6 seconds
   with `mscoree=d` confined to that maintenance process. The normal launcher does
   not disable CLR. The migrated clone passed the existing real GPU/native Steam
   bridge check. `finish-prefix-migration.py` backs up the actual prefix and applies
   the same scoped operation, stopping the maintenance server afterward.
2. The new private runner had unconditionally set `SteamAppId`/`SteamGameId`.
   This incorrectly registered `iscriptevaluator.exe` as the game and produced
   `AppError_16` when Steam tried to start the real executable. Restored the old
   runner's distinction: only `waitforexitandrun` gets those values; `run` removes
   them. Actual native Steam launch subsequently completed the helper and created
   `TheCrewMotorfest.exe` without `AppError_16`.
3. The old Highball prefix retained
   `HKLM\Software\Microsoft\Wow64\x86` default `wow64cpu.dll`. A fresh prefix
   for this arm64/FEX engine selects `libwow64fex.dll`. The actual game child
   loaded `wow64cpu.dll` and immediately reported `stack overflow 624 bytes`.
   Two clones, differing only in that selector, reproduced the causal difference:
   the old selector failed with the same stack overflow; the FEX selector ran the
   self-written i386 probe successfully. See `wow64-selector-2026-11-17.json` for
   engine/probe hashes and results. `repair-wow64-selector.py` validates the
   matched proof, backs up the registry and changes only that one value.
4. The next actual launch loaded `libwow64fex.dll`, then the original 32-bit
   Ubisoft `upc.exe`. The original stack overflow disappeared. That exposed the
   incomplete 32-bit DLL migration: OpenGL forwarded symbols to missing
   `winemetalgl.dll`. In the corrected clone, the self-written OpenGL module probe
   failed with error 126 before `wineboot -u`; after updating, module loading and
   the forwarded `glDisable` address resolved successfully (exit 0). The same
   backed-up update was applied to the actual Motorfest prefix. Both 32-bit
   `opengl32.dll` and `winemetalgl.dll` now exist there.
5. After that update, real `upc.exe` and its `UplayWebCore.exe` children remained
   running without the stack overflow or missing OpenGL exports. Ubisoft's
   game-starter log reported `Failed to initialize Steam API` followed by
   `Steam info is not available`. The private entry had a working x64 native-Steam
   forwarder but retained the old i386 Windows client. A self-written i386 probe
   using Ubisoft's original `steam_api.dll` initialized and shut down the real
   native client after replacing that i386 DLL with named-export forwarding to
   the existing i386 `lsteamclient`. The control retaining the shared x64 bridge
   file also passed. Only Motorfest's i386 forwarder was replaced, with backups;
   the x64 forwarder hash is unchanged. See `steam32-forwarder-2026-11-17.json`.
   `build-steam-entry.py` now builds both architecture forwarders, and the probe
   uses the architecture-appropriate ActiveProcess client path.

Regression checks cover helper registration, other-game route isolation, exact
argument preservation, x86-only registry scope, selector idempotence and refusal
to overwrite unknown selectors. All seven tests pass.

## Remaining validation

The 10:49 native Steam run reached Ubisoft's ticket login and logged a successful
play-session creation for product 16733. The previous Steam-initialization
failures did not recur in that run. Wine subsequently loaded the builtin
`belauncher.exe`, and the original game's BE wrapper spawned the game executable.
This establishes a further startup handoff, not successful anti-cheat authentication.
Live samples of the BE wrapper show a process wait; the game sample shows active
exception/memory-protection work during startup rather than a rendered game.
One 60-second critical-section timeout was recorded in the run and has not yet
been causally tied to the game-start delay. Do not call it a repaired deadlock.
By the following process check, the game and BE-wrapper processes were gone;
Ubisoft background activity remained. No game menu was verified. The exit cause
has not been established, and the BE handoff alone is not evidence that BattlEye
caused the exit. Computer Use still timed out when binding the live Wine window.

These are startup compatibility repairs, not a claim that Motorfest is playable.
Ubisoft's full handoff, the original BattlEye client's authenticated session,
real game rendering and window/fullscreen transitions still need verification.
No game/anti-cheat binary or authentication response was modified.

Valve lists Motorfest as playable in Proton 10.0-3 on Linux:
https://github.com/ValveSoftware/Proton/wiki/Changelog
That is not a macOS compatibility guarantee. MetalSharp's published macOS port
includes BattlEye plumbing but explicitly reports no game test for BattlEye:
https://github.com/metalsharp/MetalSharp-Wine

Raw launch logs and full prefix backups stay local because they can contain
account identifiers. No raw account trace or proprietary runtime is committed.
