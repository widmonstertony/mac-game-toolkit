# Ubisoft Connect login restriction: support draft

This is a local diagnostic draft, not a submitted support request. Do not attach
account files, passwords, tokens, the full process environment, or an unredacted
block-page screenshot to a public issue.

The legitimate Steam installation of The Crew Motorfest uses its own Highball
Wine 11 / CrossOver 26.3-r28 prefix on Apple silicon. Ubisoft Connect reports
version 175.1.13416. The official bundled Ubisoft and Visual C++ installers
completed successfully. The login page renders using launcher-local DXMT;
the game's renderer remains D3DMetal.

After the account owner manually submits the login form, the launcher immediately
shows “Access is temporarily restricted.” No completed authentication event was
found in the launcher log. The inspected browser arguments did not disable
JavaScript or enable automation, remote debugging, or a proxy. These observations
do not reveal Ubisoft's blocking rule or establish an account ban.

Please advise on a supported way to complete legitimate authentication in this
environment, or review the false-positive restriction using the private request
ID from the block page. The user can supply that ID directly through the page's
feedback link or official Ubisoft support.

An [upstream report](https://github.com/metalsharp/MetalSharp/issues/718) describes
the same manual-login symptom on Apple silicon. Its linked workaround changes
the WebGL device fingerprint to avoid the anti-bot decision. That workaround was
not installed. No repeated login attempts, identity spoofing, session copying,
network rotation, account reset, or anti-cheat modification was performed.

Authentication and actual game startup remain unverified; a visible launcher
page alone is not evidence that Motorfest can run.
