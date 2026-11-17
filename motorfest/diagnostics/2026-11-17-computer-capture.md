# Computer-tool capture blocker — November 17

Status: unresolved. This is a desktop-tool issue, not evidence that Motorfest or
either Forza game has been fixed. Interactive operations remain restricted to the
user-authorized Computer Use tool.

Observed in the native Steam binding attempt:

- ScreenCaptureKit returned `SCStreamErrorDomain -3811`.
- `replayd` reported `TCC Allow`; there was no capture-permission rejection in
  this attempt.
- The Computer Use helper constructed a desktop-independent window filter for a
  1 × 1 window at `(-15000, 16132)`.
- ScreenCaptureKit then logged `failed display lookup for windowBounds`,
  `cgError=0 found=0`; WindowServer logged `Could not find display 0x0`.
- The helper's enumerated display was ID 1 at `(0, 0, 1736, 1133)` logical pixels.
  The selected window does not intersect it.

The immediate failure is an off-display window capture request. Why the helper
selected that window instead of Steam's visible main window is not established
from the redacted system log. Do not call this a game-renderer, GPU, audio or TCC
permission failure.

Recovery attempts:

- Rebinding by the actual running Steam application path: same failure.
- Opening the existing Steam application through Finder: same failure.
- Resetting the Computer Use JavaScript session: same failure.
- Terminating only the Computer Use helper and allowing the desktop app to
  recreate it: same failure with a new helper process.
- Finder and System Settings initially produced white captures; after raising
  and navigating System Settings, it produced a real window image. Thus capture
  is not universally unavailable.
- A normal `launchctl kickstart` of the user `replayd` service was refused by SIP.
  SIP was not disabled, and no alternate mechanism was used to force that service
  to restart. WindowServer, audio services, Steam and the game were not killed.

Pending isolation: put the already-open Steam main window on the built-in main
display and retry the supported binding. If it still selects the hidden window,
the tool needs a window-selection fix: reject invalid/nonintersecting tiny
utility windows, prefer the application's usable main window, and tolerate one
uncapturable window without failing the whole application binding. This is a
proposed upstream remedy, not a patch applied to the signed helper. The current
documented macOS Computer Use API does not expose a window-ID selector.

No raw system logs, account data or user screenshots are included here.
