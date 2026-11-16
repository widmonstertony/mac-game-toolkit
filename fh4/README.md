# Forza Horizon 4: owned Microsoft Store package

The PC EAppx bundle has downloaded completely (85,219,173,025 bytes, 79.37 GiB).
Its full SHA-256 matches Microsoft's FE3 package metadata. See
`download-receipt.json`. The local destination is
`~/Games/Windows/Xbox/Forza-Horizon-4/package-download/`.

This is **downloaded package data, not an installed or verified-playable game**.
MACxus 0.3.11's install-plan only accepts supported Windows.Desktop MSIXVC
packages. FH4's selected PC package is EAppx/UWP. Its built-in streaming installer
therefore did not perform this download and no fake ready-to-play library entry
was created. MACxus not listing it among installed games is expected at this stage.

The alternative download used Microsoft's public catalog and FE3 APIs, with the
FE3 workflow informed by [StoreLib](https://github.com/query-store-links/storelib_rs).
It used exact ranged reads and resumable checkpoints; no account credentials were
sent to the public delivery CDN. The final trusted SHA-256 was required before
accepting the package. No game files or saves were removed for this download.

Upstream Xodus tracks EAppx support in
[issue 91](https://github.com/xodus-gaming/xodus/issues/91). The bundled
`extract-eappx` command calls itself a research task and accepts a local key file;
having this command does not establish support for this game's licensing or WinRT
dependencies. Installation, authorized extraction and gameplay remain unverified.
Reports about Steam FH4 do not establish that the older Store package works.

A bounded, read-only inspection of the bundle header, three footer records and
two unencrypted XML metadata records confirmed one x64 application payload,
`SunrisePCPackage.appx`, version `1.478.564.2`. No game payload was extracted.
The normal account-license request returned a private local CIK file, but the
initial package-header key-ID comparison did not match it. This is unresolved;
the existence of a license response is not evidence of successful decryption.

At the installation checkpoint, only about 48 GiB was free. Full extraction was
blocked by automatic approval review because the package alone is 79.37 GiB and
unbounded unpacking could exhaust the disk. The bounded metadata inspection
replaced that operation; it did not retry the full extraction. Do not delete the
verified package or other games to work around this without a reviewed space
plan. FH4's window/fullscreen configuration remains pending an actual installation.

Any local account licenses or keys belong in the user's private MACxus data
directory, never this repository. Do not replace the owned Store package with
modified executables or claim that download completion means Wine compatibility.
