# The camera state

`camd` reads these files from **`/tmp/krux`** when it starts.  They are
the camera's tuning state, not firmware:

| file | values | what it does |
|---|---|---|
| `af` | 0 - 4 | the autofocus level.  **0 turns the autofocus off completely.** |
| `focus` | -1 - 44 | a fixed lens step.  Only used when `af` is 4.  -1 means "sweep". |
| `luma_gain` | 1 - 4 | how much the luma (Y) plane is amplified before the decoder sees it. |
| `unsharp` | 0 - 2 | the unsharp mask applied to the luma plane. |

`/tmp` is a tmpfs, so this state is **volatile**: it is gone after a
reboot, and the camera comes back with its defaults.  The files here
only seed it for one session.

**None of it is required.**  A file that is missing is written out by
`camd` with its default, so the state directory always describes the
camera's configuration, and the camera works with no state at all.  This
matters: the code default for `af` used to be 0, which switches the
autofocus off, and a missing file therefore turned the autofocus off
silently.  The default is now 2.

## The `af` levels

The levels build on each other:

| level | what camd does |
|---|---|
| 0 | nothing.  The actuator is never opened, so the lens never moves. |
| 1 | opens the actuator |
| 2 | opens it and configures it (the step range and the damping) |
| 3 | also parks the lens at its default position (step 7) |
| 4 | also moves the lens to a fixed step (`focus`), so no sweep is needed |

Level 2 is the normal setting: camd sweeps the whole range when a scan
starts, and the scan can ask for a repeat.

Level 0 is silent when it is wrong: the sweep code still runs and still
reports scores, but they are the scores of a lens that never moved, and
the log has no `actuator opened` line.

## The current values

```
af         2
luma_gain  1
unsharp    0
focus      -1   (no fixed step: the camera sweeps)
```

`luma_gain` and `unsharp` have code defaults of 1 and 0, so the files
here match the code and only seed it.

## Changing them

For a session, edit the file on the device:

```sh
adb shell 'echo 4 > /tmp/krux/af'
```

`camd` reads these when it starts, so the change takes effect after the
camera daemon restarts -- a reboot is the simple way.  The level can
also be changed at runtime through the daemon's control socket.
