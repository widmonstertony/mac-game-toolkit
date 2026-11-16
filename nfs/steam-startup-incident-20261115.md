# Native Steam 0x3000 startup incident

During the NFS/FH4 work, an agent searched the whole temporary directory to find
a small diagnostic script. That scope was a mistake: the directory also held
pre-existing read-only SMB mounts. The search blocked in filesystem I/O.

Steam's main thread waited in `fgets` for its local socket/process probe. That
`lsof` process was stuck in `proc_pidfdinfo` while examining the blocked search's
file descriptor. This explains the stalled client startup and subsequent 0x3000
transport error; the game files or Steam credentials were not the cause.

Releasing the stalled temporary network mount allowed the blocked search/probes
to exit. Restarting native Steam restored the rendered library, successful
account login and download progress. A later window capture confirmed the game
download completed. No remote files, saves or account records were deleted.

After persisting per-game launch options on November 16, Steam was normally
closed using its own `-shutdown` command and reopened. Its 00:27:08 login returned
`OK`, and the main library window was present.

Future searches must target known project/game directories. Never recursively
scan all of `/private/tmp`, `/Volumes` or a user's home to locate one helper.
Reading a mount table is sufficient to identify mounts; it does not require
walking their contents. Do not unmount unrelated healthy storage as a generic
Steam repair.
