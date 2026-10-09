#include "gpu_ws_menu.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

constexpr int kStride = PSX_WS_MENU_WORDS;
constexpr int kGreen = 2, kRed = 1, kBlack = 3, kDim = 4, kAbove = 10, kBelow = 11;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

constexpr std::uint16_t colour(int red, int green, int blue) { return static_cast<std::uint16_t>(red | (green << 5) | (blue << 10)); }

/* A palette of greens from dark to light in entries 16..31, with a red, a black and a dim grey among the first. */
std::vector<std::uint16_t> palette() {
    std::vector<std::uint16_t> colours(256, colour(3, 6, 3));
    colours[kRed] = colour(28, 4, 4);
    colours[kGreen] = colour(5, 9, 5);
    colours[kBlack] = 0;
    colours[kDim] = colour(6, 7, 6);
    colours[kAbove] = colour(2, 8, 6);
    colours[kBelow] = colour(2, 24, 6);
    for (int step = 0; step < 16; ++step) {
        colours[16 + step] = colour(2, 8 + step, 6);
        colours[48 + step] = colour(6, 8 + step, 2); /* the same greens with red and blue changed over */
    }
    colours[200] = colour(20, 16, 2);
    return colours;
}

std::vector<std::uint16_t> picture_of(int index) { return std::vector<std::uint16_t>(kStride * PSX_WS_MENU_HIGH, static_cast<std::uint16_t>(index | (index << 8))); }

/* Box coordinates to the picture's. */
void put(std::vector<std::uint16_t> &picture, int x, int y, int index) {
    psx_ws_menu_set_texel(picture.data(), kStride, PSX_WS_MENU_LOGO_LEFT + x, PSX_WS_MENU_LOGO_TOP + y, index);
}

int at(const std::vector<std::uint16_t> &picture, int x, int y) {
    return psx_ws_menu_texel(picture.data(), kStride, PSX_WS_MENU_LOGO_LEFT + x, PSX_WS_MENU_LOGO_TOP + y);
}

void test_the_backdrop_is_two_rectangles() {
    expect(psx_ws_menu_backdrop_half(0, 0, 256, 240) == 1 && psx_ws_menu_backdrop_half(256, 0, 64, 240) == 2, "the left half is 256 wide, the right the other 64");
    expect(psx_ws_menu_backdrop_half(0, 1, 256, 240) == 0 && psx_ws_menu_backdrop_half(0, 0, 256, 239) == 0 && psx_ws_menu_backdrop_half(0, 0, 255, 240) == 0 &&
               psx_ws_menu_backdrop_half(256, 0, 63, 240) == 0 && psx_ws_menu_backdrop_half(64, 0, 256, 240) == 0 && psx_ws_menu_backdrop_half(256, 0, 256, 240) == 0,
           "a rectangle elsewhere or of another size is no half of it");
}

void test_a_word_holds_two_texels() {
    std::vector<std::uint16_t> picture(kStride * 2, 0x2211);
    expect(psx_ws_menu_texel(picture.data(), kStride, 0, 0) == 0x11 && psx_ws_menu_texel(picture.data(), kStride, 1, 0) == 0x22, "the low byte is the left texel");
    psx_ws_menu_set_texel(picture.data(), kStride, 4, 1, 0xAB);
    psx_ws_menu_set_texel(picture.data(), kStride, 7, 1, 0xCD);
    expect(picture[kStride + 2] == 0x22AB && picture[kStride + 3] == 0xCD11 && picture[kStride + 1] == 0x2211, "a texel is set without its neighbour");
}

void test_the_logo_is_told_by_its_colours() {
    expect(psx_ws_menu_logo_colour(colour(28, 4, 4)) && psx_ws_menu_logo_colour(colour(31, 31, 31)) && psx_ws_menu_logo_colour(colour(10, 10, 10)) &&
               psx_ws_menu_logo_colour(colour(3, 1, 1)) && psx_ws_menu_logo_colour(colour(10, 10, 9)),
           "red, white, grey and dark red are the logo's");
    expect(!psx_ws_menu_logo_colour(colour(5, 6, 4)) && !psx_ws_menu_logo_colour(colour(15, 19, 14)) && !psx_ws_menu_logo_colour(colour(4, 4, 4)) &&
               !psx_ws_menu_logo_colour(0) && !psx_ws_menu_logo_colour(colour(10, 10, 8)),
           "green, dark, and a grey gone yellow are the scene's");
    expect(psx_ws_menu_edge_colour(0) && psx_ws_menu_edge_colour(colour(4, 4, 4)) && psx_ws_menu_edge_colour(colour(6, 7, 6)) && !psx_ws_menu_edge_colour(colour(5, 9, 5)),
           "the edge is dark or nearly grey, and a plain green is neither");
    expect(psx_ws_menu_edge_colour(colour(24, 25, 24)) && psx_ws_menu_edge_colour(colour(26, 26, 23)) && psx_ws_menu_edge_colour(colour(16, 16, 4)) &&
               psx_ws_menu_edge_colour(colour(8, 8, 9)) && !psx_ws_menu_edge_colour(colour(6, 8, 6)),
           "the glint is grey or yellow, no greener than red by more than one");
    expect(!psx_ws_menu_edge_colour(colour(0, 1, 31)) && !psx_ws_menu_edge_colour(colour(8, 8, 10)) && psx_ws_menu_edge_colour(colour(0, 0, 12)) &&
               !psx_ws_menu_edge_colour(colour(0, 0, 13)),
           "and no bluer than green by more than one: a blue is the scene's unless it is dark");

    const std::vector<std::uint16_t> colours = palette();
    std::vector<std::uint16_t> picture = picture_of(kGreen);
    expect(!psx_ws_menu_has_logo(picture.data(), kStride, colours.data()), "a picture without red where the logo stands is not the front end's");
    for (int count = 0; count < PSX_WS_MENU_LOGO_LEAST - 1; ++count) put(picture, count % PSX_WS_MENU_LOGO_WIDE, count / PSX_WS_MENU_LOGO_WIDE, kRed);
    expect(!psx_ws_menu_has_logo(picture.data(), kStride, colours.data()), "one texel short of the least is not it either");
    put(picture, (PSX_WS_MENU_LOGO_LEAST - 1) % PSX_WS_MENU_LOGO_WIDE, (PSX_WS_MENU_LOGO_LEAST - 1) / PSX_WS_MENU_LOGO_WIDE, kRed);
    expect(psx_ws_menu_has_logo(picture.data(), kStride, colours.data()), "the least is");
    std::vector<std::uint16_t> elsewhere = picture_of(kGreen);
    for (int y = 60; y < 120; ++y)
        for (int x = 0; x < PSX_WS_MENU_WIDE; ++x) psx_ws_menu_set_texel(elsewhere.data(), kStride, x, y, kRed);
    expect(!psx_ws_menu_has_logo(elsewhere.data(), kStride, colours.data()), "red below the logo's rows does not count");
}

void test_the_mask_is_the_logo_and_its_edge() {
    const std::vector<std::uint16_t> colours = palette();
    std::vector<std::uint16_t> picture = picture_of(kGreen);
    std::vector<std::uint8_t> mask(PSX_WS_MENU_LOGO_ROWS * PSX_WS_MENU_LOGO_WIDE, 9);
    const auto marked = [&](int x, int y) { return mask[y * PSX_WS_MENU_LOGO_WIDE + x]; };
    put(picture, 20, 10, kRed);
    put(picture, 20 + PSX_WS_MENU_SHADOW, 10 + PSX_WS_MENU_SHADOW, kBlack);
    put(picture, 20 - PSX_WS_MENU_SHADOW, 10, kDim);
    put(picture, 20 + PSX_WS_MENU_SHADOW + 1, 10, kBlack);
    put(picture, 20 + PSX_WS_MENU_SHADOW + 1, 10 + PSX_WS_MENU_SHADOW, kBlack);
    put(picture, 20, 10 + PSX_WS_MENU_SHADOW + 1, kBlack);
    put(picture, 100, 0, kBlack);
    put(picture, 100, PSX_WS_MENU_LOGO_ROWS - 1, kBlack);
    put(picture, 0, 20, kBlack);
    put(picture, PSX_WS_MENU_LOGO_WIDE - 1, 20, kBlack);
    put(picture, 0, 0, kRed);
    put(picture, PSX_WS_MENU_LOGO_WIDE - 1, PSX_WS_MENU_LOGO_ROWS - 1, kRed);
    put(picture, PSX_WS_MENU_LOGO_WIDE - 2, PSX_WS_MENU_LOGO_ROWS - 1, kBlack);
    psx_ws_menu_logo_mask(picture.data(), kStride, colours.data(), mask.data());
    expect(marked(20, 10) == 1 && marked(0, 0) == 1 && marked(PSX_WS_MENU_LOGO_WIDE - 1, PSX_WS_MENU_LOGO_ROWS - 1) == 1, "a texel of the logo's colours is the logo, in the box's corners too");
    expect(marked(20 + PSX_WS_MENU_SHADOW, 10 + PSX_WS_MENU_SHADOW) == 2 && marked(20 - PSX_WS_MENU_SHADOW, 10) == 2 && marked(PSX_WS_MENU_LOGO_WIDE - 2, PSX_WS_MENU_LOGO_ROWS - 1) == 2,
           "a dark or dim texel within reach of it is its edge");
    expect(marked(20 + PSX_WS_MENU_SHADOW + 1, 10) == 0 && marked(20 + PSX_WS_MENU_SHADOW + 1, 10 + PSX_WS_MENU_SHADOW) == 0 && marked(20, 10 + PSX_WS_MENU_SHADOW + 1) == 0,
           "one past the reach is not, across or down, and an edge texel does not pass the reach on");
    expect(marked(100, 0) == 0 && marked(100, PSX_WS_MENU_LOGO_ROWS - 1) == 0 && marked(0, 20) == 0 && marked(PSX_WS_MENU_LOGO_WIDE - 1, 20) == 0,
           "a dark texel on the box's border with no logo near is scene, and nothing is read past the border for it");
    expect(marked(21, 10) == 0 && marked(19, 11) == 0 && marked(100, 20) == 0, "green beside the logo is scene");
    expect(std::count(mask.begin(), mask.end(), std::uint8_t{9}) == 0, "every texel of the box is decided");

    std::vector<std::uint16_t> kept(PSX_WS_MENU_LOGO_ROWS * PSX_WS_MENU_LOGO_WIDE, 0x1234);
    psx_ws_menu_keep_logo(picture.data(), kStride, colours.data(), mask.data(), kept.data());
    expect(kept[10 * PSX_WS_MENU_LOGO_WIDE + 20] == colours[kRed] && kept[10 * PSX_WS_MENU_LOGO_WIDE + 20 - PSX_WS_MENU_SHADOW] == colours[kDim], "the kept logo has its own colours");
    expect(kept[(10 + PSX_WS_MENU_SHADOW) * PSX_WS_MENU_LOGO_WIDE + 20 + PSX_WS_MENU_SHADOW] == 0x8000, "its black is kept with the top bit, or a rectangle would not draw it");
    expect(kept[10 * PSX_WS_MENU_LOGO_WIDE + 21] == 0 && kept[20 * PSX_WS_MENU_LOGO_WIDE + 100] == 0, "and nothing where the scene is");
}

void test_the_kept_logo_is_squashed_about_the_centre() {
    expect(PSX_WS_MENU_KEPT_PAGE == 0x108, "the kept logo is read from the page at 512 as colours, not as palette entries");
    std::int32_t left = 0, right = 0;
    psx_ws_menu_logo_span(160, 3, 4, &left, &right);
    expect(left == 84 && right == 240, "at 16:9 the logo's 207 columns are drawn in 156, about the centre");
    psx_ws_menu_logo_span(160, 4, 7, &left, &right);
    expect(left == 102 && right == 221, "at 21:9 in 119");
    psx_ws_menu_logo_span(160, 1, 1, &left, &right);
    expect(left == PSX_WS_MENU_LOGO_LEFT && right == PSX_WS_MENU_LOGO_RIGHT, "and where they were when nothing is squashed");
}

void test_the_logo_is_painted_over() {
    const std::vector<std::uint16_t> colours = palette();
    std::vector<std::uint16_t> picture = picture_of(kGreen);
    for (int y = 0; y < PSX_WS_MENU_LOGO_TOP; ++y)
        for (int x = 0; x < PSX_WS_MENU_WIDE; ++x) psx_ws_menu_set_texel(picture.data(), kStride, x, y, kAbove);
    for (int y = PSX_WS_MENU_LOGO_BOTTOM; y < PSX_WS_MENU_HIGH; ++y)
        for (int x = 0; x < PSX_WS_MENU_WIDE; ++x) psx_ws_menu_set_texel(picture.data(), kStride, x, y, kBelow);
    std::vector<std::uint8_t> mask(PSX_WS_MENU_LOGO_ROWS * PSX_WS_MENU_LOGO_WIDE, 0);
    for (int y = 0; y < PSX_WS_MENU_LOGO_ROWS; ++y)
        for (const int x : {0, 50, PSX_WS_MENU_LOGO_WIDE - 1}) {
            mask[y * PSX_WS_MENU_LOGO_WIDE + x] = 1;
            put(picture, x, y, kRed);
        }
    mask[5 * PSX_WS_MENU_LOGO_WIDE + 120] = 2;
    const std::vector<std::uint16_t> before = picture;
    std::vector<std::int16_t> nearest(PSX_WS_MENU_NEAREST, -1);
    psx_ws_menu_paint(picture.data(), kStride, colours.data(), mask.data(), nearest.data());

    const auto green_of = [&](int x, int y) { return (colours[at(picture, x, y)] >> 5) & 31; };
    bool rising = true, no_red = true;
    for (const int x : {0, 50, PSX_WS_MENU_LOGO_WIDE - 1}) {
        for (int y = 0; y < PSX_WS_MENU_LOGO_ROWS; ++y) {
            no_red = no_red && (colours[at(picture, x, y)] & 31) <= 3;
            if (y >= 4) rising = rising && green_of(x, y) + 1 >= green_of(x, y - 4);
        }
        expect(green_of(x, 0) <= 10 && green_of(x, PSX_WS_MENU_LOGO_ROWS - 1) >= 22, "a painted column starts as the scene above it and ends as the scene below");
        expect(green_of(x, PSX_WS_MENU_LOGO_ROWS / 2) >= 13 && green_of(x, PSX_WS_MENU_LOGO_ROWS / 2) <= 19, "and is halfway in the middle");
    }
    expect(rising && no_red, "the blend goes one way down the rows and takes no colour of the logo");
    bool blue_kept = true;
    for (int y = 0; y < PSX_WS_MENU_LOGO_ROWS; ++y) blue_kept = blue_kept && ((colours[at(picture, 50, y)] >> 10) & 31) == 6;
    expect(blue_kept, "and each part of the colour is blended as itself");
    expect(at(picture, 120, 5) != kGreen && green_of(120, 5) >= 9 && green_of(120, 5) <= 13, "a texel of the edge is painted like one of the logo");
    bool rest_untouched = true;
    for (int y = 0; y < PSX_WS_MENU_HIGH; ++y)
        for (int x = 0; x < PSX_WS_MENU_WIDE; ++x) {
            const int bx = x - PSX_WS_MENU_LOGO_LEFT, by = y - PSX_WS_MENU_LOGO_TOP;
            const bool masked = bx >= 0 && by >= 0 && bx < PSX_WS_MENU_LOGO_WIDE && by < PSX_WS_MENU_LOGO_ROWS && mask[by * PSX_WS_MENU_LOGO_WIDE + bx];
            if (!masked && psx_ws_menu_texel(picture.data(), kStride, x, y) != psx_ws_menu_texel(before.data(), kStride, x, y)) rest_untouched = false;
        }
    expect(rest_untouched, "no texel outside the mask is touched");

    std::vector<std::uint16_t> striped = before;
    for (int row = 0; row < PSX_WS_MENU_PAINT_FROM; ++row) psx_ws_menu_set_texel(striped.data(), kStride, PSX_WS_MENU_LOGO_LEFT + 50, PSX_WS_MENU_LOGO_TOP - 1 - row, kBelow);
    psx_ws_menu_paint(striped.data(), kStride, colours.data(), mask.data(), nearest.data());
    const int lone = (colours[at(striped, 50, 0)] >> 5) & 31;
    expect(lone >= 9 && lone <= 13, "one bright column above the logo is a fifth of the blend under it, not a stripe");
}

void test_the_nearest_entry_is_looked_up_once() {
    std::vector<std::uint16_t> colours = palette();
    std::vector<std::int16_t> nearest(PSX_WS_MENU_NEAREST, -1);
    expect(psx_ws_menu_nearest(colours.data(), nearest.data(), 2, 12, 6) == 20 && psx_ws_menu_nearest(colours.data(), nearest.data(), 27, 5, 5) == kRed &&
               psx_ws_menu_nearest(colours.data(), nearest.data(), 0, 0, 0) == kBlack,
           "the nearest entry is the one closest in all three parts");
    expect(nearest[2 | (12 << 5) | (6 << 10)] == 20 && std::count(nearest.begin(), nearest.end(), std::int16_t{-1}) == PSX_WS_MENU_NEAREST - 3,
           "and only what was asked is filled in");
    colours[20] = colour(31, 0, 31);
    expect(psx_ws_menu_nearest(colours.data(), nearest.data(), 2, 12, 6) == 20, "what was asked once is not looked up again");
    expect(psx_ws_menu_nearest(colours.data(), nearest.data(), 2, 12, 5) != 20, "a colour not asked before is looked up in the palette as it is");
    expect(psx_ws_menu_nearest(colours.data(), nearest.data(), 6, 12, 2) == 52, "and red is not taken for blue");
}

void test_what_was_put_is_given_back_word_by_word() {
    const std::vector<std::uint16_t> put = {11, 12, 13, 14, 15, 16}, before = {1, 2, 3, 4, 5, 6};
    std::vector<std::uint16_t> memory = {11, 12, 13, 77, 78, 14, 99, 16, 79, 80};
    expect(psx_ws_menu_return(memory.data(), 5, 3, 2, put.data(), before.data()) == 5, "every word still as it was put is counted");
    expect(memory == std::vector<std::uint16_t>({1, 2, 3, 77, 78, 4, 99, 6, 79, 80}),
           "it goes back to what it was, a word written since stays, and nothing beside the rectangle is touched");
    expect(psx_ws_menu_return(memory.data(), 5, 3, 2, put.data(), before.data()) == 0, "and nothing is given back twice");
}

void test_the_picture_and_the_kept_logo_are_reached() {
    expect(psx_ws_menu_reaches(PSX_WS_MENU_X, 0, PSX_WS_MENU_WORDS, PSX_WS_MENU_HIGH) && psx_ws_menu_reaches(PSX_WS_MENU_X + PSX_WS_MENU_WORDS - 1, PSX_WS_MENU_HIGH - 1, 1, 1) &&
               psx_ws_menu_reaches(0, 0, PSX_WS_MENU_X + 1, 1),
           "the picture is reached by its own rectangle, by its last word and from the left");
    expect(!psx_ws_menu_reaches(0, 0, PSX_WS_MENU_X, 480) && !psx_ws_menu_reaches(PSX_WS_MENU_X, PSX_WS_MENU_HIGH, PSX_WS_MENU_WORDS, 16) &&
               !psx_ws_menu_reaches(PSX_WS_MENU_X + PSX_WS_MENU_WORDS, 0, PSX_WS_MENU_KEPT_X - PSX_WS_MENU_X - PSX_WS_MENU_WORDS, PSX_WS_MENU_HIGH),
           "the frame buffers beside it, the rows under it and the gap before the kept logo are not it");
    expect(psx_ws_menu_reaches(PSX_WS_MENU_KEPT_X, 0, 1, 1) && psx_ws_menu_reaches(PSX_WS_MENU_KEPT_X + PSX_WS_MENU_LOGO_WIDE - 1, PSX_WS_MENU_LOGO_ROWS - 1, 1, 1) &&
               !psx_ws_menu_reaches(PSX_WS_MENU_KEPT_X + PSX_WS_MENU_LOGO_WIDE, 0, 64, 64) && !psx_ws_menu_reaches(PSX_WS_MENU_KEPT_X, PSX_WS_MENU_LOGO_ROWS, PSX_WS_MENU_LOGO_WIDE, 64),
           "the kept logo is reached in its own rows and columns only");
    expect(psx_ws_menu_is_picture(PSX_WS_MENU_X, 0, PSX_WS_MENU_WORDS, PSX_WS_MENU_HIGH) && !psx_ws_menu_is_picture(PSX_WS_MENU_X, 0, PSX_WS_MENU_WORDS, PSX_WS_MENU_HIGH - 1) &&
               !psx_ws_menu_is_picture(PSX_WS_MENU_X, 1, PSX_WS_MENU_WORDS, PSX_WS_MENU_HIGH) && !psx_ws_menu_is_picture(PSX_WS_MENU_X + 1, 0, PSX_WS_MENU_WORDS, PSX_WS_MENU_HIGH) &&
               !psx_ws_menu_is_picture(PSX_WS_MENU_X, 0, PSX_WS_MENU_WORDS - 1, PSX_WS_MENU_HIGH),
           "the whole picture is its own rectangle and no other");
    expect(psx_ws_menu_reaches(1000, 300, 100, 8) && psx_ws_menu_reaches(900, 500, 8, 40) && !psx_ws_menu_reaches(1000, 300, 24, 8) && !psx_ws_menu_reaches(900, 500, 8, 12),
           "and a rectangle that runs off video memory wraps round, so it may reach either");
}

void test_a_rectangle_keeps_its_shape_on_the_stretched_screen() {
    std::int32_t x = 110;
    expect(psx_ws_menu_squash_rect(&x, 24, 3, 4) == 18 && x == 122, "a rectangle is squashed about the centre, edge by edge");
    x = 110;
    expect(psx_ws_menu_squash_rect(&x, 24, 4, 7) == 14 && x == 131, "by the screen's own ratio");
    x = 0;
    expect(psx_ws_menu_squash_rect(&x, PSX_WS_MENU_WIDE, 3, 4) == PSX_WS_MENU_WIDE && x == 0, "one from side to side lies over the whole screen and is left");
    x = -8;
    expect(psx_ws_menu_squash_rect(&x, PSX_WS_MENU_WIDE + 16, 3, 4) == PSX_WS_MENU_WIDE + 16 && x == -8, "and so is one wider than the screen");
    x = 0;
    expect(psx_ws_menu_squash_rect(&x, PSX_WS_MENU_WIDE - 1, 3, 4) == 239 && x == 40, "one that stops short of the right edge is not");
    x = 1;
    expect(psx_ws_menu_squash_rect(&x, PSX_WS_MENU_WIDE, 3, 4) == 240 && x == 41, "nor one that starts after the left");
    x = 101;
    expect(psx_ws_menu_squash_rect(&x, 1, 3, 4) == 1 && x == 116, "a dot stays a dot where its edges fall on one column");
    x = 101;
    expect(psx_ws_menu_squash_rect(&x, 0, 3, 4) == 0 && x == 101, "and nothing stays nothing");
}

void test_a_polygon_is_squashed_away_from_its_own_middle() {
    std::int32_t frame[4] = {109, 135, 109, 135};
    psx_ws_menu_squash_corners(frame, 4, 3, 4);
    expect(frame[0] == 121 && frame[1] == 142 && frame[2] == 121 && frame[3] == 142, "a frame behind a picture squashed to 122..140 shows on both sides of it");
    std::int32_t wider[4] = {109, 135, 109, 135};
    psx_ws_menu_squash_corners(wider, 4, 4, 7);
    expect(wider[0] == 130 && wider[1] == 146, "and at another ratio, behind 131..145");
    std::int32_t right[3] = {201, 221, 211};
    psx_ws_menu_squash_corners(right, 3, 3, 4);
    expect(right[0] == 190 && right[1] == 206 && right[2] == 198,"right of the centre too, and a corner at its own middle goes to the nearest column");
    std::int32_t across[4] = {0, PSX_WS_MENU_WIDE, 0, PSX_WS_MENU_WIDE}, beyond[2] = {-4, PSX_WS_MENU_WIDE + 10}, short_of[2] = {0, PSX_WS_MENU_WIDE - 1};
    psx_ws_menu_squash_corners(across, 4, 3, 4);
    psx_ws_menu_squash_corners(beyond, 2, 3, 4);
    psx_ws_menu_squash_corners(short_of, 2, 3, 4);
    expect(across[0] == 0 && across[1] == PSX_WS_MENU_WIDE && beyond[0] == -4 && beyond[1] == PSX_WS_MENU_WIDE + 10, "one from side to side lies over the whole screen and is left");
    expect(short_of[0] == 40 && short_of[1] == 280, "one that stops short of an edge is not");
    std::int32_t alone[1] = {110};
    psx_ws_menu_squash_corners(alone, 1, 3, 4);
    expect(alone[0] == 122, "a single corner goes to the nearest column");
}

}  // namespace

int main() {
    test_the_backdrop_is_two_rectangles();
    test_a_word_holds_two_texels();
    test_the_logo_is_told_by_its_colours();
    test_the_mask_is_the_logo_and_its_edge();
    test_the_kept_logo_is_squashed_about_the_centre();
    test_the_logo_is_painted_over();
    test_the_nearest_entry_is_looked_up_once();
    test_what_was_put_is_given_back_word_by_word();
    test_the_picture_and_the_kept_logo_are_reached();
    test_a_rectangle_keeps_its_shape_on_the_stretched_screen();
    test_a_polygon_is_squashed_away_from_its_own_middle();
    if (g_failures) return 1;
    std::cout << "GPU widescreen front-end backdrop tests passed\n";
    return 0;
}
