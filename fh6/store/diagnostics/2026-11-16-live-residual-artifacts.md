# FH6 live residual artifacts, 2026-11-16

Status: unresolved; no new rendering provider was installed in this session.
The user reports improvement, with remaining fragments beside mirrors, near
the rear wheel and in the rear window. The 12:38–12:39 screenshots are not
evidence of MetalFX output. The game was launched from native Steam at 12:30
with the existing FSR quality SR and 2× FG route, native 3472×2266 output and
2314×1510 render size. Audio, online configuration and caches were retained.

## Current evidence

- The live process stayed active through these observations. The installed
  visibility provider reported successful configuration (`flags=13`) and
  dispatch (`appliedFlags=1, decision=3`) in free roam. These flags alone do
  not establish correct pixels or successful 2× display delivery.
- Current-launch D3DMetal logs contain `AGX: exceeded compiled variants
  footprint limit`, followed by eight failed fragment-function compilations
  and no-op graphics pipelines 22341–22348. The affected shaders have not
  been identified; attributing every visual artifact to these errors would
  be premature.
- Two captures at frames 20647/20648 contain the pause menu and cannot
  establish vehicle quality. Frames 22692/22693 contain the car stopped
  beside a wall, at 0 mph. Their full scene, present, generated-before-UI,
  generated-after-UI, depth and motion snapshots are complete, but they do
  not validate the user's high-speed artifacts.
- The 8-second Metal recording contains 432 present requests, all matched
  to submission/completion. Its longest request gaps were 965.756, 274.598
  and 216.012 ms. For the longest, prior completion followed its request by
  0.182 ms, then the next present submission arrived 965.179 ms after that
  completion; the new work completed in 1.036 ms. There was no measured
  drawable wait. This excludes that particular previous command buffer
  remaining busy for the whole gap. It does not identify the CPU dependency
  or prove that an unrelated queue/fence was not blocking submission.
- This recording includes menu/fullscreen transitions. Its frame rate is
  not a valid road-driving benchmark. Trace finalization was CPU intensive
  and overlapped the user's screenshot interval; it has completed.

## Capture correction

The prior FOV-only moving-frame trigger can select a parked car. The new
`capture_moving.py` requires actual native-camera displacement between
matching-frame Prepare records, valid depth/motion/HUD-less tags and a
successful same-frame native UI dispatch. It rejects reset, menu, stale,
stationary and large teleport samples. Default mode is read-only; `--arm`
creates only the existing observer's single two-frame request. It has a
deadline, stops on a new observer session and removes only its own pending
request. The checks cover stationary, moving, reset, missing scene tags,
teleport, stale dispatch and session change; replay against the actual log
selected the user's earlier moving section and rejected the parked section.
This repairs diagnostic selection, not rendering.

## Remaining verification limitation

Computer Use intermittently captured only the Stage Manager thumbnail;
fullscreen captures were available, but input/window positioning later
returned `noWindowsAvailable` / `windowNotFoundAtPosition`. Rebinding did
not restore reliable control. Two attempts to bind System Settings timed
out and reset the tool runtime. No alternative UI automation was used.
The game was not restarted to work around this, and no global window,
audio or graphics setting was changed. An earlier test fullscreen toggle
was returned to window mode. A later fullscreen attempt restored full-size
screenshots, but menu clicks and keys still did not reliably reach the game.
Do not equate a full-size screenshot with recovered input control.

Next acceptance remains actual moving adjacent-frame comparison, then
replacement only after the candidate meets image and stability checks.
The native MetalFX saved-road replay still has artifacts and is not a
production fix, despite passing synthetic midpoint tests.

## Installed visibility ABI audit

The locally built `framegeneration-source.dll` has SHA-256
`e01596aa0ac754568b94296fd96996526eee3ca4c78bd0adcf1fa1ad8ab1829c`.
Its retained symbols and read-only disassembly establish that `flags=13`
and `appliedFlags=1` are not evidence of a disabled correction:

- `Configure` at VA `0x180010290` accepts extension type
  `0x4648365649530001`, version 1, size 184, and stores it by frame parity.
- `Dispatch` checks matching Configure/Prepare/current frame numbers,
  previous frame + 1, `(flags & 0x7c) == 0x0c`, matching dimensions,
  finite compatible depth ranges/matrix and a valid previous depth
  resource. It then checks bit 0 and writes the actual applied flag as 1
  at VA `0x180011489`, forwarding depth and matrix to interpolation.
- After successful interpolation, VA `0x1800115a9` selects decision 3
  when the applied flag is nonzero. Thus decision 3 is the accepted path,
  not an error return. This does not prove per-pixel correctness.

No binary was modified. Forcing all input flags into the applied field
would be an unjustified change; the input contract and applied feature
bits are different fields.

A later composition/interpolation fence event group contained waits near
1 second. Its recorded preceding dependencies were already completed at
the sample: interpolation 23538 and composition 58773 (completed 58775).
These are only the recorded latest waits, not a complete queue dependency
graph. A bounded trigger-based CPU sampler then observed no new stall in
its 180-second window, so it produced no causal thread stack.
