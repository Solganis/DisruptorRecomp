# Building from source

## Requirements

- The verified Disruptor USA executable and BIN/CUE files in `input/`; see
  [DISC.md](../DISC.md).
- Git, Python 3, CMake 3.20+, and a C++20 toolchain.
- Windows x64: Visual Studio with **Desktop development with C++**, or
  MSYS2 MinGW-w64. Ninja is recommended.
- Linux: GCC and the platform development libraries needed to build SDL.
  Linux is a development target; Windows is the primary tested platform.

The first build needs network access to fetch the pinned PSXRecomp revision and
dependencies. OpenBIOS is built locally; no proprietary PlayStation BIOS is
required.

## Windows

Run from an x64 developer PowerShell at the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
.\run.ps1
```

The build verifies the executable, applies the framework overlay, generates and
audits the translated code, builds the runtime, and runs the tests. The runtime
is produced in `build/`, or `build/Release/` for a multi-configuration generator.

Optional launch switches can be combined:

```powershell
.\run.ps1 -ModernControls -Widescreen -GeometryCorrection -PerspectiveTextures
.\run.ps1 -ModernControls -VerticalLook
```

`-MouseAim` enables horizontal mouse aim without the modern action bindings.
Use the in-game settings menu for render scale, other aspect ratios, fullscreen,
audio, and control preferences.

## Linux

```sh
chmod +x build.sh run.sh tools/regen.sh
./build.sh
./run.sh --modern-controls --widescreen --geometry-correction --perspective-textures
```

`--mouse-aim` and `--vertical-look` are also available.

## Generated game code

The build scripts generate the translated resident game code locally from your
verified executable. That retail-derived code is excluded from this source
repository. The Windows alpha reads game assets and runtime-loaded code from
the player's disc; it does not require loose game executables or captured files.

If a private regeneration produces `generated/overlays_static.c`, CMake can
also link that optional static overlay. The current alpha does not use it.
Keep generated game translations, disc files, captures, and binaries outside
Git; publish runtime binaries as release assets rather than source commits.

## Incremental build and tests

After the initial setup, use the same compiler environment:

```sh
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Changes to `psxrecomp-overlay/` must be applied before rebuilding:

```sh
python tools/apply_framework_overlay.py --framework psxrecomp
```

The framework revision is pinned in `PSXRECOMP_PIN`, and the reviewed overlay
files are listed in `PSXRECOMP_OVERLAY_FILES.txt`. Runtime preferences live in
`settings.toml` beside the executable; game files, generated code, and build
directories are ignored by Git.
