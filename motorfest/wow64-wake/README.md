# Motorfest-only i386 lost-wake recovery

The native Steam profile selects an isolated engine copy for the **next** launch.
The currently running game remains on its existing engine. Its scene entry was
confirmed by the user; gameplay with this synchronization candidate is not yet
verified. This is a recovery for an observed lost notification, not proof of the
underlying FEX translator fault or a universal fix for game crashes.

## Evidence and source

In an isolated 32-bit probe, the Motorfest engine's critical section sometimes
stalled for approximately five seconds with no owner, semaphore value one, and
an empty address-wait queue. Moving only the critical-section algorithm into the
probe while retaining RtlWaitOnAddress still reproduced the stall. Changing
msync or strengthening TSO did not eliminate it.

[DroidDeck's upstream patch](https://github.com/Droid-Deck/DroidDeck-Proton/blob/main/patches/wine-hotfixes/pending/ntdll-i386-recover-lost-wake-on-address.patch)
by maxjivi05 describes the same dequeued-waiter/undelivered-alert state under FEX
with 32-bit CEF applications. The adapted patch uses bounded i386 wait slices
and checks whether the waiter was actually dequeued before treating a lost alert
as a wake. It does not grant an owned critical section to another thread: the
caller must still atomically acquire its semaphore. It preserves the caller's
absolute/relative deadline while the waiter remains queued.

Cleanup also takes the queue lock before examining/removing the stack entry,
following [Wine MR12309](https://gitlab.winehq.org/wine/wine/-/merge_requests/12309),
to synchronize with a wake-all still accessing that entry. The recovery helper
is i386-only; this build replaces no native/ARM64EC synchronization code.

The source is MetalSharp-Wine v7.0's exact Wine pin
`dd1f6c2f9801327d30fd82f2d55071177824fb99`. Rebuilding its unchanged i386 ntdll
reproduced two five-second stalls in four runs. The patched build completed
all eight runs without a stall, 200,000 critical-section entries per run in
45–83 ms. These are liveness tests, not a game FPS benchmark. All runs preserved
the final protected counter value. The independent wait-contract test passed:
1/2/4/8-byte comparisons, immediate return for unequal values, relative and
absolute timeout, invalid size rejection, infinite wait with single wake and
broadcast wake. All 1,509 exported names and ordinals match the installed DLL.
See `validation-2026-11-17.json`.

## Rebuild and deployment

Download the exact pinned source archive (hash in the receipt), configure with
llvm-mingw and `--enable-archs=arm64ec,aarch64,i386`, then build only
`dlls/ntdll/i386-windows/ntdll.dll`. Save the unchanged build for the negative
control before applying `lost-wake.patch`; rebuild the same target. The isolated
build omitted optional GUI/media development dependencies because no native or
other Wine module is installed from it.

`install.py --dll PATH` checks the candidate hash and validation gates and prints
a plan. `--apply` APFS-clones Motorfest's private engine, replaces just its i386
ntdll, restores the original file mode, verifies all other regular-file hashes
and symlinks, backs up the profile, and atomically selects the clone. Installation never
stops a running process or changes other AppIDs. On the next actual game launch,
a one-time handoff runs only after acquiring the existing exclusive launch lock:
it stops/waits for the old Motorfest-prefix wineserver so a persistent Ubisoft
background process cannot retain the old DLL, then clears the pending marker.
Install-script helpers and path queries never trigger that handoff. `--resume` can finish a partial
stage only if its candidate DLL already has the verified hash.

The original engine is retained. Rollback means restoring the backed-up
`profile.json` for a later launch; do not replace files loaded by a running game.
No game data, licenses, account state, saves, quality/audio settings or graphics
libraries are changed. Native Steam remains the launch entry. A new launch is
still needed to verify the startup stall in Ubisoft itself; the current game
should not be restarted merely to demonstrate this component result.
