# FH4 Store package context candidate

This is an **inactive Highball candidate**, not an FH4 playability claim. It has
passed real Wine API calls and a combined UWP/D3DMetal component probe. It is not
installed into the owned game's Content directory or the shared engine, and it
does not alter licensing, cloud saves, account credentials or MACxus readiness.

## Implemented

- Process package identity: full/family names, application ID, package path,
  basic PACKAGE_ID, and a bounded main-plus-one-framework PACKAGE_INFO graph.
- Context is supplied by a launcher after parsing the actual owned manifests.
  `prepare-context.py` checks the catalog/manifests, exact FH4 executable,
  dependency name/publisher/minimum version, framework property and every
  dependency file hash. It emits an **inactive** environment receipt.
- The runtime also checks exact process executable identity and the private
  prefix's real package catalog paths. Unrelated helper executables cannot
  inherit this identity merely by inheriting the environment. This is metadata,
  **not an authentication or entitlement mechanism**.
- `GetPackagePathByFullName` now uses the existing real package catalog, via
  PackageIdFromFullName and GetPackagePath; the original function was still a stub.
- `LoadPackagedLibrary` loads the main package before the declared framework,
  passes that same ordered graph to the native loader for transitive imports,
  and excludes current-directory/PATH lookup. System32 remains available for
  operating-system imports. Invalid absolute/traversal paths and missing
  dependencies fail rather than reporting a successful load.

The semantics follow Microsoft's [package identity API](https://learn.microsoft.com/en-us/windows/win32/api/appmodel/nf-appmodel-getcurrentpackagefullname),
[package graph flags](https://learn.microsoft.com/en-us/windows/win32/appxpkg/package-constants)
and [LoadPackagedLibrary contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-loadpackagedlibrary).
The environment-based hosting approach was reviewed against
[designgears/wine-uwp](https://github.com/designgears/wine-uwp/tree/88d1de1322d1394391eefd3b98d34b836acca0cd).
This implementation is intentionally bounded to the actual FH4 graph; it is not
a general replacement for Windows package deployment, licensing or app activation.
Source is LGPL-2.1-or-later; see COPYING.LIB.

## Two integration failures found and corrected

1. The existing source's LastError synchronization guard only selected GCC.
   Clang targeting `x86_64-windows` does not define `__GNUC__`. Rebuilding even
   the unmodified package implementation omitted writes to `%gs:0x68`, while
   the installed ntdll reads that slot. A real GetTokenInformation buffer-size
   query returned false with error **0**, instead of **122**, and services.exe
   subsequently used a null environment block. Extending the guard to Clang
   restores the paired TEB/GS write; error 122 and service initialization pass.
   This correction is in the isolated source copy only.
2. Copying the complete renderer directory caused both the copied and original
   D3DMetal frameworks to load, because an existing component still referenced
   the original path. Both original and new kernelbase controls failed with
   duplicate Objective-C class warnings and a null execute address. Selecting
   the unchanged original renderer/framework paths eliminates the duplication;
   the new package candidate passes the combined probe. No Apple binary was
   edited or signed. `uwp-runtime/run-test.py --graphics-engine ORIGINAL_ROOT`
   keeps these paths separate from the private Wine-only engine.

## Actual validation

`validation-2026-11-17.json` records the tested kernelbase hash and results:

- Ordinary process, unrelated executable inheriting context, and catalog-path
  mismatch all retain `APPMODEL_ERROR_NO_PACKAGE`.
- A synthetic packaged process passes exact string/ID results, short-buffer
  no-write checks, pointer bounds, canaries, head/direct filtering, real main
  and transitive framework DLL execution, main-before-framework selection,
  rejection of a DLL planted in CWD, and missing dependency failure.
- The original installed kernelbase fails the packaged positive case.
- The new engine also passes all eight UWP lifecycle cases, asynchronous
  dispatcher ownership/cancellation/shutdown tests, automatic activation
  routing, actual D3DMetal factory imports, 4,096-pixel GPU readback and resize.
  That GPU probe is a component test, not FH4 or visible presentation.

To reproduce, clone the existing Highball source/build and Wine engine into an
isolated `fh4-package-runtime-*` directory. Run `apply-source.py --source ...`,
then build `dlls/kernelbase/x86_64-windows/kernelbase.dll` with the matching Wine
build system. Bison 3 and msgfmt are required. Only the isolated engine and its
`probe-prefix` receive the candidate DLL. Run:

```sh
python3 run-probe.py --work /private/tmp/fh4-package-runtime-N \
  --cc /path/to/x86_64-w64-mingw32-clang
```

Use the existing UWP GPU runner with `--graphics-engine` pointing to the original
Highball renderer root. Keep the generated fixture registries and raw logs local.

## Remaining integration gates

Actual FH4 Store activation and a visible game window have **not** passed.
UWP input/window events, adapter-parent interception, remaining runtime imports,
and Xbox/cloud-save integration still need verification. This candidate neither
changes the launcher to claim support nor fabricates an MSIXVC marker. Unsupported
package graphs are rejected by preparation; do not reuse it as a general Wine patch.
