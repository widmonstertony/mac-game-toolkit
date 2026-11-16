# The Crew Motorfest: preparation, not verified gameplay

The installed native Steam game is AppID **2698940**, using its own existing
Proton-ish/Highball r28 prefix. It does not share the NFS/EA prefix. Its first-run
log had only reached Steam's install-script evaluator; Ubisoft Connect was
absent. The bundled official `UbisoftConnectInstaller.exe /S` and
`Support/Software/VC_redist.x64.exe /install /quiet /norestart` both subsequently
returned zero. Ubisoft Connect's executable is now present.

`configure_launcher.py --apply` gives only `upc.exe` and `UplayWebCore.exe`
browser-local copies of the installed DXMT libraries. As with the verified EA
repair, it removes just the Wine builtin DOS marker from those private copies
so Wine does not redirect them to the game's renderer. Original engine libraries
remain untouched. The prefix registry backup and local hash receipt provide
rollback information; do not delete accounts or caches to undo this.

Native Steam LaunchOptions select D3DMetal for the DX12 game, with
`DXMT_ALLOW_CROSS_PROCESS_SWAPCHAIN=1` and `WINE_SIMULATE_WRITECOPY=1` for the
Ubisoft browser. This follows the installed Highball Ubisoft recipe's cross-
process browser requirement. No game executable or BattlEye module is patched.

The requested window → macOS green-button fullscreen → window behavior is still
pending the game's own settings being generated and checked. Do not fabricate
`PCScalability.xml` enum values or claim an untested startup argument works.
Ubisoft account sign-in and actual game/anti-cheat compatibility on this Mac
have not been established. A working launcher is not proof of playable Motorfest.

## Login restriction observed, 2026-11-16

After the user submitted the login form, the actual Ubisoft Connect window
displayed **Access is temporarily restricted**, listing device/network activity
as possible causes. The launcher log had no completed authentication event and
its `user.vdat` file was absent. The inspected browser process arguments did not
include disabling JavaScript, automation, remote-debugging, or proxy switches.
These checks do not identify the service's blocking rule, prove an account ban,
or establish that every browser feature works.

No automated login retry, credential change, account/cache reset, user-agent
spoofing, or network-route change was performed. Further game startup validation
requires normal Ubisoft authentication to succeed. If the restriction persists,
use the page's feedback link or
[Ubisoft's account login support](https://www.ubisoft.com/en-ca/help/ubisoft-account/article/troubleshooting-ubisoft-account-login-issues/000063897).
Do not publish the block-page IP, request ID, login state files or credentials.

Ubisoft's [game listing](https://store.ubisoft.com/tr/the-crew-motorfest/63bc67e6d406ab22f1174306.html)
requires Ubisoft Connect and BattlEye. Reports of working Linux Proton do not
by themselves establish macOS Wine compatibility.
