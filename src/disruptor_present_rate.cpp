/*
 * The menu control for the rate of Disruptor's in-between frames. Built only
 * together with the frame interpolator: a default build has neither.
 */

#include "disruptor_present_rate.h"

#include "host_ui.h"
#include "imgui.h"

#include <cstdint>
#include <cstdlib>

extern "C" void gl_renderer_interpolation_diag(int *enabled, int *suspended, int *history_frames,
                                               double *host_hz, double *target_hz, uint64_t *swaps);

int disruptor_present_rate_apply(int frames_per_second) {
    int enabled = 0, blend = 0;
    psx_host_video_get_interpolation(&enabled, nullptr, &blend);
    /* Switching in-between frames on stays the launcher's business. */
    return enabled && psx_host_video_set_interpolation(1, frames_per_second, blend) != 0;
}

int disruptor_present_rate_control(int *accepted) {
    int enabled = 0, target = 0, changed = 0;
    double display_hz = 0.0;
    psx_host_video_get_interpolation(&enabled, &target, nullptr);
    gl_renderer_interpolation_diag(nullptr, nullptr, nullptr, &display_hz, nullptr, nullptr);
    const int top = disruptor_present_rate_top(display_hz);
    int shown = disruptor_present_rate_shown(target, top);
    if (!enabled) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::SliderInt("In-between frame rate", &shown, kDisruptorPresentRateLowest, top, "%d FPS",
                         ImGuiSliderFlags_AlwaysClamp) &&
        disruptor_present_rate_apply(shown)) {
        if (accepted) *accepted = shown;
        changed = 1;
    }
    if (!enabled) ImGui::EndDisabled();
    if (!enabled)
        ImGui::TextDisabled("In-between frames are off in this session.");
    else if (std::getenv("PSX_FRAME_INTERPOLATION_FPS"))
        ImGui::TextDisabled(
            "Pictures shown per second. Applied now and saved, but "
            "PSX_FRAME_INTERPOLATION_FPS of the launcher replaces it at the next start.");
    else
        ImGui::TextDisabled(
            "Pictures shown per second. The top is the display's refresh rate, rounded, or 120 when "
            "that rate is lower, not known or outside the host's range. Applied now and saved.");
    return changed;
}
