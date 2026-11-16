# Steam retained a stopped FH6 shortcut, 2026-11-16

The user reported the native Steam shortcut still displaying Running after exit.
Read-only process inspection found neither FH6 nor its Node launcher, Xodus
service/CLI or prefix-wait helper alive. Steam's gameprocess log still retained
the launcher PID registered at 01:48, with no corresponding removal event.

The old native Steam client was alive, but its main Steam Helper child was a
zombie. Its webhelper log recorded `Stalled cross-thread pipe` at 09:39:41.
The computer-control interface also timed out. This establishes a stalled Steam
client with stale tracking rather than an actually running FH6 process. It does
not establish why Steam's helper originally exited or identify a Valve code fix.

A two-second client sample was retained privately. With the game confirmed
absent, the old client received SIGTERM; it remained alive after ten seconds.
Only that exact client PID was then force-terminated. The official native Steam
app was reopened and started a new client and healthy, non-zombie helper
processes at 09:43. No game launch was requested. Game files, shortcuts, account
data, saves, graphics settings and caches were unchanged.

The new client log starts a fresh session without registering FH6 as running.
Visual confirmation of the Start button was blocked by the computer-control
service's ScreenCaptureKit -3811 capture failure. Do not report a completed UI
check or a permanent fix to all Steam hangs. Do not change the already tested
FH6 prefix-wait logic to address this distinct client failure.

FH6's XeSS Quality / XeFG 2x configuration remains installed. Independent GPU
tests have passed, but live free-roam ghosting, pacing and stability acceptance
remain pending. The prepared status observer was not installed during this repair.
