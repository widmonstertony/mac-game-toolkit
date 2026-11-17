# Experimental FH4 Store UWP runtime

**Not installed into FH4. Not verified to launch or play the game.** This is a
standalone component experiment for the owned Microsoft Store edition. The
Steam edition's working Wine patches do not supply its missing UWP lifecycle.
Game files, saves, account credentials and licensing are untouched.

## Origin

`coreapplication.c`, `corewindow.c`, `dispatcher.c`, and `private.h` derive from
[Rosentti/wine-uwp at 888416f1](https://github.com/Rosentti/wine-uwp/tree/888416f1f8c2c4aefab46d71b525b501bcbdb5f2/dlls/windows.applicationmodel).
Original copyright notices remain; the license is LGPL-2.1-or-later
([COPYING.LIB](COPYING.LIB)). Other files in this directory are new adaptation
and probe code under the same license. This does not incorporate a complete
Wine-UWP runtime or imply that upstream has run FH4.

The missing MinGW `ICoreWindowInterop` declaration uses the upstream IDL ABI.
The `ILaunchActivatedEventArgs` declaration follows
[Microsoft's Windows metadata projection](https://github.com/microsoft/windows-rs/blob/master/crates/libs/windows/src/Windows/ApplicationModel/Activation/mod.rs).
Lifecycle behavior is informed by Microsoft's
[IFrameworkView documentation](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.core.iframeworkview)
and [DirectX app model](https://learn.microsoft.com/en-us/windows/uwp/gaming/about-the-uwp-user-interface-and-directx).

## Implemented experiment

- Real CoreApplication/CoreWindow factories, rather than accepting a factory
  lookup while failing the ICoreApplication query.
- XmlLite parsing of the executable's own AppxManifest.xml: package name,
  display name, matching executable, application ID and entry point. DTDs are
  prohibited. No entitlement or package registration is synthesized.
- CreateView, Initialize, SetWindow, Load, local-launch Activated callbacks,
  Run, and Uninitialize. Failed HRESULTs propagate and references/TLS/window
  state are cleaned before a subsequent run.
- Activated subscription ownership, unique tokens, removal, callback snapshots
  and callback failure handling. Local launch exposes IActivatedEventArgs and
  ILaunchActivatedEventArgs, an empty argument string and the manifest app ID.
- Current-thread window lookup, consistent CoreWindow/interop COM identity,
  thread-affinity checks and the four CoreDispatcher ProcessEvents modes.
  An empty queue does not dispatch uninitialized MSG data; teardown does not
  leave a stale WM_QUIT for the next run. A retained dispatcher remains valid
  after its window is gone.
- Process-local activation context routing through Highball's actual
  RoGetActivationFactory. No persistent class registration or shared DLL
  replacement. Highball's RoRegisterActivationFactories is a success-returning
  stub, so that function is not used as evidence of registration.
- Normal-priority CoreDispatcher.RunAsync queues a retained delegate to the
  owner's hidden-window message pump. IAsyncAction/IAsyncInfo expose status,
  results, errors, cancellation, Close and a single completion-handler assignment.
  Completion handlers run outside internal locks, including late subscriptions.
  Shutdown drains pending actions without executing their work, and retained
  dispatchers complete newly submitted actions without invoking their delegates.
  This follows the documented [RunAsync shutdown behavior](https://learn.microsoft.com/en-us/uwp/api/windows.ui.core.coredispatcher.runasync).

## Reproduction and verified scope

### Owned package class registration, November 17

The earlier staged process manifest registered only CoreApplication/CoreWindow.
It omitted all **34 classes across four DLLs** declared by the owned FH4
AppxManifest. `package_classes.py` now translates those original in-process
registrations into the process activation context, retaining class names and
threading models and recording original DLL hashes. It rejects missing DLLs,
path escapes, duplicate/overriding classes and unsupported threading models.
No Microsoft DLL is replaced, no license is manufactured, and no global WinRT
registry is modified. The original declarations follow Microsoft's
[InProcessServer contract](https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/appxmanifestschema/element-inprocessserver).

The original owned DLLs were actually loaded in the isolated Wine prefix using
`package-class-probe.c`. With the old manifest, all 34 queries returned
`REGDB_E_CLASSNOTREG` (`80040154`). With the merged manifest, **32 factories
succeed**, including all 31 Xbox Services classes and Fanatec's factory. The
remaining two fail with `E_NOTIMPL`, with a concrete native trace:

- `GameControllerInfo.GameControllerInfo` calls
  `RegisterCustomFactoryForHardwareId(1103, 46675)`.
- `Microsoft.Xbox.Input.AdvancedGipWheel` calls
  `RegisterCustomFactoryForGipInterface(f546656f-e106-4c82-a90f-554012904b85)`.

Both functions are unimplemented in the private Highball engine's
`windows.gaming.input/manager.c`; the inspected Wine upstream source also
returns `E_NOTIMPL`. Returning empty success is not a repair: provider matching,
controller creation and callbacks still need an implementation. The runner
correctly exits nonzero while either owned factory fails. This is not proof
of the game's exact startup call order, Xbox sign-in, cloud saves or gameplay.
No factory instances were activated by the probe.

For reproduction, compile `package-class-probe.c` with the same llvm-mingw
compiler using `-municode -lruntimeobject -lole32 -luuid`, prepare a candidate
with `stage-profile.py`, then run `run-package-classes.py --help`. Its temporary
layout uses links to the four hash-verified owned DLLs beside the staged
manifest, matching eventual app-local deployment. An initial test without
those links found the class entries but returned module-not-found; this was a
probe layout error, not evidence of missing original DLLs. Results and negative
control hashes are in `validation-package-classes-2026-11-17.json`.

On November 17, the window-state adapter was corrected: hidden windows no longer
report `Visible=true`, and successful first mouse capture no longer reports a
failure. Win32 [SetCapture returns the previous owner](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcapture),
which is null when no window previously owned capture. The adapter now checks
the resulting owner and rejects capture from another thread. The hidden-window
probe covers null output, initial visibility, first/repeated capture, release,
and cross-thread rejection without stealing the owner thread's capture.
Combined lifecycle/dispatcher and D3DMetal GPU readback/resize pass with the
private package-runtime kernelbase. See `validation-window-2026-11-17.json`.
These are proven adapter defects, not proven causes of FH4's actual launch failure;
no game startup or visible presentation is claimed.

Build into a new output directory with llvm-mingw's x64 clang wrapper:

```sh
python3 build.py --cc /path/to/x86_64-w64-mingw32-clang --out /tmp/fh4-uwp-probe
python3 run-test.py --build /tmp/fh4-uwp-probe \
  --engine /path/to/highball-engine \
  --prefix /path/to/isolated/probe-prefix
```

The prefix must already exist and be dedicated to diagnostics. The probe creates
only its own hidden window. It does not drive any user interface or launch FH4.
The compiler symlink name must be preserved: llvm-mingw derives the target from
argv[0]. Build and result receipts include hashes of the tested artifacts.

On November 16, 2026, Highball x64-crossover26.3-r28 actually passed eight runs:
two complete lifecycles, five injected failures in CreateView/Initialize/
SetWindow/Load/Run, and one activation-callback failure. Assertions covered
window destruction, no stale quit message, view/handler/event-argument reference
counts, cross-thread lookup, dispatcher survival, entry-point/app-ID delivery,
real RoGetActivationFactory routing and launch-argument ABI. Details are in
`validation-2026-11-16.json`.

On November 17, the normal asynchronous dispatcher passed in the same combined
Highball/D3DMetal test: worker-to-owner execution, no premature callback,
Started/Completed/Error/Canceled results, early Close rejection, closed-object
errors, early/late single completion, callback failure, canceled-work suppression,
shutdown drain and reference balances. Both complete lifecycle iterations pass
these assertions; all eight lifecycle cases and 4,096-pixel GPU readback/resize
also pass. See `validation-dispatch-2026-11-17.json`. This closes an actual stub
in the candidate runtime, not a demonstrated FH4 gameplay failure. An initial
attempt using an ARM/FEX test-prefix clone failed loading kernel32 before the
probe began; the successful run uses an isolated Highball x64 test-prefix clone.

## Remaining work before a game trial

### Real GPU CoreWindow adapter, November 16

The next missing API is now directly reproduced and partially addressed. Using
the same test executable, actual Highball/D3DMetal device and queue, and hidden
CoreWindow, the unadapted `IDXGIFactory2::CreateSwapChainForCoreWindow` returns
`0x80004001` (`E_NOTIMPL`). D3DMetal's own log reports that method as unsupported.

`corewindow-swapchain.cpp` provides an experimental factory/swap-chain wrapper.
It retrieves the actual HWND through ICoreWindowInterop and uses D3DMetal's
existing HWND swap chain. The wrapper retains the original CoreWindow and its
factory, returns that window through GetCoreWindow, preserves COM identity and
forwards GPU methods. Newer DXGI interfaces are advertised only when supported
by the underlying renderer. Foreground-layer composition and exclusive fullscreen
are not claimed as supported by this route.

Through the wrapped factory's **standard** CreateSwapChainForCoreWindow method,
the same probe successfully creates a swap chain, checks its dimensions/window/
parent identity, clears an actual GPU back buffer, waits for a fence and reads
back all 4,096 pixels (RGBA8, one quantization-step tolerance). Present returns
S_OK and ResizeBuffers successfully changes 64×64 to 128×96. This verifies
GPU work and submission, **not visible display or FH4 gameplay**. The window
remains hidden. See `validation-gpu-2026-11-16.json` for the paired receipts.

The combined test also succeeds using Wine's automatic `lifecycle.exe.manifest`
loading, with no explicit CreateActCtx call or registry changes. This supplies
a tested startup routing mechanism for an executable without an embedded
manifest; integration with the owned game still needs separate verification.

Reproduce the failing native control and then the adapted case with one build:

```sh
python3 run-test.py --gpu --auto-manifest --build /tmp/fh4-uwp-probe \
  --engine /path/to/highball-engine --prefix /path/to/isolated/probe-prefix
python3 run-test.py --gpu-bridge --auto-manifest --build /tmp/fh4-uwp-probe \
  --engine /path/to/highball-engine --prefix /path/to/isolated/probe-prefix
```

The first command is expected to fail at the unimplemented native method.
Each mode saves a separate log and result receipt. Only the test executable's
temporary associated manifest is created, and it is removed afterward.

### Normal DXGI import entry and renderer initialization

`dxgi-entry.cpp`/`.def` now provide an app-local DXGI entry with the original
renderer loaded **unchanged** from `FH4_UWP_REAL_DXGI`. The owned FH4 executable
imports CreateDXGIFactory1 and CreateDXGIFactory2; both imports, and the legacy
CreateDXGIFactory export, passed through this entry in the real-GPU probe.
The probe no longer explicitly wraps its factory in this mode. The import
entry provides the wrapper, and the normal CoreWindow swap-chain call works.

An initialization-order failure was found during this integration. Loading the
proxy without first initializing the original DXGI module caused an execute
page fault during initial device setup. CoreApplication.Run now initializes the
original renderer outside DllMain and before invoking the app's framework
callbacks. It retains a single successful entry-module reference for process
lifetime rather than incrementing it on every Run. A negative control using the
same probe and DXGI DLLs with the prior UWP runtime reproduced the execute fault;
the corrected runtime passed all lifecycle and GPU checks. Wine's failing
backtrace contains only the invalid execute address, so this identifies the
tested ordering dependency, not a fully symbolized internal Apple call chain.

`validation-entry-2026-11-16.json` records artifact hashes, the negative control,
the imported-entry result and one-time initialization. Reproduce with:

```sh
python3 run-test.py --gpu-entry --auto-manifest --build /tmp/fh4-uwp-probe \
  --engine /path/to/highball-engine --prefix /path/to/isolated/probe-prefix
```

The runner temporarily supplies `dxgi.dll` **only beside the test executable**.
The real renderer path must be absolute; self-loading is rejected to avoid
recursion. The two private import thunks for D3D10 are forwarded but have not
been validated for D3D11 applications; this experiment targets FH4's DX12 route.

`stage-profile.py` verifies these positive test receipts and the actual FH4 Store
manifest before preparing three DLLs and an associated executable manifest in a
separate candidate directory. It refuses to stage inside the game, records
existing target hashes, and does not install, launch, register or change saves.
The candidate remains inactive and explicitly lists the unfinished work.

### Remaining game-integration gaps

This remains a partial UWP implementation. CoreWindow input/window events,
non-normal-priority/idle CoreDispatcher actions, splash-screen behavior, application suspension,
multi-view APIs, complete inspectable metadata and UWP package/activation
integration are not implemented. Unimplemented event subscriptions explicitly
fail instead of returning an uninitialized success token. Local-launch arguments
are only the empty-argument case; other activation kinds are unsupported.

Normal CreateDXGIFactory1/2 imports now reach the wrapper in the probe, but the
entry has not been installed into FH4. Factory identities obtained through
adapter GetParent are not wrapped yet. Window input, DPI,
resize events, longer GPU operation and visible presentation need verification.
The game may encounter additional APIs.
MACxus's MSIXVC-oriented launch gate still needs a legitimate EAppx/UWP route.
The tests do **not** establish entitlement, Xbox authentication, cloud-save
access, menu entry, driving, multiplayer or performance. No game deployment
should be represented as validated on the strength of these tests alone.
