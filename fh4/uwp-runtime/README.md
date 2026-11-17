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

## Reproduction and verified scope

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

### Still incomplete

This remains a partial UWP implementation. CoreWindow input/window events,
CoreDispatcher async actions, splash-screen behavior, application suspension,
multi-view APIs, complete inspectable metadata and UWP package/activation
integration are not implemented. Unimplemented event subscriptions explicitly
fail instead of returning an uninitialized success token. Local-launch arguments
are only the empty-argument case; other activation kinds are unsupported.

The factory wrapper is currently invoked by the probe. It has not been connected
to the game's imported CreateDXGIFactory1/2 functions, and factory identities
obtained through adapter GetParent are not wrapped yet. Window input, DPI,
resize events, longer GPU operation and visible presentation need verification.
The game may encounter additional APIs.
MACxus's MSIXVC-oriented launch gate still needs a legitimate EAppx/UWP route.
The tests do **not** establish entitlement, Xbox authentication, cloud-save
access, menu entry, driving, multiplayer or performance. No game deployment
should be represented as validated on the strength of these tests alone.
