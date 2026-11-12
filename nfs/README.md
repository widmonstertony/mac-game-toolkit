# Need for Speed: shared EA container / 共用 EA 容器

Heat (`1222680`) and Unbound (`1846380`) keep their original native Steam entries and existing game files. They use Heat's existing prefix and EA installation. `share_ea.py --apply` retains the former Unbound prefix, merges only missing NFS save directories, and hard-links the preparation locks so both AppIDs lock the same prefix. Close both games and EA before installation. Do not run both games simultaneously in this prefix.

`ea-launcher-media.reg` disables `winegstreamer` **for the four EA launcher executables only**. This avoids the observed GLib-GObject `type_data_ref_Wm` fatal assertion during EA startup. Game media/rendering overrides are unchanged.

`install_launch_fixes.py --apply` patches the existing r28 Proton-ish runner with AppID-scoped settings:

- Heat: DXMT and `dxgi.handleAltTab=True`. Its black-screen log repeatedly selected the identical `3472x2266@120` display mode after a fullscreen/focus transition. The installed DXMT implements this option; it explicitly handles foreground loss in `GetFullscreenState` and `Present`. See [upstream configuration](https://github.com/3Shain/dxmt/blob/main/dxmt.conf) and [swap-chain implementation](https://github.com/3Shain/dxmt/blob/main/src/d3d11/d3d11_swapchain.cpp).
- Unbound: explicitly select D3DMetal for its DX12 route. The automatic helper selected DXMT even when given the main executable.
- Other AppIDs: retain existing settings. Resolution, graphics quality, audio, saves and caches are not edited.

The installer checks the runner layout and installed DXMT capability, refuses changes while NFS/EA processes are alive, and retains one small original runner script. Re-run after a Proton-ish runner update if its generated script was replaced; an unknown layout fails rather than patching arbitrary code.

## Local evidence, 2026-11-12

- Both AppIDs resolve to the same actual prefix and the same preparation-lock inode.
- EA login and Heat launch succeeded; a real `NeedForSpeedHeat.exe` process was observed.
- After applying the focus setting, native Steam launched Heat again. The actual renderer logged `Found config env: dxgi.handleAltTab=True`, with `3472x2266@120` retained.
- **The fullscreen leave/return fix still needs the user's reproduction test.** Loading a setting does not prove that black screen is fixed.
- Unbound's DX12 selection is installed; successful game startup has not yet been verified. Do not change the shared prefix while Heat is running to test it.

Recovery: remove the two clearly marked `mac-game-toolkit` blocks from the runner, or restore `run.before-nfs-focus-fix` if no newer runner changes need retaining. The prefix alias and shared-lock changes are recorded in `steamapps/compatdata/nfs-shared-records/`; restore the retained prefix only with both games and EA stopped. Never delete the common game files to change containers.
