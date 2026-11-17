# The Crew Motorfest: preparation, not verified gameplay

The installed native Steam game is AppID **2698940**, using its own existing
Proton-ish/Highball r28 prefix. It does not share the NFS/EA prefix. Its first-run
log had only reached Steam's install-script evaluator; Ubisoft Connect was
absent. The bundled official `UbisoftConnectInstaller.exe /S` and
`Support/Software/VC_redist.x64.exe /install /quiet /norestart` both subsequently
returned zero. Ubisoft Connect's executable is now present.

`configure_launcher.py --apply` gives only `upc.exe` and `UplayWebCore.exe`
browser-local copies of the installed DXMT libraries. As with the verified EA
repair, it removes just the Wine builtin DOS marker from those private copies
so Wine does not redirect them to the game's renderer. Original engine libraries
remain untouched. The prefix registry backup and local hash receipt provide
rollback information; do not delete accounts or caches to undo this.

Native Steam LaunchOptions select D3DMetal for the DX12 game, with
`DXMT_ALLOW_CROSS_PROCESS_SWAPCHAIN=1` and `WINE_SIMULATE_WRITECOPY=1` for the
Ubisoft browser. This follows the installed Highball Ubisoft recipe's cross-
process browser requirement. No game executable or BattlEye module is patched.

The requested window → macOS green-button fullscreen → window behavior is still
pending the game's own settings being generated and checked. Do not fabricate
`PCScalability.xml` enum values or claim an untested startup argument works.
Ubisoft account sign-in succeeded in the November 16 evening run described below.
Actual game/anti-cheat compatibility remains unverified. A working launcher is
not proof of playable Motorfest.

The signed MetalSharp Wine 7.0 arm64 candidate has been downloaded into a separate
workspace test directory. Archive SHA-256:
`68f8acce238e09da334edb36301a954996bb1b9edbe2c1c6bc60fdf483df99a0`.
The first sandboxed codesign check failed because the authority could not be read;
an unrestricted read-only verification subsequently passed deep/strict checking,
showed Apple's trust chain and the stapled notarization ticket. The original app
was not modified or re-signed. Its own Wine 11.20 successfully ran a headless x64
probe in a new isolated prefix. The package includes linuxhost.dll and belauncher,
but official BattlEye bridge/game-session validation remains pending. Existing
Motorfest and other game prefixes were not migrated to this engine.

## Latest run: authentication succeeded, BattlEye pending

The 17:20 run logged successful Ubisoft authentication, a successful play-session
start for product 16733, and a connected game API. The previous login restriction
is therefore not the current stopping point. No additional login retry is needed.
The main executable handed off to `TheCrewMotorFest_BE.exe`; later
`BELauncherNew.exe` remained running without a verified game window. The runtime
also reported that a window could not be created because explorer failed to start;
this message alone does not establish why BattlEye stopped.

`service-status.cpp` queried the existing prefix without starting, stopping or
reconfiguring its service. The result was `state=1` (stopped), `win32_exit=0`,
`service_exit=0`, `pid=0`. A zero stored service status is not proof of successful
anti-cheat initialization. The first sandboxed probe timed out; the same read-only
probe outside the sandbox returned successfully. Do not attribute that probe
timeout to the game.

The upstream [MetalSharp Wine patch set](https://github.com/metalsharp/MetalSharp-Wine/blob/main/docs/PROVENANCE.md)
ports Valve's BattlEye launcher and runtime lookup. Its
[Linux module host at the inspected revision](https://github.com/metalsharp/MetalSharp-Wine/blob/b1ceb881dc6443da8772fbae3a906204266e21cc/README.md)
reports loading `beclient_x64.so`, running constructors and finding 17 bridge entry
points on Apple silicon under FEX. This is meaningful component evidence, not a
Motorfest gameplay certification. The installed Highball source lacks this
launcher/runtime path, and the game directory currently contains the Windows
`BEClient_x64.dll`, not a local Proton runtime. Nothing from that experimental
branch has been installed in this prefix. Its Linux host also documents incomplete
signals, shared mappings and TLS support, requiring compatibility review before
adoption. No game or anti-cheat binary has been patched or disabled.

### Isolated Linux-client dependency check

The next check found a concrete limitation beyond the bridge itself. See
[the local reproduction and results](diagnostics/2026-11-16-linux-client.md).
The upstream host's `hello` and `extras` checks both passed under the signed
MetalSharp Wine 7.0/FEX engine. Parsing the Lutris-distributed BattlEye runtime
resolved every mandatory import in its Wine bridge, but the actual Linux client
had **47 unresolved relocations**. This candidate is therefore not ready to
replace the live Motorfest runtime. No client constructors or anti-cheat checks
were executed, and no success-returning substitutes were installed.

This is the 2024-05-21 package still listed by Lutris's current runtime API;
it is not established to match Valve's current Steam depot. The anonymous
SteamCMD attempt for AppID 1161040 returned `No subscription`, so the
current official depot was not downloaded. Do not claim the older package's
failure proves every newer BattlEye build fails, or that a passing bridge test
establishes an authenticated game session.

The subsequent [isolated C ABI implementation](linuxhost-compat/README.md)
adds eleven ordinary string/character/fortification exports and reduces the
same original client's unresolved relocations from 47 to 36. Its Linux ELF
behavioral tests, five bounds-rejection processes, and upstream host regressions
passed. The client itself was only inspected, never executed; the candidate was
not installed into Motorfest. The remaining dependencies and protected game
session remain unresolved. Upstream's current 6.5/7.0 change notes explicitly
say BattlEye has not been tested with a game.

GTA V Story Mode is not a substitute test: [Rockstar's FAQ](https://support.rockstargames.com/articles/1nenwhZlVrJY6CTFeSS2Fx/grand-theft-auto-online-battleye-faq)
distinguishes Story Mode from BattlEye-protected Online. [Valve documents](https://partner.steamgames.com/doc/steamhardware/proton)
that BattlEye support requires per-title configuration.

## Login restriction observed, 2026-11-16

After the user submitted the login form, the actual Ubisoft Connect window
displayed **Access is temporarily restricted**, listing device/network activity
as possible causes. The launcher log had no completed authentication event and
its `user.vdat` file was absent. The inspected browser process arguments did not
include disabling JavaScript, automation, remote-debugging, or proxy switches.
These checks do not identify the service's blocking rule, prove an account ban,
or establish that every browser feature works.

No automated login retry, credential change, account/cache reset, user-agent
spoofing, or network-route change was performed. Further game startup validation
requires normal Ubisoft authentication to succeed. If the restriction persists,
use the page's feedback link or
[Ubisoft's account login support](https://www.ubisoft.com/en-ca/help/ubisoft-account/article/troubleshooting-ubisoft-account-login-issues/000063897).
Do not publish the block-page IP, request ID, login state files or credentials.

Ubisoft's [game listing](https://store.ubisoft.com/tr/the-crew-motorfest/63bc67e6d406ab22f1174306.html)
requires Ubisoft Connect and BattlEye. Reports of working Linux Proton do not
by themselves establish macOS Wine compatibility.
