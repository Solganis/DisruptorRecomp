#!/usr/bin/env python3
"""Source contract for the launcher's settings pages and language row."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Keys the game saves and the launcher leaves to the in-game menu, the command line or a hand edit.
NOT_OFFERED = {
    "video.renderer", "video.window_width", "video.antialiasing", "video.texture_filtering", "video.crt_filter",
    "video.auto_skip_fmv", "video.turbo_loads", "video.fast_boot", "video.bios_hle", "video.low_latency_input",
    "video.frame_interpolation_blend", "audio.spu_hq", "disruptor.high_precision_camera",
}
# Keys written together with a launcher row, or by the language row.
COVERED = {"video.adaptive_view", "disruptor.language_disc", "disruptor.language_discs"}
SHOWN_SECTIONS = ("video", "audio", "disruptor")
UTF8_PAGE = '<activeCodePage xmlns="http://schemas.microsoft.com/SMI/2019/WindowsSettings">UTF-8</activeCodePage>'


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def plain_values(text: str) -> dict[str, str]:
    values, section = {}, ""
    for line in text.splitlines():
        if header := re.fullmatch(r"\[(\w+)\]", line.strip()):
            section = header[1]
        elif pair := re.fullmatch(r"(\w+)\s*=\s*(\S.*)", line.strip()):
            values[f"{section}.{pair[1]}"] = pair[2]
    return values


table = read("src/launcher/launcher_settings.cpp")
window = read("src/launcher/main.cpp")
region = read("src/launcher/region_disc.cpp")
launcher_cmake = read("src/launcher/CMakeLists.txt")
cmake = read("CMakeLists.txt")
loader = read("psxrecomp-overlay/recompiler/src/config_loader.cpp")
menu = read("src/disruptor_dev_menu.cpp") + read("src/disruptor_present_rate.cpp")

rows = re.findall(r'\b(toggle|choice|slider)(?:<[^>]*>)?\(\s*"([\w.]+)", Page::(\w+), L"([^"]+)", (-?\d+)', table)
keys = [key for _, key, _, _, _ in rows]
initial = {key: int(value) for _, key, _, _, value in rows}
require(len(rows) == 21 and len(set(keys)) == 21, "the launcher's table must hold its 21 rows, each key once")

writer = loader[loader.index("bool save_user_settings(") :]
saved, section = set(), ""
for header, key in re.findall(r'f << "(?:\\n)?\[(\w+)\]\\n|f << "(\w+)\s*= ', writer):
    section = header or section
    if key and section in SHOWN_SECTIONS:
        saved.add(f"{section}.{key}")
require(len(saved) >= 34, "the settings writer was not read as expected")
undecided = saved - set(keys) - NOT_OFFERED - COVERED
require(not undecided, f"decide whether the launcher shows these saved keys: {sorted(undecided)}")
require(not set(keys) - saved, f"the launcher shows keys the game does not save: {sorted(set(keys) - saved)}")
require(
    not (NOT_OFFERED | COVERED) - saved,
    f"keys listed here are no longer saved: {sorted((NOT_OFFERED | COVERED) - saved)}",
)

for kind, key, _, label, _ in rows:
    require(f'"{label}"' in menu, f"the row {key} must be named as the in-game menu names it: {label}")
for label in (
    "Windowed", "Borderless fullscreen", "Exclusive fullscreen", "16:9", "21:9", "32:9", "Match window (up to 32:9)",
    "1x (native)", "8x", "Adaptive", "Immediate", "Synchronised",
):
    require(f'L"{label}"' in table and f'"{label}"' in menu, f"the choice {label} must read as in the in-game menu")
require('L"Vanilla", L"Improved"' in table and '"Vanilla\\0Improved\\0"' in menu, "the shadow choices must match")

shipped = plain_values(read("release/windows/settings.toml"))
for key, value in shipped.items():
    require(key in initial, f"the shipped settings.toml sets {key}, which the launcher does not show")
    require(initial[key] == {"true": 1, "false": 0}[value], f"the launcher's start value of {key} is not the shipped one")
game = plain_values(read("game.toml"))
require(
    game["video.supersampling"] == "4" and initial["video.supersampling"] == 3
    and game["video.aspect_ratio"] == '"4:3"' and initial["video.aspect_ratio"] == 0,
    "the launcher must start from game.toml's resolution scale and aspect ratio",
)
mouse = read("src/disruptor_mouse_aim.cpp")
require(
    "constexpr double kDefaultHorizontalSensitivity = 0.080;" in mouse
    and "constexpr double kDefaultVerticalSensitivity = 0.080;" in mouse
    and initial["disruptor.horizontal_sensitivity"] == 80 and initial["disruptor.vertical_sensitivity"] == 80,
    "the launcher must start from the game's mouse sensitivity",
)
require(
    initial["video.vsync"] == 2 and "static int           g_video_vsync        = 1;" in read("psxrecomp-overlay/runtime/src/main.cpp")
    and initial["disruptor.hud_scale"] == 100 and initial["audio.master_volume"] == 100,
    "the launcher must start from the runtime's own defaults",
)

require(
    '"${PSXRECOMP_ROOT}/recompiler/src/config_loader.cpp"' in launcher_cmake
    and '"${PROJECT_SOURCE_DIR}/src/disruptor_language_disc.cpp"' in launcher_cmake
    and "    Settings fresh = PSXRecompV4::load_user_settings(path_);" in table
    and "    if (!PSXRecompV4::save_user_settings(path_, fresh)) return false;" in table
    and "    if (!readable_) return false;\n    alter(fresh);" in table,
    "the launcher must read and write settings.toml with the game's code, and never over a file it could not read",
)
require(
    "    if (!known_disc(other, known))" in region and "    if (!laid_out.build(home, other, why))" in region,
    "a language disc must be checked with the game's own layout code",
)
require(
    "result->disc = task == Task::import ? import_disc(source, root, cancelled, report)" in window
    and "                    result->language = check_region_disc(home, source);" in window
    and "            } else if (app->settings.choose_language_disc(utf8(result->image))) {" in window
    and window.count("import_disc(") == 2,
    "a language disc is checked and named where it is, only the US disc is copied",
)
require(
    "                            check_region_disc(result->disc.data, from_utf8(spoken));" in window
    and "            if (!result->language_error.empty()) {" in window
    and 'L" --no-launcher --game "' in window
    and '        SetEnvironmentVariableW(L"PSX_DISRUPTOR_LANGUAGE_DISC", nullptr);' in window
    and window.count("PSX_DISRUPTOR_LANGUAGE_DISC") == 1,
    "the language disc must be checked again before the game starts, and reach the game through settings.toml alone",
)
worker = window[window.index("        worker = std::thread(") : window.index("    void take_image(")]
require(
    len(re.findall(r"(?<!check_)region_disc\(", window)) == 2
    and len(re.findall(r"(?<!check_)region_disc\(", worker)) == 2
    and window.count("check_region_disc(") == worker.count("check_region_disc(") == 2
    and "            for (const std::string& path : listed) result->regions.emplace(path, region_disc(from_utf8(path)));" in worker
    and "        app->regions = std::move(result->regions);" in window,
    "a disc image is opened on the worker only: one on a share that is gone must not stall the window",
)
require(
    "        if (launch) settings.load(); // the game reads the file, not what this window last saw of it\n" in window
    and '            } else if (app->cancelled) {\n                app->say(L"Cancelled. The language is as it was.");' in window
    and "            if (WaitForSingleObject(app->game, 0) == WAIT_OBJECT_0) {" in window
    and "code != STILL_ACTIVE" not in window,
    "Play must start from the file as it is, a cancelled check must change nothing, "
    "and the game's end is asked of its handle: 259 is an exit code too",
)
require(
    "                app->settings.load(); // the in-game menu writes the same file" in window
    and "    bool editable() const { return !busy && !game && settings.readable(); }" in window,
    "the launcher must not write settings while the game runs and must read them again after it",
)
require(
    "        DragAcceptFiles(window, TRUE);" in window and "    case WM_DROPFILES: {" in window
    and "    case WM_DPICHANGED: {" in window and "DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2" in window
    and 'log_tail(app->root / "startup.log", log_lines)' in window,
    "drag and drop, the per-monitor layout and the log tail must stay",
)
require(
    UTF8_PAGE in read("src/disruptor.manifest") and UTF8_PAGE in read("src/launcher/launcher.manifest")
    and '1 RT_MANIFEST "disruptor.manifest"' in read("src/disruptor.rc")
    and 'target_sources(psx-runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor.manifest")' in cmake
    and 'target_sources(psx-runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/disruptor.rc")' in cmake,
    "the game and the launcher must both run with UTF-8 as their code page, or a path outside it cannot be named",
)
require(
    "add_test(NAME disruptor_launcher_settings COMMAND disruptor-launcher-settings-test)" in launcher_cmake
    and "add_test(NAME disruptor_region_disc COMMAND disruptor-region-disc-test)" in launcher_cmake
    and "add_test(NAME disruptor_launcher_contract" in cmake,
    "the launcher's tests must remain registered",
)

print("Disruptor launcher source contract: PASS")
