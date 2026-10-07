#!/usr/bin/env python3
"""Source contract for the widescreen HUD size option."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


cmake = read("CMakeLists.txt")
manifest = read("PSXRECOMP_OVERLAY_FILES.txt")
gpu = read("psxrecomp-overlay/runtime/src/gpu.c")
header = read("psxrecomp-overlay/runtime/include/gpu.h")
menu = read("src/disruptor_dev_menu.cpp")
settings_loader = read("psxrecomp-overlay/recompiler/src/config_loader.cpp")

require(
    "+runtime/include/gpu_ws_hud_scale.h" in manifest.splitlines(),
    "overlay manifest must list the HUD scale arithmetic",
)
require(
    "add_test(NAME disruptor_gpu_ws_hud_scale\n" in cmake and "disruptor_hud_scale_contract" in cmake,
    "the HUD scale unit test and its contract must remain registered",
)
require(
    "void gpu_ws_set_hud_scale(int percent);" in header and "int gpu_ws_hud_scale(void);" in header,
    "the HUD size API must stay public",
)
require(
    "static int ws_hud_scale_percent = PSX_WS_HUD_SCALE_MAX;" in gpu
    and "    ws_hud_scale_percent = psx_ws_hud_scale_clamp(percent);" in gpu,
    "the HUD must start at its authored size and accept only a clamped size",
)
require(
    "    if (ws_hud_scale_percent >= PSX_WS_HUD_SCALE_MAX ||\n"
    "        !psx_ws_hud_scale_rect(x, y, &scaled_w, &scaled_h, ws_disp_w(), ws_disp_h(),\n"
    "                               ws_xnum, ws_xden, ws_hud_scale_percent))\n"
    "        return 0;" in gpu,
    "at the authored size the stock squash must stay in charge",
)
require(
    "            else if (ws_hud_sprt) {\n"
    "                int scaled_w = w, scaled_h = h;\n"
    "                if (ws_hud_user_scale(&x0, &y0, &scaled_w, &scaled_h)) {\n"
    "                    ws_w = scaled_w;\n"
    "                    ws_h = scaled_h;\n"
    "                } else {\n"
    "                    x0 = ws_scale_about(x0, ws_hud_pivot(x0, w));\n"
    "                    ws_w = (int)ws_scale_len(w);\n"
    "                }\n"
    "            }" in gpu,
    "an untagged textured rectangle must take the user's HUD size, else the stock squash",
)
require(
    "    if (ws_hud_sprt) {\n"
    "        int scaled_w = w, scaled_h = w;\n"
    "        if (ws_hud_user_scale(x0, y0, &scaled_w, &scaled_h)) {\n"
    "            *out_h = scaled_h;\n"
    "            return scaled_w;\n"
    "        }\n"
    "        *x0 = ws_scale_about(*x0, ws_hud_pivot(*x0, w));" in gpu,
    "fixed-size sprites must take the user's HUD size, else the stock squash",
)
require(
    gpu.count("ws_hud_user_scale(") == 3,
    "only untagged screen-space SPRT paths may apply the HUD size",
)
require(
    "        gr_draw_textured_rect_scaled(x0, y0, dw, dh, u0, v0, u0 + w, v0 + h," in gpu
    and "        gr_draw_textured_rect_scaled(x0, y0, dw, dh, u0, v0, u0 + 8, v0 + 8," in gpu
    and "        gr_draw_textured_rect_scaled(x0, y0, dw, dh, u0, v0, u0 + 16, v0 + 16," in gpu,
    "a scaled widget must keep its whole texture",
)

require(
    '    if (ImGui::SliderInt("HUD size", &hud_scale, 50, 100, "%d%%",\n'
    "                         ImGuiSliderFlags_AlwaysClamp)) {\n"
    "        gpu_ws_set_hud_scale(hud_scale);\n"
    "        mark_hud_scale(hud_scale);" in menu,
    "the settings menu must apply the size live and stage it for saving",
)
require(
    "    if (g_preferences.dirty & PREF_HUD_SCALE) {\n"
    "        settings.has_hud_scale = true;\n"
    "        settings.hud_scale = pending.hud_scale;" in menu,
    "a changed size must reach settings.toml",
)
require(
    "    if (settings.has_hud_scale) gpu_ws_set_hud_scale(settings.hud_scale);" in menu,
    "a saved size must be applied at start",
)
require(
    '            const int percent = toml::find<int>(d, "hud_scale");\n'
    "            if (percent >= 50 && percent <= 100) {"
    in settings_loader
    and '            f << "hud_scale = " << s.hud_scale << "\\n";' in settings_loader
    and "s.has_perspective_textures || s.has_hud_scale) {" in settings_loader,
    "settings.toml must read and write a valid [disruptor] hud_scale",
)

print("Disruptor HUD size source contract: PASS")
