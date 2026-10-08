# Getting started

This Windows x64 alpha requires your own **Disruptor (USA, SLUS-00224)** disc
image. Game assets are not included. You do not need to build the project,
extract its executable, or supply a PlayStation BIOS to use the release.

Join the [community Discord](https://discord.gg/aeTQjaQUr) to discuss the project
and share feedback.

## Setup

1. Download **DisruptorRecomp-v0.1.0-alpha.1-win64.zip** from the
   [GitHub release](https://github.com/micmea668/DisruptorRecomp/releases/tag/v0.1.0-alpha.1).
   Choose the Windows ZIP asset, rather than GitHub's source-code archives.
2. Use **Extract All** and open the extracted folder. Keep its files together
   in a writable location, such as your Games folder.
3. On Windows 10 or 11 x64, install the latest
   [Microsoft Visual C++ x64 Redistributable](https://aka.ms/vc14/vc_redist.x64.exe)
   if it is not already installed. The GPU driver must support OpenGL 3.3.
4. Copy your raw **MODE2/2352 BIN/CUE** disc dump into the package's `input`
   folder. Name the CUE **Disruptor (USA).cue**. The BIN can keep its original
   filename, provided the CUE's `FILE` line names that exact file.
5. Double-click **Play Disruptor.cmd**. At the game menu, use the arrow keys
   and **Enter** to select **New Game**, or choose **Practice Mode** to try a
   training mission.

OpenBIOS is included. PAL and Japanese discs are not supported. Keep the raw
BIN/CUE format; do not convert the disc to a cooked ISO.

## Controls

| Action | Default key or button |
| --- | --- |
| Move / strafe | W / S / A / D |
| Turn | Mouse, after capture; arrow keys also work |
| Capture / release mouse | Middle-click; Escape also releases |
| Fire / menu confirm | Left mouse / Enter |
| Psionic attack | Right mouse / F |
| Jump / menu cancel | Space |
| Use | E |
| Choose weapon / psionic | Q / R |
| Map / pause | Tab / P |
| Settings | Backquote (`` ` ``); Escape closes |
| Fullscreen | Alt+Enter |
| Save / load state | Shift+F1–F12 / F1–F12 |

Modern mouse/keyboard controls, geometry correction, and perspective textures
are enabled initially. Vertical mouse look is experimental and starts off;
enable it in **Settings → Controls** if desired.

**Settings → Enhancements** offers 1x–8x resolution, widescreen options, and a
HUD size for widescreen.
The default is 4x at 4:3; lower the scale if performance is poor. Menus and
movies remain at 4:3. Controls, display, and audio preferences are saved in
`settings.toml` beside the executable.

## Saves and updates

Use the game's memory-card save system for long-term progress. Memory cards and
save states are stored in `saves/`; back up that folder before installing a new
release. Save states are specific to their build and may not load after an
update. The original password system is also available in the game.

God Mode resets off on launch. Granting all weapons and psionics marks the game
as cheated, including subsequent game saves.

## Troubleshooting

- **Missing MSVCP140 or VCRUNTIME140 DLL:** install the latest x64 Visual C++
  Redistributable linked above, then relaunch. See Microsoft's
  [runtime download guidance](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist/).
- **Disc not found:** confirm you extracted the ZIP, placed both BIN and CUE
  in `input/`, and named the CUE exactly **Disruptor (USA).cue**. Its `FILE`
  line must match the BIN filename.
- **Wrong disc or boot failure:** only the supported USA revision works. Its
  BIN is 636,350,064 bytes and has SHA-256
  `3b49f9874e30c613ca9d17720716764cd76d0ac968c0acd0f53159366c0cf3a4`.
- **Slow rendering:** lower resolution to 2x or 1x in Enhancements and update
  your graphics driver. The game retains its original world-update cadence.
- **Mouse does not turn:** enter gameplay and middle-click to capture it.
- **Other problems:** include the level, reproduction steps, render settings,
  and `startup.log` in a [bug report](https://github.com/micmea668/DisruptorRecomp/issues).
  Do not attach your game image, game assets, or BIOS files.

This is an early alpha. The first mission and training-level path have been
tested, including the recent 8x / 32:9 corruption fix. Full campaign coverage
remains incomplete, and minor geometry or texture artifacts can remain.
