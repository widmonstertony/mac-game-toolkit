# Motorfest: child waits, signals and the actual client bridge

The requested process/signal compatibility changes are implemented and tested.
This does **not** establish that they were the only things blocking Motorfest.
The active game container still uses Highball; its native Steam mapping has not
been changed. No game executable, account state, original client binary or
authentication response was modified.

## Concrete faults and fixes

The upstream serialized child table could wait on its first live child even
when a later child had exited. The replacement scans all matching children,
returns a completed child immediately, otherwise waits for any eligible process
handle. Reaping closes handles and allows slot reuse. Tests cover WNOHANG,
normal exit, real termination, signal status, a completed child before kill,
ECHILD and 24 sequential launches. Unsupported process-group/options report
an error. This remains the upstream shared-memory fork model, not POSIX fork.

Linux signal masks and handler calls need a different ABI from Windows, and the
raw x86_64 Linux signal syscall mask is 8 bytes rather than glibc's 128 bytes.
The new translation handles that distinction, standard pending signals, mask
inheritance and saved-mask jumps. Unsupported realtime queues, SA_SIGINFO and
alternate stacks are rejected instead of reporting false success.

Real hardware-fault tests found guest FS cleared both on Wine exception entry
and after NtContinue under FEX. Restoring it only before the handler did not
repair the return path. The candidate restores the guest thread control block
at both transitions. Repeated illegal-instruction/access-fault recovery and a
handler that fixes page protection then returns to retry the faulting load pass.
The exception filter covers loaded Linux ELF code only, not game PE or graphics
code. Full AVX and asynchronous host signal delivery are not established.

`system()` uses an explicit arm64 native supervisor, an owned exclusive result
file and real child wait status translated from Darwin to Linux. It does not
pretend the macOS shell has Linux commands, mounts or `/proc` behavior.

## From import inspection to a real DLL load

With the pinned GNU runtime, native-child helper and logical Wine affinity opt-in,
the original client's imports resolve and its constructors return. Loading the
old `.dll.so` as a modern Wine builtin initially failed with error 126. An owned
PE adapter now performs normal ELF loading/import binding and forwards to the
original bridge, without rewriting its code or its return values.

The adapter uses raw exported Win64 entries rather than the host's SysV debug
entry trampoline. An owned fixture verifies six floating-point arguments,
including stack arguments, and guest thread-state restoration. It is a test
fixture only and is excluded from deployment payloads.

The actual original bridge returns `GetVer=250` through both an absolute PE load
and a same-directory DLL-name lookup. That early name-lookup test did not
exclude application-directory discovery; see the stronger check below. A staging error initially inserted a 17-byte
Wine marker into a 16-byte slice, shifting PE offsets and causing error 193;
the builder now writes exactly 17 bytes and asserts unchanged image length.
That was an adapter packaging bug, not a fault in the vendor DLL.

`Init` and authenticated game initialization have **not** been run by these
probes. The Lutris package is dated 2024-05-21 and is not proven equivalent to
the current Steam depot. Zero unresolved imports and a version result are
component milestones only.

## Native Steam and deployment

Highball's old Rosetta engine does not supply the tested FEX Linux-host path.
Dropping the candidate DLL into that engine alone cannot enable it. A private
Motorfest runner must integrate the signed MetalSharp engine and keep the
native Steam bridge, without changing shared compatibility tools.

The existing Proton-ish bridge expects its ntdll redirection and interface
storage inside steamclient64's `.data` section. The candidate forwards named
exports to the user's real lsteamclient, with dedicated storage and no CRT data
in that region. No login, entitlement or ownership result is substituted.
The earlier diagnostic loaded the real macOS client library but could not open
a Steam pipe while native Steam was closed. The final overlay's runner now
records that precondition and avoids pointless login/session retries.

`stage-candidate.py` collects hash-verified host, helper, adapter, original runtime,
GNU dependencies and Steam overlay into a new **inactive** directory. The local bundle is staged under Motorfest AppID 2698940 at
`.motorfest-linuxhost-candidates/39-43`, outside the active `pfx`. Existing
directories are never overwritten. `candidate.json` records the precise hashes
and remaining gates. Removing that directory rolls back staging because no
active runner/registry/game files are changed.

Remaining integration work is explicit:

1. Verify SteamAPI initialization through the final overlay with native Steam
   running and online; do not start another Windows Steam client.
2. Validate the combined private runner in the cloned container, including the
   current Ubisoft/graphics setup, before activating it for AppID 2698940.
3. Validate actual Motorfest initialization/session and gameplay separately.

The user can open native Steam for step 1 without launching Motorfest. No UI
control fallback or repeated game restart has been used for these probes.

## Reproducible evidence

[Process/signal and bridge results](../linuxhost-compat/validation-process-signals-2026-11-16.json)
bind the successful tests to the build receipt and binary hashes. The same core
build passed ordinary ABI/bounds, IPv4/IPv6, GNU C++ exception/destructor,
dlopen dependency rejection, logical affinity and nonlocal-jump regressions.
Raw logs and the cloned prefix stay local; account-bearing files are not
published. The source additions are AGPL-3.0-only, matching the Linux-host base.


## Combined client/graphics check and missing registration

The next offscreen probe runs the real original bridge's GetVer and a D3D12
clear/copy/fence/readback workload in one process under the candidate engine's
Mullion backend. It lives in a separate application directory with no adjacent
bridge DLL. The first corrected probe failed before graphics with error 126:
WINEDLLPATH alone did not discover the newly introduced bridge. This closes a
coverage gap in the earlier same-directory name-loading test, which returned
250 but did not establish the full deployment lookup route.

Registering the hash-verified owned adapter at the **isolated clone's**
`C:\windows\system32\beclient_x64.dll`, with the private overlay and builtin
load order, fixed that failure. Existing differing files are never overwritten.
The original vendor bridge/client remain unmodified. The resulting same-process
run returned version 250, completed a real GPU fence and read back all 4,096
RGBA pixels within one quantization step. No window was created and vendor Init
was not called. This validates component coexistence, not Ubisoft startup,
visible presentation, authenticated Motorfest or road performance.

[Combined GPU receipt](../linuxhost-compat/validation-combined-gpu-2026-11-16.json)
records the exact renderer/adapter/probe inputs. `run-gpu-entry-test.py` builds
the probe statically to avoid the missing libc++/libunwind dependency in the
first test executable. That earlier exit 53 was a test packaging fault, not a
Motorfest diagnosis. The production candidate remains inactive; the registration
step must be carried into the final Motorfest-only installation after its
remaining native Steam/session and runner checks.
