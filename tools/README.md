# tools/

Kernel build scripts and profiles.  `build-kernel.sh` applies a strip
profile (`hammerhead.list`) and the keep-lists (`hammerhead-keep.list`,
`hammerhead-camera.list`) to produce the air-gapped camera kernel.
`profiles/` define which kernel symbols are stripped or kept.

`KEEPLISTS` overrides the keep-list files (space-separated paths).  Use
it for a diagnostic build that needs one symbol different, rather than
editing a tracked profile in place: `scripts/cam/build-vfe-kernel.sh`
does this to turn `CONFIG_DEVTMPFS` on for the instrumented VFE kernel
only.  The standard image keeps it off; TWRP's ueventd creates the
device nodes, including the camera's runtime-allocated `/dev/video*`.
