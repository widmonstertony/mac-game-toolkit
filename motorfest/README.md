# The Crew Motorfest: compatibility work, gameplay not verified

## Installed candidate, November 17

The native Steam entry for AppID 2698940 now selects a private FEX/Mullion
runtime in the **existing Motorfest prefix**. This is installed for the next
user trial; authenticated/protected game startup and gameplay are still
unverified. Other AppIDs and data paths retain the byte-identical original
Highball runner body. The installation did not launch the game.

The missing browser integration was reproduced with a real D3D11 workload:
the old Ubisoft-local Highball DXMT DLL fails shared-texture creation with
`0x80004005` and a Mach-port registration error under the candidate engine.
The matching engine D3D11 successfully shares a BGRA texture across two
processes, transfers keyed-mutex ownership twice and reads back all 4,096 pixels.
Both controls retain the production cross-process/write-copy environment flags.
This establishes renderer-sharing compatibility, not a visible Ubisoft login.

The new profile archives those two browser-local DLLs, selects the matching
builtin renderer for the three Ubisoft executable names, installs the verified
native Steam bridge and registered original-client adapter, and retains game
files, account data, resolution/quality/audio settings and caches. It does not
substitute entitlement or anti-cheat responses. The original vendor Init path
and current protected game remain unverified.

An APFS clone of the entire previous prefix and the original shared runner are
stored under the Motorfest data directory's `.motorfest-private-20261117`.
The signed engine and payload were copied there so deleting workspace build
inputs does not remove the active runtime. `install-private-profile.py --rollback
--apply --prepared <original-prepared-directory>` stops only that prefix,
preserves the post-test prefix separately and restores the previous clone/runner;
it refuses to overwrite a subsequently changed shared runner.

The final entry also passed a four-second real child-lifetime check through the
native Steam shim (returned after about 8.9 seconds, child completion already
present). This is not yet evidence for Ubisoft/game handoff or game-exit behavior.
See [browser GPU results](linuxhost-compat/validation-browser-2026-11-17.json) and
[private-entry results](linuxhost-compat/validation-private-entry-2026-11-17.json).
Installation verification checked payload hashes, registry scope, shell syntax,
native entry resolution and unchanged routing for three other game IDs.

## Earlier preparation state

**Current state, November 17:** child-wait and synchronous-signal fixes pass in
an isolated clone of the existing container. The real legacy Linux bridge now
loads by DLL name and returns its original version; its authentication/Init path
has not been validated. The original native Steam shim now passes a real
SteamAPI initialization/shutdown test and a combined legacy-bridge/DX12 GPU
readback test. The native client path had to point to the directory containing
`steamclient.dylib`; completion is verified through the actual shim child.
These checks do not verify Ubisoft's candidate browser route, anti-cheat
authentication, or Motorfest gameplay. Computer Use still cannot bind Steam
because it requests capture of an off-display hidden window. The production
Highball runner and other games remained unchanged at that checkpoint. The
installed candidate section above supersedes this preparation-only status.

The [current findings and deployment status](diagnostics/2026-11-16-process-signals.md)
take precedence over the historical progress entries below. An inactive bundle
is preparation, not a declaration that Motorfest launches successfully.

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

The next isolated experiment supplies unmodified Valve Steam Runtime GNU C++
libraries through an explicit dependency resolver, and adds seven host math/
formatting exports. Actual owned ELF allocation, exception catching and destructor
execution now pass against those libraries. Their unresolved imports are zero;
the inspected client still has 16 unresolved relocations. See the
[C++ test evidence and limitations](linuxhost-compat/README.md#gnu-c-dependency-experiment-november-16).
This is not deployed to the production engine and does not establish a successful
BattlEye game session.

The subsequent UDP host addition passes actual IPv4/IPv6 loopback send/receive,
zero-length and connected-datagram tests, alongside the previous ABI and C++
regressions. The inspected client's unresolved relocations are now 14. This
still leaves runtime functionality to implement and an actual protected session
to verify; the existing game's engine has not been changed. Details and receipts
are in [the UDP experiment](linuxhost-compat/README.md#udp-additions-november-16).

The latest candidate integrates the pinned GNU libraries into the ordinary host
`dlopen` path, instead of relying on the inspection driver's resolver. An
independent executable loaded the actual candidate DLL and completed the owned
C++ constructor/exception/destructor test. Invalid dependencies and failed cached
loads were rejected before executing the test's constructor. Hostname lookup
translation and thread-local resolver state also pass; the inspected client has
13 unresolved relocations remaining. See the
[DLL-entry validation](linuxhost-compat/README.md#ordinary-dlopen-and-actual-dll-entry-november-16).
No game installation has been changed by these experiments.

Valve's [Proton changelog](https://github.com/ValveSoftware/Proton/wiki/Changelog)
lists Motorfest as playable beginning with Proton 10.0-3. That confirms a Linux
route exists; it does not certify the incomplete macOS Linux-host port. The
[upstream Motorfest issue](https://github.com/ValveSoftware/Proton/issues/9119)
also records runtime/graphics-specific failures, so successful Ubisoft login
alone is not a sufficient completion criterion.

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


## Latest isolated candidate, November 16: five unresolved references

Formatted I/O, local-time conversion, timer waiting and mount-table parsing now
pass actual Linux ELF callers along with the existing networking, GNU C++ and
real-DLL regressions. The same inspection-only client is down to five unresolved
relocations across four names: `system`, `sched_getaffinity`, `longjmp` and
`sched_setaffinity`. The [mount validation record](linuxhost-compat/validation-mount-2026-11-16.json)
identifies the exact build and test scope. The candidate has not replaced the
installed engine, and no protected game session has been validated. Symbol
resolution alone is insufficient: process, signal and scheduler semantics still
need review against actual runtime behavior.


The following nonlocal-jump candidate passes actual DLL/ELF register, stack,
pointer-guard and signed-return tests on three threads. Its inspection-only
client now has four unresolved relocations across three names. **Saved signal
masks, signal delivery and other documented runtime semantics remain unsupported**;
this is not a claim that completing those three symbols makes the game playable.
See [jump validation and limits](linuxhost-compat/README.md#nonlocal-jump-abi-november-16).
Nothing has been installed in a game container.
