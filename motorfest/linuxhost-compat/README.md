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
it only adds a resolver table and compiles the added C file. The upstream source
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

## Actual validated subset

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
