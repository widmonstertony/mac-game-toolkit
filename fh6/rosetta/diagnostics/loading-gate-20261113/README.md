# Loading-phase history gate and shutdown evidence

Status: GPU regression passed; gameplay loading and ghosting remain unverified.

## Observed game failure

The user reported loading never finished after the visibility provider update.
The preserved Windows stack for macOS PID 58575 shows the main thread already
inside `RtlExitUserProcess`, blocked by PlayFab's process-detach destructor. It
was not still loading the scene. The saved exception was a read at address
`0x58`, game RVA `0x3a4467e`. Its recovered context unwinds through CRT exit
callbacks and `_cexit`; it therefore does **not** establish why the game first
started exiting. The two first-chance worker exceptions are likewise not proof
of the initiating failure. The process was ended after saving this evidence;
Steam, saves and cache were not removed.

## Confirmed defect and change

During loading, FG remained enabled but native scene/UI tags and HUD-less color
were absent. The new history path nevertheless initialized its pipeline and
retained Prepare copies, although its postpass could not run. The owned GPU
fixture reproduced this: the old candidate reported one history pipeline where
zero was expected (`loading-regression-before.log`).

The history Prepare path now requires same-frame, compatible full scene/UI
resources before initializing or copying. Successful FG also registers already
prepared snapshots for retirement when the UI binding disappears later. Slot
retirement includes recorded but invalidated snapshots. No SR algorithm, base
frame, frame-generation multiplier or shader visibility rule was changed.

## Verification and remaining limit

- Twenty enabled FG loading frames with absent scene/UI resources pass through
  the real provider without initializing history resources.
- Full provider GPU replay still generates intermediate frames and executes four
  history corrections on saved adjacent game inputs (686,232 RGB pixels differ
  from the uncorrected provider). This is not proof of artifact-free gameplay.
- The owned public-exit fixture preserves exit code 37 through both CRT `exit`
  and Win32 `ExitProcess`, while recording the request before destructors.

The observer now logs only the main executable's public exit-import calls in
`fh6-process-exit.jsonl`, at most eight fixed records, on a separate handle with
no frame/log lock or stack unwind. It forwards the original API and code. This
is to distinguish an initial exit request from the secondary teardown hang on
the next actual run; it does not suppress errors or change game exit behavior.

Native Steam, the selected private Wine/D3DMetal route, 3472×2266 output,
FSR 3.1.6 SR, official 2× FG, audio, cloud and cache are preserved. FSR 4 and
MetalFX are not selected by this change. Other games are untouched.

Build manifest and GPU receipts identify exact validated binaries. Raw game
pixels remain local. Git parent `f53d2ee` identifies the previous deployed source
for recovery; no duplicate game/container or backup app was created.
