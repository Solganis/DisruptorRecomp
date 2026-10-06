# Project status

Last verified: **6 October 2026**. The project is in development; full campaign
and public-release validation are incomplete.

| Area | Current state |
| --- | --- |
| Platform and disc | Windows x64; Disruptor USA, SLUS-00224 only. Linux is a development target. |
| Gameplay | Boot, menus, FMVs, audio, and the first mission have been exercised on Windows. |
| Rendering | OpenGL supports 1x–8x resolution, widescreen, geometry correction, and perspective textures. Minor edge gaps and localized texture wobble can remain. |
| Controls | Modern keyboard/mouse controls are available. Vertical look and weapon aim remain experimental. |
| Settings | Controls, Enhancements, Cheats, and System tabs; supported preferences persist between runs. |
| Interpolation and diagnostics | Frame interpolation is disabled. The Diagnostics tab and experimental debug controls are removed. |
| Ultrawide corruption | Fixed primitive-buffer overflow. The user's 8x / 32:9 training retest stayed correct through 8,934 frames and closed normally. |
| Save states | Expanded rendering buffers survive saving and loading in a fresh process; the training-level restart check passed. |
| Automated checks | Windows Release build and all 25 root CTests passed. |

## Remaining limits

- Full campaign, later levels, and broad memory-card regression coverage remain
  unverified.
- The validated private build uses captured game overlay code that is excluded
  from Git. A fresh source checkout does not reproduce that package yet.
- The retail game produces about 30 unique world/camera frames per second while
  host presentation runs at about 60 Hz. Higher render resolution does not
  increase that game cadence.
- Vertical aim needs broader actor, room-edge, and level-transition testing.
- 5x–7x resolution settings have automated coverage but have not been
  individually exercised live.
- Live volume adjustment and its relaunch persistence need user validation.

See [build details](docs/BUILD.md) and [disc requirements](DISC.md) before
building or reporting a problem.
