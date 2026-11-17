# FH4 private custom-controller factory registration

This closes two real `E_NOTIMPL` results produced while loading FH4's **original**
`GameControllerInfo.dll` and `Microsoft.Xbox.Input.AdvancedGipWheel.dll`.
The private production candidate now passes all **34 owned package factory
queries**, versus 32 before this change. No game instance, Xbox sign-in, cloud
save, actual controller input or FH4 gameplay is established by that result.

## Change

`apply-source.py` accepts only the exact Highball manager source SHA-256
`afbed1e6fff5ed0fc38c8d2ecea5962a5c19b3dc5ab2f8c77c63cfd8f26dace7` in a private
Wine source tree. It adds the `custom-factories.inc` registration implementation
and wires it into actual provider arrival/removal. No global Wine engine or
another game's prefix is modified.

- Hardware-ID and GIP registrations retain the real factory and canonical COM
  identity. Duplicate identical registrations are idempotent; registrations for
  multiple matching criteria create one controller per factory/device pair.
- Hardware matching uses actual provider vendor/product values. Registration
  also matches devices that already exist. A serialized job queue delivers
  creation/add/remove outside the registry lock, including callback reentry
  and concurrent removal during creation.
- External factories implement the public `IInspectable` contract, not Wine's
  private `IGameControllerImpl`. The external wrapper therefore does not demand
  that private interface. Null creation results decline a device; failures do
  not invent a controller or fire an added callback.
- Controllers participate in the existing factory-controller lookup. External
  entries are excluded from the default-provider removal/notification loops so
  the two paths cannot notify or free the same controller twice.
- Initial allocation of the original built-in wrapper is zeroed, preventing
  its failure path from releasing an uninitialized inner pointer.

Contracts: Microsoft documents registration for a
[hardware ID](https://learn.microsoft.com/en-us/uwp/api/windows.gaming.input.custom.gamecontrollerfactorymanager.registercustomfactoryforhardwareid)
and a [GIP interface](https://learn.microsoft.com/en-us/uwp/api/windows.gaming.input.custom.gamecontrollerfactorymanager.registercustomfactoryforgipinterface).
**GIP transport limitation:** current Wine HID/XInput providers do not supply a
GIP identity. Their arrival passes no GIP ID and must not match a GIP-only
registration. The registry supports matching a supplied real GIP identity,
tested with a synthetic provider, but this patch does not implement a macOS
GIP transport or physical wheel report delivery. XUSB-type registration and
other unimplemented input interfaces are not silently turned into successes.

## Validation and build

`build.py` builds separate production and diagnostic variants, verifies their
export split, and restores production source/spec/output. The production DLL
has only the original three exports; the test variant additionally exposes
`FH4CustomControllerSelfTest`. `registry-test.inc` is never compiled into the
production DLL. `test-runner.c` calls only that self-authored test export.

The actual Wine test covers:

- null/duplicate registration, nonmatching hardware, and late registration;
- actual provider hooks and existing factory-controller lookup;
- GIP metadata matching and deduplication against a second hardware criterion;
- same-thread and cross-thread registration from an added callback;
- removal from an added callback and concurrent removal during creation;
- declined/failed creation, ordered add/remove and exact provider/factory/inner
  reference lifetimes.

The tests pass in the isolated Highball prefix. The production candidate then
passes all 34 original FH4 DLL factory queries through the merged package
manifest. The earlier original candidate records two `80004001` failures and
the old unmerged manifest records 34 `80040154` failures. See
`validation-2026-11-17.json` and the adjacent UWP package-class validation.

Two deployment/build pitfalls were caught before staging:

1. Replacing the private engine's DLL did not replace the old physical DLL
   already copied into the probe prefix. The first probe still executed its
   stubs; updating the isolated prefix copy made the candidate effective.
   A future FH4 prefix must select the same verified bytes.
2. Source/spec swaps within one filesystem timestamp tick could reuse the test
   object in a supposedly production build. The builder now invalidates only
   this module's manager object and DLL at each transition and checks exports.
   The misleading intermediate artifacts were not installed into any game.

Example (preconfigured private Wine build, llvm-mingw on PATH):

```sh
python3 apply-source.py /path/to/private/wine-source
python3 build.py --source /path/to/private/wine-source \
  --build /path/to/private/wine-build --out /tmp/fh4-custom-build \
  --cc /path/to/llvm-mingw/bin/x86_64-w64-mingw32-gcc
```

Build and test logs are local; Microsoft DLLs, game payloads, accounts and keys
are not distributed. Code is LGPL-2.1-or-later, matching Wine's manager source.
