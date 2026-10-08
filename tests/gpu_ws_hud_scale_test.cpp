#include "gpu_ws_hud_scale.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

constexpr std::int32_t kWidth = 320;
constexpr std::int32_t kHeight = 240;

struct Rect {
    std::int32_t x, y, w, h;
};

struct Aspect {
    std::int32_t numerator, denominator;
};

/* 16:10, 16:9, 21:9 and 32:9 as gpu_ws_configure reduces them. */
constexpr std::array<Aspect, 4> kAspects{{{5, 6}, {3, 4}, {4, 7}, {3, 8}}};

/* The gameplay HUD of the first mission: health box with its caps and digits, ammunition box and digits. */
constexpr std::array<Rect, 10> kWidgets{{
    {12, 197, 56, 24}, {42, 200, 10, 15}, {31, 200, 10, 15}, {184, 24, 14, 20}, {120, 24, 14, 20},
    {134, 23, 50, 22}, {171, 27, 8, 12},  {160, 27, 10, 15}, {149, 27, 10, 15}, {138, 27, 10, 15},
}};
constexpr Rect kWeapon{128, 168, 60, 72};

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    if (g_failures <= 20) std::cerr << "FAIL: " << message << '\n';
}

bool scale(Rect &rect, const Aspect &aspect, int percent) {
    return psx_ws_hud_scale_rect(&rect.x, &rect.y, &rect.w, &rect.h, kWidth, kHeight, aspect.numerator,
                                 aspect.denominator, percent) != 0;
}

bool same(const Rect &a, const Rect &b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

void test_authored_size_and_bad_input_change_nothing() {
    for (const Aspect &aspect : kAspects)
        for (int percent : {100, 101, 49, 0, -30}) {
            Rect rect = kWidgets[0];
            expect(!scale(rect, aspect, percent) && same(rect, kWidgets[0]),
                   "the authored size and a size out of range leave a widget to the stock squash");
        }
    for (const Rect &bad : {Rect{12, 197, 0, 24}, Rect{12, 197, 56, 0}, Rect{12, 197, -4, 24}}) {
        Rect rect = bad;
        expect(!scale(rect, kAspects[1], 70) && same(rect, bad), "an empty widget is not scaled");
    }
    Rect rect = kWidgets[0];
    expect(!psx_ws_hud_scale_rect(&rect.x, &rect.y, &rect.w, &rect.h, kWidth, kHeight, 3, 0, 70) &&
               !psx_ws_hud_scale_rect(&rect.x, &rect.y, &rect.w, &rect.h, 0, kHeight, 3, 4, 70) &&
               same(rect, kWidgets[0]),
           "a broken squash factor or screen size changes nothing");
    expect(psx_ws_hud_scale_clamp(10) == 50 && psx_ws_hud_scale_clamp(75) == 75 && psx_ws_hud_scale_clamp(400) == 100,
           "a requested size is clamped to 50..100");
}

void test_weapon_column_is_left_alone() {
    for (const Aspect &aspect : kAspects)
        for (int percent = 50; percent < 100; percent += 7) {
            Rect weapon = kWeapon;
            expect(!scale(weapon, aspect, percent) && same(weapon, kWeapon), "the weapon keeps its size");
            for (std::int32_t y : {80, 100, 140, 200})
                for (std::int32_t x : {110, 150, 190}) {
                    Rect effect{x, y, 16, 16};
                    const Rect before = effect;
                    expect(!scale(effect, aspect, percent) && same(effect, before),
                           "whatever is drawn in the middle column below the top third keeps its size");
                }
        }
}

void test_widgets_shrink_toward_their_own_edge() {
    for (const Aspect &aspect : kAspects)
        for (int percent = 50; percent < 100; percent += 5) {
            Rect ammo = kWidgets[0];
            expect(scale(ammo, aspect, percent), "the ammunition box is scaled");
            expect(ammo.x >= 0 && ammo.x <= kWidgets[0].x && ammo.y >= kWidgets[0].y &&
                       ammo.y + ammo.h <= kHeight && ammo.w < kWidgets[0].w && ammo.h < kWidgets[0].h,
                   "a bottom-left widget moves toward the bottom-left corner and stays on screen");
            Rect health = kWidgets[5];
            expect(scale(health, aspect, percent), "the health box is scaled");
            expect(health.y <= kWidgets[5].y && health.y >= 0 && health.h < kWidgets[5].h &&
                       2 * health.x + health.w >= kWidth - 4 && 2 * health.x + health.w <= kWidth + 4,
                   "a top-centre widget moves up and stays centred");
            Rect corner{300, 4, 16, 16};
            expect(scale(corner, aspect, percent) && corner.x + corner.w <= kWidth && corner.x >= 300 &&
                       corner.y <= 4 && corner.y >= 0,
                   "a top-right widget moves toward the top-right corner");
            Rect side{4, 112, 16, 16};
            expect(scale(side, aspect, percent) && side.x <= 4 && side.x >= 0 &&
                       2 * side.y + side.h >= kHeight - 2 && 2 * side.y + side.h <= kHeight + 2,
                   "a widget at mid height keeps its height on screen");
        }
}

void test_neighbours_keep_touching() {
    for (const Aspect &aspect : kAspects)
        for (int percent = 50; percent < 100; ++percent) {
            /* Left cap, box and right cap of the health display share two vertical edges. */
            Rect left = kWidgets[4], box{134, 24, 50, 20}, right = kWidgets[3];
            scale(left, aspect, percent);
            scale(box, aspect, percent);
            scale(right, aspect, percent);
            expect(left.x + left.w == box.x && box.x + box.w == right.x && left.y == box.y && left.h == box.h,
                   "widgets that shared an edge share it at every size");
            Rect upper{20, 190, 30, 10}, lower{20, 200, 30, 10};
            scale(upper, aspect, percent);
            scale(lower, aspect, percent);
            expect(upper.y + upper.h == lower.y && upper.x == lower.x && upper.w == lower.w,
                   "widgets stacked on one another stay stacked");
            /* Not a promise: the same edge under two pivots, and an edge behind a one pixel minimum. */
            Rect in_left{90, 10, 20, 10}, in_middle{110, 10, 20, 10};
            scale(in_left, aspect, percent);
            scale(in_middle, aspect, percent);
            expect(in_left.x + in_left.w < in_middle.x,
                   "widgets that centre in different thirds are pulled apart: one pivot is the limit of the rule");
            Rect speck{3, 3, 1, 1}, beside{4, 3, 1, 1};
            scale(speck, aspect, percent);
            scale(beside, aspect, percent);
            expect(speck.w == 1 && beside.w == 1 && speck.x + speck.w >= beside.x,
                   "one pixel widgets keep their pixel and may overlap, they never leave a hole");
            Rect thin{6, 6, 1, 1};
            expect(scale(thin, aspect, percent) && thin.w == 1 && thin.h == 1, "nothing shrinks away to no pixels");
        }
}

void test_exact_values() {
    Rect ammo = kWidgets[0];
    scale(ammo, kAspects[1], 50);
    expect(same(ammo, Rect{5, 218, 21, 12}), "16:9 at 50%: the ammunition box is 3/8 as wide and half as tall");
    Rect health = kWidgets[5];
    scale(health, kAspects[1], 80);
    expect(same(health, Rect{144, 18, 30, 18}), "16:9 at 80%: the health box keeps its centre column");
    Rect straddling{100, 4, 20, 10};
    scale(straddling, kAspects[1], 50);
    expect(straddling.x == 137, "a widget belongs to the third its centre is in, not its left edge");
    Rect low{4, 76, 10, 10};
    scale(low, kAspects[1], 50);
    expect(low.y == 98, "a widget belongs to the third its centre is in, not its top edge");
    expect(psx_ws_hud_scale_edge(10, 0, 1, 2) == 5 && psx_ws_hud_scale_edge(11, 0, 1, 2) == 6 &&
               psx_ws_hud_scale_edge(-11, 0, 1, 2) == -6 && psx_ws_hud_scale_edge(150, 160, 3, 4) == 152,
           "an edge rounds to nearest on both sides of its pivot");
}

}  // namespace

int main() {
    test_authored_size_and_bad_input_change_nothing();
    test_weapon_column_is_left_alone();
    test_widgets_shrink_toward_their_own_edge();
    test_neighbours_keep_touching();
    test_exact_values();
    if (g_failures) return 1;
    std::cout << "GPU widescreen HUD scale tests passed\n";
    return 0;
}
