# Motorfest IDXGIFactory7 startup repair

The original 2026-11-17 native Steam launch queried Factory7, got
`E_NOINTERFACE` (`80004002`), and immediately faulted reading address zero in
`TheCrewMotorfest.exe+2ded48a`. The exception registers retained `rax=80004002`,
`rcx=0`; the game's crash reporter then started. Mullion's flight record agrees.
This is a specific startup graphics fault, not evidence of BattlEye rejecting
an authenticated session.

The installed renderer identifies itself as Mullion `39d1e4a4`. Build from
`39d1e4a40121ddfe4f9fc3793bfc336f4491d5b5` in
https://github.com/aaf2tbz/mullion, apply `factory7.patch`, then run `build.py`
with source/output/toolchain/engine arguments. The toolchain directory is the
LLVM-MinGW `bin` directory; the engine is the signed app's `Contents` directory.
Only DXGI is built. Existing winemetal exports supply the import library;
D3D12, shaders, the signed native bridge, and other games are unchanged.

Factory7 shares COM identity with Factory0–6 and inherits the existing rendering
methods. Adapter-change registrations retain event handles, return distinct
cookies, and observe sorted Metal registry IDs. The watcher polls only while
there are subscribers and exits when the factory is released. `IsCurrent`
checks the original topology. No event is signalled as a substitute for work.
Physical GPU hot-plug has not been tested on this Apple Silicon host.

## Validation

The exact pinned source without the patch failed Factory7 with `80004002`.
With the patch, the ARM64EC build passed the same query, COM identity,
registration/unregistration and factory destruction checks. An actual D3D12
queue copied 4096 bytes, completed fence value 1, and returned all 1024 expected
words. This proves the tested graphics contract; it does not prove game
rendering, a protected BattlEye session, or playability.

The engine's builtin renderer path outranks a normal WINEDLLPATH overlay here.
The verified deployment therefore uses a native DXGI in this game's prefix
`system32` and the private profile's `factory7DxgiNative` option. Other graphics
DLLs retain builtin loading; 32-bit Ubisoft uses its original builtin fallback.
`install.py` checks the proof/library/engine hashes, stops only this prefix,
backs up existing files and atomically replaces its DXGI. It does not patch the
game or anti-cheat binaries or return synthetic authentication results.

## Separate 60-second wait

The earlier waiter was Windows thread `0268` in Ubisoft `upc.exe` PID `0178`.
Its critical section `768bdf5c` belongs to `libcef+0xb44df5c`. A later read showed
LockCount `-1`, recursion 0 and owner 0: the lock had been released. That later
stack is not the holder's stack at timeout and cannot establish why it waited.
A read-only sampler is watching the next run for recurrence. The Factory7 patch
must not be described as fixing this browser wait.

## Actual game follow-up

The 11:11 native Steam run loaded the private DXGI and passed the original
null dereference. It reached an activity-card scene reported by the user,
with no repeat of the original fault or 60-second lock wait in this run.
It then exposed the separate Metal shader compilation failure documented
in `../moltenvk-address-space/README.md`; the game uses native Vulkan for
this rendering path, so fixing DXGI alone cannot fix those shaders.


## First CPU fault recorder

The later run passed loading but faulted on a read of 0x28 in dynamically
allocated executable memory. The opt-in recorder preserves 128 instruction
bytes, region metadata, and 256 stack words locally before the original exception
handlers run. It does not consume exceptions or force successful returns.
`first-fault.patch` and `motorfest-first-fault.inc` add this diagnostic to the
private DXGI only. Apply the patch and copy the include to `src/dxgi/` before
building. It opens a file only when the process is exactly TheCrewMotorfest.exe
and the private runner supplies a diagnostic path. The isolated fault test wrote
2,280 bytes then reached its original recovery handler; Factory7 identity/event
lifetime and 1,024-word GPU readback passed with this same DLL. This is diagnostic
coverage, not a fix for the CPU crash. Evidence remains local and is not uploaded.
