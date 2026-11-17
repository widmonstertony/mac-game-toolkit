# Linux host C ABI additions: experimental, not installed

This is a general C runtime compatibility experiment for the legitimate Linux
client route. It does not patch a game, anti-cheat binary, account, identity or
authentication response. No game session is validated by these results.

The original signed MetalSharp Wine 7.0/FEX engine runs the tests in a separate
`probe-prefix`. Its engine and the existing Motorfest container are unchanged.

## Provenance and build

The host sources are from [MetalSharp's Linux host at b1ceb881](https://github.com/metalsharp/MetalSharp-Wine/tree/b1ceb881dc6443da8772fbae3a906204266e21cc),
under the project's AGPL-3.0 license. This directory's additions use that license
too. `upstream-files.json` pins the C sources and headers used for the tests.
The builder verifies them before copying to a new isolated output directory;
it adds resolver tables and compiles the added compatibility files. The upstream source
directory is not edited. No upstream archive or proprietary client is published
in this directory.

```sh
python3 build.py --source /path/to/pinned/linuxhost \
  --toolchain /path/to/llvm-mingw --out /tmp/motorfest-libc-test
python3 run-test.py --build /tmp/motorfest-libc-test \
  --engine /path/to/MetalSharp-Wine.app/Contents \
  --prefix /path/to/isolated/probe-prefix \
  --runtime /path/to/already-downloaded/battleeye_runtime
```

## Initial validated subset

Eleven exports were added: `strtol`, `strspn`, `strcasecmp`, `strncasecmp`,
`tolower`, `isalnum`, `__ctype_toupper_loc`, `__memcpy_chk`, `__strcpy_chk`,
`__stpcpy_chk` and `__fdelt_chk`.

- `strtol` uses Linux's 64-bit long and the host's per-thread Linux errno. Tests
  cover both signed limits, overflow, sign, whitespace, bases, no digits and end
  pointers. It implements the legacy export, not C23's separate binary-prefix
  version.
- Character/string operations are for the host's existing C locale. This does
  not add support for other locales.
- Fortified copies check bounds before any write; FD-set indexing checks its
  range. Five negative processes use invalid destination pointers or FD values
  and terminate through the expected rejection path, without performing a write.
  The host lacks Linux signal delivery, so rejection uses terminal Windows
  fail-fast (`0xc0000409`), **not a claim of SIGABRT emulation**.
- `strings.so` is actual Linux x86_64 ELF code loaded by the PE host, exercising
  System V calling convention, return values, pointer sizes and ELF resolution.
  It has 12 resolved imports (including the existing errno accessor), zero
  unresolved/unsupported imports, and its behavioral checks pass.
- Upstream `hello` and `extras` regression checks pass with the additions.

The same Lutris-distributed 2024-05-21 original client is still only inspected;
its constructors and exports are never executed by this test. The bridge has
zero unresolved mandatory imports. The client's unresolved relocations decrease
from **47 to 36**, with zero unsupported relocation types. That remaining failure
is explicitly recorded, even though the general ABI tests pass.

See [validation-2026-11-16.json](validation-2026-11-16.json) for outputs, hashes,
negative tests and outstanding state. Missing GNU C++ runtime, networking,
formatted I/O and thread functionality still prevent a complete client load.
An authenticated game session is a separate remaining requirement. This old
runtime is not established to be the current Steam depot.

Contract references: GNU libc's [integer parsing](https://sourceware.org/glibc/manual/latest/html_node/Parsing-of-Integers.html)
and [source fortification](https://sourceware.org/glibc/manual/latest/html_node/Source-Fortification.html).

## GNU C++ dependency experiment, November 16

`dependency-probe.c` resolves C++ symbols from **unmodified** `libgcc_s.so.1`
and `libstdc++.so.6.0.21` obtained from Valve's Steam Runtime Scout repository.
Package URLs, index/package/library hashes and verification scope are recorded
in [validation-cxx-2026-11-16.json](validation-cxx-2026-11-16.json). HTTPS index
hashes were checked; no independent GPG verification is claimed. No proprietary
client or runtime library is redistributed here. The first-name lookup matches
the required export versions for all 30 applicable imports in these exact three
files; this is not a general implementation of versioned ELF symbol lookup.

Seven additional host exports supply `ceil`, `floor`, `frexpl`, `get_nprocs`,
`__assert_fail`, `__snprintf_chk` and `__vsnprintf_chk`. The 80-bit `frexpl` test
initially failed: on this MinGW target, Clang's `sysv_abi` still returned fp80 via
a hidden pointer. An explicit assembly adapter now preserves Linux's stack input
and x87 return convention. The probe verifies the real result and exponent.

Formatting tests exercise Linux 64-bit long, integer and floating register spills,
80-bit long double, dynamic width/precision, truncation and protected `%n`.
Seven rejection processes verify bounds and writable-format rejection. Sequential
narrow C conversions are supported; positional/wide conversions fail with EINVAL.
This is not a complete glibc printf implementation. In particular, upstream maps
ELF images writable: a literal in such an image fails the fortified `%n` readonly
check. The positive test uses a genuinely read-only mapped page; the check is not
weakened to make the test pass. Pointer formatting flags also need wider coverage.

All general ABI tests and upstream hello/extras regressions pass. The original
GNU libraries have **zero unresolved imports**. An owned Linux C++ ELF probe
actually allocates/frees memory, throws and catches an integer, and runs the
stack object's destructor during unwinding using those libraries. It passes.

```sh
python3 run-cxx-test.py --build /tmp/motorfest-libc-test \
  --engine /path/to/MetalSharp-Wine.app/Contents \
  --prefix /path/to/isolated/probe-prefix \
  --gnu-runtime /path/to/pinned/steamrt-libraries \
  --client /path/to/original/BEClient_x64.so
```

The runner validates build and GNU library hashes. Only the owned C++ probe runs;
the client remains an inspection target without constructors or code execution.
Its unresolved relocations are now **16**, across 14 missing names: `system`,
`sendto`, `gethostbyname`, `sscanf`, `__fprintf_chk`, `usleep`, `setmntent`,
`endmntent`, `getmntent`, `gethostname`, `sched_getaffinity`, `longjmp`,
`localtime_r`, and `sched_setaffinity`. The normal loader still does not automatically
load these GNU dependencies; this is an explicit test resolver. **Nothing has been
installed in Motorfest and gameplay is not verified.**

## UDP additions, November 16

`libc-network-extra.inc` is included only in the builder's copied `libc_net.c`.
It reuses the upstream descriptor/address/errno translation for `sendto` and
adds `gethostname`. The Linux ELF test sends and receives real loopback packets
on both IPv4 and IPv6, including zero-length datagrams and connected UDP with no
explicit destination. It also checks closed descriptors, undersized addresses,
oversized datagrams (rejected before touching the invalid buffer), hostname
truncation, and unsupported flags. Hostnames and local addresses are not logged.

`MSG_NOSIGNAL` is supported; `MSG_DONTWAIT` and other unimplemented Linux-only
send flags explicitly return EOPNOTSUPP instead of becoming blocking calls.
No global socket-mode changes or fabricated network replies are used.
The implementation follows the [Winsock sendto contract](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-sendto).

All ABI, bounds, loopback and GNU C++ behavioral tests pass together. The original
client is still inspected only, with **14 unresolved relocations across 12 names**
remaining. See [validation-network-2026-11-16.json](validation-network-2026-11-16.json).
This does not validate authentication, anti-cheat initialization, or gameplay.

## Ordinary dlopen and actual DLL entry, November 16

The earlier C++ test used a dedicated dependency resolver. `gnu-runtime.c` now
connects the validated GNU libraries to the host's ordinary `dlopen` path with
the opt-in `LINUXHOST_GNU_RUNTIME` directory. It checks both exact SHA-256 hashes
and loads the bytes that were hashed, resolves symbols only from those verified
module objects, requires complete dependencies/syscall routing, and then runs
constructors. Initialization is once per process. An absent override preserves
the non-C++ path; a supplied but invalid runtime reports an error.

The copied host's `dlopen` also rejects unresolved/unsupported imports before
running constructors. Repeating a failed open cannot return the incomplete
module that the upstream loader left in its cache. This host does eager linking;
general glibc lazy-binding semantics are not implemented.

`run-dlopen-test.py` verifies the ordinary path with a C++ constructor, allocation,
exception and stack destructor. It then runs an independent PE executable that
links **no host implementation**, uses `LoadLibrary` on the actual candidate
`linuxhost.dll`, and obtains its resolver with `GetProcAddress`. The same C++ test
passes through this DLL entry. Missing/modified GNU libraries are rejected before
the test constructor executes; a failed cached open remains rejected. These are
six passing cases, not evidence of a protected game session.

```sh
python3 run-dlopen-test.py --build /tmp/motorfest-libc-test \
  --engine /path/to/MetalSharp-Wine.app/Contents \
  --prefix /path/to/isolated/probe-prefix \
  --gnu-runtime /path/to/pinned/steamrt-libraries
```

The same build adds `gethostbyname` and `__h_errno_location`, translating Winsock's
16-bit hostent fields to Linux's 32-bit fields and preserving resolver error
categories. Local/numeric lookups and two-thread result/error isolation pass.
No external game endpoint is used. The IPv4-only legacy lookup rejects an IPv6
literal; the provider may report HOST_NOT_FOUND or NO_DATA. References:
[Winsock gethostbyname](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-gethostbyname).

All prior ABI, bounds, IPv4/IPv6 and C++ checks pass in this combined build.
The inspected original client still has **13 unresolved relocations across 11
names**, recorded in [validation-dlopen-2026-11-16.json](validation-dlopen-2026-11-16.json).
Global destructor registration/teardown, full Linux signals and other upstream
limitations are not covered by the stack-destructor test. This candidate remains
isolated and is **not installed in the game**.
