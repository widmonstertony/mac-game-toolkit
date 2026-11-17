# FH4 Store package-host gap, November 17

The owned game imports LoadPackagedLibrary, GetPackagePath and
PackageIdFromFullName. Highball 26.3 kernelbase/loader.c returns
APPMODEL_ERROR_NO_PACKAGE unconditionally from LoadPackagedLibrary. Registering
actual package paths fixes catalogue lookup in the isolated probe, but does not
assign package identity to the process or implement the loader.

[Microsoft's contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-loadpackagedlibrary)
requires a packaged process and limits library lookup to its package dependency
graph. Ordinary LoadLibrary success, a copied CRT DLL, or a fabricated success
status is not equivalent.

## Additional public source checked

[designgears/wine-uwp](https://github.com/designgears/wine-uwp/tree/88d1de1322d1394391eefd3b98d34b836acca0cd)
was inspected at commit `88d1de1322d1394391eefd3b98d34b836acca0cd`.
The source files downloaded for reading, without executing project scripts, are:

| File under `wine/` | SHA-256 |
| --- | --- |
| dlls/kernelbase/loader.c | 08e2fef42caa07ad6e8893c14a4f5410cc52ab97581c713c07d9e24acc8b65d2 |
| dlls/kernelbase/version.c | af2a7b82f9f30a976fd4d607f62bac5a569023ad1c63bba3971a5c0d9949556e |
| dlls/wineuwp/main.c | 9a74f3f240d674e4961799f5e72958d9b13b833e2741580c94e22d1a1119d0d3 |

This provides a more concrete host reference than the previously inspected
ImmersiveLoader example. Its kernelbase package APIs consume explicit
WINE_UWP_PACKAGE_FULL_NAME/FAMILY_NAME/PATH and dependency metadata; its package
library loader consults the current package path before normal loading.
Those variables have no such effect in the installed Highball kernelbase.

The loader's fallback depends on the rest of that fork's package-aware loader.
Copying the function alone into Highball would not prove dependency isolation or
correct transitive imports. Its process identity and graph must agree with the
actual manifest, catalogue and executable, and unrelated games must remain
unpackaged. It is a porting reference, not a validated FH4 Mac solution.

The project's documented runtime uses Wine 11.20 modifications and Microsoft's
frameworks extracted from a Windows image. This inspection did not download a
Windows image, execute service shims, replace the shared Wine engine, or change
licensing. No FH4 gameplay or cloud-save result is claimed. Next integration
needs real package identity, graph-aware loading and activation together, plus
the still-missing CoreWindow input/events in the current local prototype.
