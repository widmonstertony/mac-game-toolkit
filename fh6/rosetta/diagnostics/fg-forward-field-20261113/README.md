# Current live lock and FG pixel result

The game stayed running throughout collection and all owned GPU controls.
The timeout-owner snapshot is from before any exit/cleanup. It shows an already
released lock, not an actively held lock with a proven cycle. Wine source also
corrects the old interpretation of its `retrying (60 sec)` text: first wait 5s,
then a 60s retry. Do not diagnose the current running game's ghost from an old
DLL-detach stack.

Two real, adjacent free-roam frames (104186/104187) reproduced the duplicate in
generated color, not base scene. Raw car-light motion agreed with measured image
movement; separate replay depth dilation matched all 62,418 sampled points per
input. The forward-source candidate improves depth-proven visibility samples.
A separate conservative border fill treats stale-dark residuals where no current
primary source is visible. It remains a heuristic for unseen border information,
not a universal quality guarantee. The old inverse solver did not cover that
case. See `summary.json` and the GPU logs for the scope of each result.

The complete real official provider, startup gating, native UI, history wrap,
three rotating outputs, reset/NaN/overwrite guards and an older captured scene
were checked. Candidate runs retain official SR, official 2× generation and
native output. The generated output still differs from base frames.

Raw game pixels, local capture paths, raw account-bearing reports and tokens
remain local. This folder contains only code-evaluation metadata and sanitized
logs. Deployment receipt, if present, identifies what was actually installed;
GPU controls alone do not establish a successful live game visual result.

Deployment: the game process had already exited before replacement; no process
was killed. The installer selected this candidate in the FH6 private prefix.
`deployment.json` records the read-back DLL hash and unchanged OptiScaler INI
hash. Gameplay visual confirmation remains pending. The source is in commit
`06a076e`; to undo the code change, revert that commit, rebuild and re-run the
GPU verification, then install with the same guarded installer while FH6 is
closed. A Git revert alone does not replace an installed DLL.
