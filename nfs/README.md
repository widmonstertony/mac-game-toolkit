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

## EA blank login view, 2026-11-15

The two games still resolved to **the same Heat prefix**. Unbound's launch inherited
D3DMetal into EA's browser as well as the game. EA's CEF GPU helpers repeatedly
terminated with `-[NSViewBackingLayer setDisplaySyncEnabled:]: unrecognized selector`.
The game had not started; EA was waiting for authentication behind a blank view.

`install_ea_browser_fix.py --apply` installs the existing Highball DXMT `d3d11.dll`
and `dxgi.dll` only in the versioned EA application directory, and sets native
overrides only for `EADesktop.exe` and `EACefSubProcess.exe`. It removes the
17-byte `Wine builtin DLL` marker in the DOS stub of those private copies.
Otherwise Wine silently redirects them back to the D3DMetal directory even with
`native,builtin` overrides. No PE sections, renderer instructions, engine files,
game DLLs, authentication code, or other games are changed.

Observed after this change: `lsof` resolved both DLLs to EA's own directory;
the current log no longer contained that NSException; a window-specific screenshot
showed a rendered **Verify your identity** page instead of the blank panel.
EA account verification remained the user's local step. The subsequent game
entry confirmation is recorded below. Disabling only `vulkan-1`
was tested and did not resolve the issue; those candidate overrides were removed.

After local EA verification, the user confirmed entry into Unbound in this same
run. The real `NeedForSpeedUnbound.exe` process was then observed running for
over three minutes, and the launch log selected `renderer d3dmetal`. The EA
browser used its local DXMT copies. A separate `c0000005` line remains in the
Wine log; its process/source has not yet been attributed, so this result does
not assert a completely error-free session or long-term stability.

The installer records source and installed hashes in `.nfs-ea-renderer.json` in
EA's directory. EA updates may move to a new version directory; rerun this
installer with EA and both games closed. To undo, remove only the two recorded
matching local DLLs and the two app-specific `d3d11`/`dxgi` overrides. Preserve
the earlier `winegstreamer` overrides and account data.


## Persistent native Steam settings, 2026-11-15

`install_steam_options.py --apply` stores the renderer choices in each game's own
LaunchOptions, so regenerating the Proton-ish runner does not erase them:

- Heat: `CX_GRAPHICS_BACKEND=dxmt DXMT_CONFIG=dxgi.handleAltTab=True %command%`.
- Unbound: `CX_GRAPHICS_BACKEND=d3dmetal %command%`.

Apply only with native Steam and both games stopped. The installer preserves all
other existing VDF values byte-for-byte, keeps a private local backup, rejects
unexpected/custom launch options, and is idempotent. No account or cloud setting
is changed. The Heat setting still awaits the user's fullscreen leave/return
validation; Unbound game entry was confirmed after the EA fix.

## Bordered windows / macOS fullscreen, 2026-11-16

`install_windowed.py --apply` changes only the two installed NFS display profiles:
`GstRender.FullscreenEnabled=0`, `GstRender.FullscreenMode=0`, and
`GstRender.WindowBordersEnable=1` (the profile format separates key/value by a
space). Existing resolution, quality and audio values stay unchanged. Unbound
had no text graphics profile, so a display-only profile was created; its binary
`PROFILEOPTIONS`, saves and caches are untouched.

The desired workflow is a bordered, resizable game window, then the macOS green
button to enter and leave fullscreen. Wine's existing `adjustFullScreenBehavior`
enables native fullscreen for resizable, non-maximized, top-level windows; an
exclusive or borderless game window does not provide the same titlebar behavior.
No invented `NativeFullscreen` registry option or global driver patch is used.
The installed winemac driver contains this native fullscreen support. The user's
actual game-window roundtrip remains unverified; these settings alone do not
prove the Heat black-screen issue is resolved.

Restore `PROFILEOPTIONS_profile.before-windowed-20261116` for an existing profile.
For the newly created Unbound profile, remove the three display keys (or remove
the whole text file only if it still contains just those keys). Keep any values
the game added afterward. `install_steam_options.py --include-motorfest` also
supports the separately prepared Motorfest launch environment; other AppIDs and
account/cloud options remain unchanged.
