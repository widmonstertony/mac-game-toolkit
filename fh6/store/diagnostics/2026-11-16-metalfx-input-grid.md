# Mixed-resolution interpolation: bounded reproduction and candidate correction

Status: two isolated bridge defects corrected; production game still unchanged.
Neither this document nor synthetic success establishes that FH6 is repaired.
Native Steam, native output, FSR Quality SR / 2× FG, audio, networking and caches
were retained. No texture settings or graphics presets were reduced.

## Corrections with actual GPU evidence

1. The reset sample and next sample return the current original on this native
   implementation. They must not be counted as intermediate generated frames.
   The moving-square test verifies ten actual midpoint outputs after startup,
   and verifies rejection/reset behavior without advancing history incorrectly.
2. With lower-resolution depth/motion, passing output-pixel motion directly to
   MetalFX duplicates foreground content. Input-grid normalization fixes a
   deterministic layered perspective reproduction at 1/2 and approximately
   2/3 input dimensions. Removing the normalization deliberately reproduces it.

See the bridge README, real-GPU tests and `gpu-results-20261116.json`. These run
on Apple M5 Max, macOS 27.3 build 26C5035r. Synthetic test duration while the
game is paused is not used to make a game-performance claim.

The chronological saved FH6 pair was replayed with two frozen initialization
samples, render-grid motion scale and the recorded camera matrices. Compared
with output-grid scale, large duplicated rear-body regions are substantially
reduced. Mirror/vehicle-edge fragments still fail visual acceptance. Frozen
history is not a replacement for a continuous moving sequence.

## Color/motion correspondence cross-check

A CPU 13×13 RGB-patch search compared current source color against previous
source color, independently of either frame-generation algorithm. At two
tail-light sites the supplied backward vectors were approximately (1.53,1.12)
and (1.72,0.81) display pixels; best integer offsets were (2,0) and (2,1).
Other road/body sites have ambiguous or occluded matches. This argues against
a uniform whole-frame motion-scale/sign error in the captured native tags;
it does not prove exact resource epoch or every disocclusion sample correct.

## Live run and pacing limits

The same native-Steam launch reached free roam. Menu and settings navigation
worked through Computer Use, but W/Up held input did not move the vehicle.
The keyboard screen confirmed W acceleration, A/D steering. No bindings were
changed. The built-in benchmark requires restarting the game, so its prompt
was cancelled. The existing run was left in the pause menu.

A persistent read-only Wine saved-syscall observer finished its bounded
240-second interval without a new fence trigger. This does not prove the
absence of stutter and does not provide a holder stack. Saved syscall state
would describe the last Wine transition, not necessarily the live PC of a
running CPU thread.

Elsewhere in this launch, interpolation fence value 4749 advanced after about
233.99 ms. The recorded preceding composition dependency (23396) was already
reached (23399) at the stalled observation. Therefore that recorded dependency
does not explain the whole interval. The observer did not capture a causal
thread stack for this event; it must not be described as a solved CPU/GPU wait.
The old 1.11-second event is also unresolved.

Periodic descriptor snapshots during this run showed no allocation or
fragmentation failure while the installed batch remained 512. This short run
does not validate the previous approximately-hour-long FHE11 failure. The
crash observer remains available to capture that initial failure instead of
only exit cleanup.
