# Forza Horizon 4: owned Microsoft Store package

## Extraction completed, November 16

The owned Store payload has now been unpacked into
`~/Games/Windows/Xbox/Forza-Horizon-4/Content`: 4,341 files, about 79.32 GiB.
Ciphertext block hashes and all available plaintext file hashes passed.
`ForzaHorizon4.exe` and DLL signatures were checked after decryption. The
original verified download is retained. **Runtime installation and playability
remain unverified**; this is UWP `Forza.App`, not the FH6 GDK/MSIXVC route.

### MACxus library import completed

The existing `Content` folder was selected through MACxus's built-in **Import
game** dialog. The UI now shows Forza Horizon 4 as **Local installation / Imported**,
and its persisted entry identifies `ForzaHorizon4.exe`, package
`Microsoft.SunriseBaseGame`, version `1.478.564.2`. No game files were duplicated
and the launcher was not patched to pretend the installation is ready.

The remaining launch gate is specific: `inspectGameDirectory` requires an
existing `.xodus-streaming.msixvc` and an executable for state `ready`;
`prepareGameLaunch` refuses the `imported` state. This extracted EAppx package
has no such MSIXVC. Creating an empty file with that name would only falsify
the check, not implement UWP activation or the backend container format.
The UI correctly displays that warning. Library import is complete; runtime
adaptation and gameplay are still pending.

`extract_owned.py` reads the nested payload directly at its bundle offset,
avoiding an additional 79 GiB intermediate copy. The local CIK remains in the
private MACxus directory. Output paths are checked, existing files require hash
verification before resuming, and extraction stops before free space falls
below 25 GiB. The format and XTS tweak reference is
[eappx-rs](https://github.com/tuxuser/eappx-rs); the owned sector helper uses
OpenSSL AES-128-XTS. No content key or game payload is included in this repo.

An in-memory encrypted DLL probe and the upstream tweak test vector passed
before extraction. During extraction, the reader was corrected to include the
final encrypted sector padding outside the footer's logical length. Files
without an optional plaintext FileHash still require every ciphertext block
hash, and their resulting plaintext SHA-256 is recorded locally for resuming.

## Download and earlier inspection history

The PC EAppx bundle has downloaded completely (85,219,173,025 bytes, 79.37 GiB).
Its full SHA-256 matches Microsoft's FE3 package metadata. See
`download-receipt.json`. The local destination is
`~/Games/Windows/Xbox/Forza-Horizon-4/package-download/`.

At the initial download checkpoint this was package data, not installed content.
The extraction above does not yet establish a verified-playable game.
MACxus 0.3.11's install-plan only accepts supported Windows.Desktop MSIXVC
packages. FH4's selected PC package is EAppx/UWP. Its built-in streaming installer
therefore did not perform this download. A subsequent manual import now lists
the actual extracted files, with their unsupported launch state intact.

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
The normal account-license request returned a private local CIK file. An initial
comparison checked only the second GUID in the header's two-GUID identifier and
reported a mismatch. A subsequent read confirmed that the first GUID matches the
license ID. That corrects the comparison; it does not prove successful payload
decryption or runtime support. No license material is published.

A streaming read of the inner block map and its unencrypted 10,871-byte manifest
confirmed `ForzaHorizon4.exe`, UWP entry point `Forza.App`, and dependency
`Microsoft.VCLibs.140.00` at least `14.0.24123.0`. The manifest also registers
`Microsoft.Xbox.Services.dll` and three controller WinRT components. This is not
the GDK/MSIXVC manifest used by the working FH6 launch route. Only the manifest
was saved locally; no executable or game payload was unpacked.

The recent [community FH4 patches](https://github.com/whyAm-i/Forza-Horizon-4-patches-for-mac)
explicitly require the **Steam** edition and a matched CrossOver build. Their
playability reports do not validate this owned UWP package or authorize applying
Steam executable offsets to it.

At the installation checkpoint, only about 48 GiB was free. Full extraction was
blocked by automatic approval review because the package alone is 79.37 GiB and
unbounded unpacking could exhaust the disk. The bounded metadata inspection
replaced that operation; it did not retry the full extraction. Do not delete the
verified package or other games to work around this without a reviewed space
plan. FH4's window/fullscreen configuration remains pending an actual installation.

Any local account licenses or keys belong in the user's private MACxus data
directory, never this repository. Do not replace the owned Store package with
modified executables or claim that download completion means Wine compatibility.
