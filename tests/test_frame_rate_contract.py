#!/usr/bin/env python3
"""Version and codegen contract for Disruptor's gameplay frame-rate unlock."""

import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOAD_ADDRESS = 0x80011200
ROM_TEXT_OFFSET = 0x800

RETAIL_INSTRUCTIONS = {
    0x80043A50: 0x8F8202EC,
    0x80043A60: 0x938305A4,
    0x80043A80: 0x93830310,
    0x80043A98: 0x2A220006,
    0x80043AA4: 0x0C00E514,
    0x80043AAC: 0x0235102A,
    0x80043E7C: 0x0C012CF5,
    0x80043E80: 0x00002021,
    0x80043E84: 0x0C012CF5,
    0x80043E88: 0x2404FFFF,
    0x80043E8C: 0x00561023,
    0x80043E90: 0x28420002,
    0x80043E94: 0x10400003,
    0x80043E9C: 0x0C012CF5,
    0x80043EAC: 0x0056A823,
    0x80043EB0: 0x0040B021,
    0x8004B3D4: 0x3C028006,
    0x8004B414: 0x3C028006,
    0x8004B418: 0x8C42B920,
}


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


cmake = read("CMakeLists.txt")
source = read("src/disruptor_frame_rate.cpp")
header = read("src/disruptor_frame_rate.h")
menu = read("src/disruptor_dev_menu.cpp")
manifest = read("PSXRECOMP_OVERLAY_FILES.txt")
cycle_header = read("psxrecomp-overlay/runtime/include/psx_cyc.h")
cycle_source = read("psxrecomp-overlay/runtime/src/psx_cycles.c")

require(
    'tests/test_frame_rate_contract.py"\n            --require-artifacts)' in cmake,
    "the build gate must run this contract with its inputs required",
)
require(
    "src/disruptor_frame_rate.cpp" in cmake
    and "disruptor_frame_rate_contract" in cmake
    and "add_test(NAME disruptor_frame_rate\n" in cmake,
    "frame-rate source, its unit test and its contract test must remain registered",
)

for name in ("game.toml", "game-widescreen.toml"):
    config = read(name)
    entries = re.search(r"mod_function_entry_funcs\s*=\s*\[([^\]]*)\]", config)
    require(
        entries is not None and '"0x8004B3D4"' in entries.group(1),
        f"{name} must hook the VSync entry that restores the retail floor",
    )
    patch = re.search(
        r'\[\[recompiler\.patch\]\]\s*id = "disruptor-gameplay-frame-floor"\s*'
        r'address = "0x80043E90"\s*expected = "0x28420002"\s*replacement = "0x28420001"',
        config,
    )
    require(patch is not None, f"{name} must carry the guarded frame-floor patch")

for token in (
    "0x8004B3D4u",
    "0x80043E84u",
    "0x800438FCu",
    "0x800442F4u",
    "0x8005B920u",
    "0x80071438u",
    "0x800716F0u",
    "0x8007145Cu",
):
    require(token in source, f"reviewed frame-rate identity missing: {token}")

require(
    "std::atomic<bool> g_unlocked{false}" in source,
    "the retail 30 FPS cadence must stay the default",
)
require(
    "if (!unlocked) cpu->gpr[4] = kRetailFloorVBlanks;" in source and "kRetailFloorVBlanks = 2u" in source,
    "the locked path must restore the retail two-VBlank floor",
)
require(
    "simulation_follows_elapsed_vblanks()" in source
    and "kAttractLoopMode" in source
    and "kSingleStepPlayerState" in source
    and "kFixedStepOverride" in source,
    "fixed-step loop states must keep the retail cadence",
)
require(
    "static_cast<int32_t>(cpu->gpr[4]) >= 0" in source and "g_psx_cyc_overclock_shift = 0u;" in source,
    "every waiting VSync call must run at the stock instruction cost",
)
require(
    'g_unlocked.store(env_enabled("PSX_DISRUPTOR_FRAME_UNLOCK"),' in source,
    "the environment must be able to switch the unlock on at start",
)
require("psx_mod_write" not in source, "the unlock must not write guest memory")

for token in (
    "disruptor_frame_rate_unlocked",
    "disruptor_frame_rate_set_unlocked",
    "disruptor_frame_rate_last_window",
):
    require(token in header and token in source, f"frame-rate API is missing {token}")

require(
    "disruptor_frame_rate_set_unlocked(unlocked ? 1 : 0);\n        mark_frame_unlock(unlocked);" in menu
    and '"60 FPS gameplay (experimental)"' in menu,
    "the settings menu must apply the switch live and stage it for saving",
)
require(
    "if (g_preferences.dirty & PREF_FRAME_UNLOCK) {\n"
    "        settings.has_frame_unlock = true;\n"
    "        settings.frame_unlock = pending.frame_unlock;" in menu,
    "a changed switch must reach settings.toml",
)
require(
    "if (settings.has_frame_unlock &&\n"
    '        !env_override_present("PSX_DISRUPTOR_FRAME_UNLOCK"))\n'
    "        disruptor_frame_rate_set_unlocked(settings.frame_unlock ? 1 : 0);" in menu,
    "a saved switch must be applied at start unless the environment overrides it",
)
settings_loader = read("psxrecomp-overlay/recompiler/src/config_loader.cpp")
require(
    's.frame_unlock = toml::find<bool>(d, "frame_unlock");' in settings_loader
    and 'f << "frame_unlock = " << (s.frame_unlock ? "true" : "false")' in settings_loader
    and "s.has_perspective_textures || s.has_frame_unlock ||" in settings_loader,
    "settings.toml must read and write [disruptor] frame_unlock",
)

for path in ("runtime/include/psx_cyc.h", "runtime/src/psx_cycles.c"):
    require(path in manifest.splitlines(), f"overlay manifest must list {path}")
require(
    "g_psx_cyc_overclock_shift" in cycle_header and "uint32_t g_psx_cyc_overclock_shift = 0;" in cycle_source,
    "the guest CPU divisor must default to faithful timing",
)
require(
    "    if (g_psx_cyc_overclock_shift) {\n"
    "        const uint32_t total = g_psx_cyc_overclock_carry + cycles;\n"
    "        g_psx_cyc_overclock_carry =\n"
    "            total & ((1u << g_psx_cyc_overclock_shift) - 1u);\n"
    "        cycles = total >> g_psx_cyc_overclock_shift;\n"
    "        if (cycles == 0u) return;\n"
    "    }\n" in cycle_header,
    "the divisor must charge whole cycles and carry the remainder",
)

image_path = ROOT / "input" / "SLUS_002.24.code"
if image_path.exists():
    image = image_path.read_bytes()
    for address, instruction in RETAIL_INSTRUCTIONS.items():
        offset = ROM_TEXT_OFFSET + address - LOAD_ADDRESS
        require(0 <= offset <= len(image) - 4, f"retail instruction outside image: 0x{address:08X}")
        require(
            struct.unpack_from("<I", image, offset)[0] == instruction,
            f"retail frame-loop identity changed at 0x{address:08X}",
        )

generated = list((ROOT / "generated").glob("SLUS_002.24.code_full_*.c"))
if generated:
    text = "".join(path.read_text(encoding="utf-8") for path in generated)
    require(
        text.count("psx_mod_function_entry(cpu, 0x8004B3D4u)") == 1,
        "generated guest code must contain exactly one VSync entry hook",
    )
    require(
        text.count("/* 0x80043E90: 0x28420001 */") == 1,
        "generated guest code must contain the patched frame-floor compare",
    )

checked = ", ".join(
    f"{name} {'checked' if present else 'absent'}"
    for name, present in (("retail image", image_path.exists()), ("generated code", bool(generated)))
)
require(
    "--require-artifacts" not in sys.argv[1:] or (image_path.exists() and bool(generated)),
    f"the build gate must check the retail image and the generated code: {checked}",
)
print(f"Disruptor frame-rate source/codegen contract: PASS ({checked})")
