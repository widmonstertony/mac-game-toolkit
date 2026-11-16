# Computer-control capture recovery for native Steam

Binding the native Steam host application repeatedly failed with ScreenCaptureKit
error -3811, including after resetting the computer tool's JavaScript session.
The macOS log established `TCC Allow`, followed by failed display lookup for a
1-by-1 off-screen window at (-15000, 16132). This was not a denied screen-recording
permission and did not justify resetting permissions or the system audio service.

The actual game-library window belongs to `Steam Helper`, bundle identifier
`com.valvesoftware.steam.helper`, inside the installed Steam app's Frameworks
directory. Binding that application with the computer tool immediately returned
the real Steam window. Its screenshot showed FH6's green Start Game button,
visually confirming recovery from the earlier stale Running state. Clicking that
button through the same computer tool changed it to Stop and started the FH6
Store process. The game window was then bound using
`local.macgametoolkit.fh6.store` and its splash screen was captured successfully.

Only the computer tool was used for UI interaction. No AppleScript, CGEvent input,
alternative screenshot tool, privacy permission reset, application binary patch,
CoreAudio change, or replayd restart was used for this recovery. Future Steam UI
automation should bind the actual Helper window instead of the hidden host window.

This repairs the control-tool target selection. It is not evidence that game
ghosting or pacing is fixed; live gameplay verification is a separate task.

The game's window later appeared only as a Stage Manager thumbnail. Raising or
rebinding did not recover the full image. Selecting Window → Enter Full Screen
through the same computer tool restored a 3472×2266 capture. No display settings
or Stage Manager preferences were changed. A later transient `noWindowsAvailable`
was resolved by rebinding the same running app.

The single native-Steam run reached the garage cinematic but did not complete
free-roam verification. After the diagnostic run was ended, the Helper screenshot
showed the green Start Game button again. No alternative UI automation was used.
