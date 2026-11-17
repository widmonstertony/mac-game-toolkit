# Motorfest: Linux bridge versus actual client

Status: component diagnosis only; Motorfest gameplay remains unverified.

## Inputs and provenance

- MetalSharp Wine 7.0 arm64, original signed/notarized app. Archive SHA-256:
  `68f8acce238e09da334edb36301a954996bb1b9edbe2c1c6bc60fdf483df99a0`.
- Linux host source pinned to
  [`b1ceb881dc6443da8772fbae3a906204266e21cc`](https://github.com/metalsharp/MetalSharp-Wine/tree/b1ceb881dc6443da8772fbae3a906204266e21cc).
  Built its reviewed `build.sh` with llvm-mingw 20261006. No shared engine files
  or game prefix were changed.
- [Heroic's runtime downloader](https://github.com/Heroic-Games-Launcher/HeroicGamesLauncher/blob/main/src/backend/wine/runtimes/runtimes.ts)
  obtains its list from `https://lutris.net/api/runtimes`. On this date the
  `battleye_runtime` entry points to the
  [Lutris 2024-05-21 archive](https://github.com/lutris/buildbot/releases/download/2024-05-21/battleeye_runtime.tar.xz).
  Local archive SHA-256:
  `229eaedf3bc16d5bf548bc2aed94bc48c44e2038337eedacbd17c6c7244cd212`.
  This hash records the downloaded artifact; the API supplies no independent
  digest. It does not establish equivalence to today's Steam depot.
- Archive paths, member types and total expansion size were checked before
  extraction into a workspace-only directory. No game payload is included here.

## Actual results

The source test executable ran in a separate prefix, using the candidate FEX
engine, with neither the Motorfest account state nor game data attached.

| Probe | Result |
|---|---|
| `hosttest hello hello.so` | Exit 0; mixed arguments, callback, TLS/mutex and direct syscall checks passed |
| `hosttest extras extras.so` | Exit 0; exception unwinding, backtrace, sockets, poll/epoll, threads, mmap and locale checks passed |
| `hosttest inspect …/v1/lib64/wine/beclient_x64.dll.so` | 26 relocations; 10 resolved mandatory imports; 0 unresolved; 3 optional weak imports absent |
| `hosttest inspect …/BEClient_x64.so` | 207 relocations; 129 resolved; 47 unresolved; 10 optional weak imports absent |

The combined inspect command returned 1. Inspect maps and relocates the libraries
but does not run their constructors or exported functions. The host's supported
relocation-type count was zero failures for both files. Its glibc resolver
provided 267 functions. This isolates missing dependencies beyond successful
FEX execution of the small bridge.

Reported unresolved names (the diagnostic buffer can deduplicate names, so
this list is not a separate relocation count):

```text
_ZTVN10__cxxabiv117__class_type_infoE
_ZTVN10__cxxabiv120__si_class_type_infoE
_ZNSs4_Rep20_S_empty_rep_storageE __gxx_personality_v0
system __ctype_toupper_loc strcasecmp __fdelt_chk sendto strspn
gethostbyname __memcpy_chk strtol __stpcpy_chk strncasecmp sscanf
__vsnprintf_chk __strcpy_chk __fprintf_chk usleep
_ZNSs6appendEPKcm setmntent endmntent _ZNSsC1EPKcmRKSaIcE
_Znam _ZdlPv getmntent _ZNSs4_Rep10_M_destroyERKSaIcE
_ZNKSs7compareEPKc _ZNSsC1EPKcRKSaIcE _ZSt9terminatev
gethostname sched_getaffinity tolower _ZNSs6assignEPKcm
_ZNSs6assignERKSs longjmp _ZdaPv isalnum localtime_r
__cxa_begin_catch _Znwm _ZNSs6appendERKSs sched_setaffinity
```

Several names belong to the GNU C++ runtime; others are glibc/network APIs.
Do not replace unresolved calls with no-op or successful-return stubs. A real
dependency/ABI implementation and subsequent initialization/session validation
are required. The upstream bridge-only success report does not cover this list.

## Remaining gates

1. Obtain and identify the appropriate current runtime without disrupting the
   user's native Steam session. Anonymous official SteamCMD returned
   `Failed to install app '1161040' (No subscription)`; this is terminal, not an
   in-progress download.
2. Resolve the actual client's dependencies with correct ABI and semantics;
   rerun the host checks and dependency inspection before executing client code.
3. Verify the complete legitimate game launch and protected session. Do not
   disable BattlEye or use a Story Mode test from another game as evidence.

Native Steam window binding was retried and still returned ScreenCaptureKit
`-3811`. No alternate UI-control mechanism was used. No game restart or login
retry was requested from the user for these component checks.
