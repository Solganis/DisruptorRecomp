#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One completed measurement window of gameplay frames (about one second). */
typedef struct DisruptorFrameRateWindow {
    uint32_t frames;
    uint32_t vblanks;
    uint32_t late_frames;
    uint32_t average_work_permille;
    uint32_t peak_work_permille;
} DisruptorFrameRateWindow;

int disruptor_frame_rate_unlocked(void);
void disruptor_frame_rate_set_unlocked(int enabled);

int disruptor_frame_rate_last_window(DisruptorFrameRateWindow *out);

#ifdef __cplusplus
}
#endif
