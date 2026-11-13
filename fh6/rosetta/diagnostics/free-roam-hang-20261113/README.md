# FH6 Steam return-to-free-roam hang, 2026-11-13

Not fixed yet. This is a diagnosis receipt, not a successful gameplay test.

- Native macOS Steam launched the existing FH6 build 483970 once, macOS PID
  97075, Wine PID 228, observer start tick 6330014. The player explicitly
  confirms they did not click Stop, close the window, or request exit.
- The game's own `HangReport.xml` says `hang34`, driving uptime zero,
  `track_route/player_houses/player_house_one/activate_cold_boot`, waiting in
  `wait_for_rollout_part_02_cutscene` / return-to-free-roam transition.
  This narrows the symptom to a transition; it does **not** identify a video
  codec, missing asset, or a specific graphics command as the cause.
- The first public CRT exit request was `exit(0)` on the game main thread at
  tick 6782474. Observed access violations began after this request, not before.
  The remaining PlayFab DLL detach stack is a secondary cleanup hang.
- Before cleanup, Wine reported two critical-section timeouts:
  `0x2538d04b0`: waiter 0x1d0, owner 0x1d4;
  `0x14a9340d0`: waiter 0x1f8, owner 0x1f4.
  Earlier all-thread stacks predate these timeouts. They cannot prove what
  either owner was doing at the moment of the hang, nor establish a lock cycle.
- D3D error code in the report was zero; reported VRAM use ~11.95 GB versus
  budget ~55.66 GB, audio API/error counts zero. These counters do not support
  a VRAM-exhaustion or reported audio-API failure diagnosis. They do not rule
  out every GPU synchronization or resource-lifetime bug.
- SR and FG dispatches continued to succeed through frame 16440. Two bounded,
  GPU-completed captures at frames 15734/15735 contained a black scene/loading
  spinner. They are not car-tail evidence and cannot validate the ghost repair.
- Only confirmed exited/hung game PID 97075 was subsequently killed. Steam,
  the engine, settings, caches, cloud settings and other games were untouched.

The existing native x64 `concrt140` override is already installed, so the
upstream native-Concrt workaround is not a newly missing component here.
No speculative CRT, game binary, timeout, resolution, or FG-disable change was
deployed on the strength of these observations.

## Next collection

`../../watch_loading.py` tails only newly appended records, targets one fresh
observer session, and invokes the owned read-only Wine stack helper on the
first lock timeout (owner, waiter, live critical-section fields, all-thread
stacks). Sparse earlier snapshots cover the period before the timeout. It
stops at the first exit request, a second launch, or a bounded deadline. No
debugger attach, thread suspension, game launch/stop, or game settings writes.
Saved Wine syscall frames may be stale for threads executing user code; a
single stack is not proof of an actively blocked thread. A stderr timeout may
come from another process: compare the target's live lock owner before using it.

The report export is allowlisted. Original reports containing account/session
identifiers and raw game pixels are deliberately excluded from Git.

中文：本轮确定是游戏在返回漫游的过场等待中触发卡死检测，随后退出清理再次卡住。
两个超时锁的持有者已定位到线程号，但还缺超时当时的栈；不能宣称根因已修复。
下一轮自动在超时时抓持锁方，保留原生 Steam、分辨率、超分、2× 插帧、音频和缓存。
