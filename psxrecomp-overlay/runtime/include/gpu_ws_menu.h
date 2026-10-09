#ifndef PSXRECOMP_GPU_WS_MENU_H
#define PSXRECOMP_GPU_WS_MENU_H

#include <stdint.h>

#include "gpu_ws_screen_tile.h"

/* The front end's backdrop on a wide screen: a 320x240 picture of 256 colours that the game uploads to (320,0) every frame, logo and all, and draws as two rectangles. */
/* It is stretched to the screen's width with the text squashed first. The logo is lifted out of it and drawn over it squashed, so the logo is as wide as it was. */

enum {
    PSX_WS_MENU_X = 320, /* where the picture is in video memory, in words */
    PSX_WS_MENU_WORDS = 160,
    PSX_WS_MENU_WIDE = 320,
    PSX_WS_MENU_HIGH = 240,
    PSX_WS_MENU_LOGO_LEFT = 59, /* measured on the US disc: the logo and its shadow stand in columns 61..263 and rows 17..48 */
    PSX_WS_MENU_LOGO_RIGHT = 266,
    PSX_WS_MENU_LOGO_TOP = 15,
    PSX_WS_MENU_LOGO_BOTTOM = 51,
    PSX_WS_MENU_LOGO_WIDE = PSX_WS_MENU_LOGO_RIGHT - PSX_WS_MENU_LOGO_LEFT,
    PSX_WS_MENU_LOGO_ROWS = PSX_WS_MENU_LOGO_BOTTOM - PSX_WS_MENU_LOGO_TOP,
    PSX_WS_MENU_LOGO_LEAST = 2500, /* of the 3864 texels of its own colours the logo has there */
    PSX_WS_MENU_KEPT_X = 512,      /* empty in the front end and in play: measured in both */
    PSX_WS_MENU_KEPT_PAGE = (PSX_WS_MENU_KEPT_X / 64) | (2 << 7), /* colours as they are, no palette */
    PSX_WS_MENU_SHADOW = 3,        /* how far from its own colours the logo's edge reaches */
    PSX_WS_MENU_PAINT_FROM = 4,    /* rows above and below the logo's that the paint is mixed from */
    PSX_WS_MENU_NEAREST = 32 * 32 * 32, /* a palette entry per colour of five bits a part */
};

/* Which half of the backdrop a textured rectangle is: 1 the left, 2 the right, 0 neither. */
static inline int psx_ws_menu_backdrop_half(int32_t x, int32_t y, int w, int h) {
    if (y != 0 || h != PSX_WS_MENU_HIGH) return 0;
    if (x == 0 && w == 256) return 1;
    return x == 256 && w == PSX_WS_MENU_WIDE - 256 ? 2 : 0;
}

static inline int psx_ws_menu_texel(const uint16_t *picture, int stride, int x, int y) {
    const uint16_t word = picture[y * stride + (x >> 1)];
    return (x & 1) ? word >> 8 : word & 0xFF;
}

static inline void psx_ws_menu_set_texel(uint16_t *picture, int stride, int x, int y, int index) {
    uint16_t *word = &picture[y * stride + (x >> 1)];
    *word = (x & 1) ? (uint16_t)((*word & 0x00FF) | (index << 8)) : (uint16_t)((*word & 0xFF00) | index);
}

/* The logo is red, white and grey. The scenes behind it are green and dark: none of their 45 colours in fourteen pictures passes this. */
static inline int psx_ws_menu_logo_colour(uint16_t colour) {
    const int red = colour & 31, green = (colour >> 5) & 31, blue = (colour >> 10) & 31;
    return red > green || (red == green && blue >= green - 1 && red + green + blue > 12);
}

/* What the logo's edge is made of where it is not its own colours: its dark shadow, and its glint, at most one step greener than red or bluer than green. */
static inline int psx_ws_menu_edge_colour(uint16_t colour) {
    const int red = colour & 31, green = (colour >> 5) & 31, blue = (colour >> 10) & 31;
    return red + green + blue <= 12 || (red >= green - 1 && blue <= green + 1);
}

/* Whether the picture is the front end's: the logo stands where it always stands. */
static inline int psx_ws_menu_has_logo(const uint16_t *picture, int stride, const uint16_t *palette) {
    int count = 0;
    for (int y = PSX_WS_MENU_LOGO_TOP; y < PSX_WS_MENU_LOGO_BOTTOM; ++y)
        for (int x = PSX_WS_MENU_LOGO_LEFT; x < PSX_WS_MENU_LOGO_RIGHT; ++x)
            count += psx_ws_menu_logo_colour(palette[psx_ws_menu_texel(picture, stride, x, y)]);
    return count >= PSX_WS_MENU_LOGO_LEAST;
}

/* Marks the logo's texels in its box, PSX_WS_MENU_LOGO_WIDE a row: 1 for its own colours, 2 for its edge beside them. */
static inline void psx_ws_menu_logo_mask(const uint16_t *picture, int stride, const uint16_t *palette, uint8_t *mask) {
    for (int y = 0; y < PSX_WS_MENU_LOGO_ROWS; ++y)
        for (int x = 0; x < PSX_WS_MENU_LOGO_WIDE; ++x)
            mask[y * PSX_WS_MENU_LOGO_WIDE + x] =
                (uint8_t)psx_ws_menu_logo_colour(palette[psx_ws_menu_texel(picture, stride, PSX_WS_MENU_LOGO_LEFT + x, PSX_WS_MENU_LOGO_TOP + y)]);
    for (int y = 0; y < PSX_WS_MENU_LOGO_ROWS; ++y)
        for (int x = 0; x < PSX_WS_MENU_LOGO_WIDE; ++x) {
            uint8_t *one = &mask[y * PSX_WS_MENU_LOGO_WIDE + x];
            if (*one || !psx_ws_menu_edge_colour(palette[psx_ws_menu_texel(picture, stride, PSX_WS_MENU_LOGO_LEFT + x, PSX_WS_MENU_LOGO_TOP + y)])) continue;
            for (int dy = -PSX_WS_MENU_SHADOW; dy <= PSX_WS_MENU_SHADOW && !*one; ++dy)
                for (int dx = -PSX_WS_MENU_SHADOW; dx <= PSX_WS_MENU_SHADOW; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= PSX_WS_MENU_LOGO_WIDE || ny >= PSX_WS_MENU_LOGO_ROWS) continue;
                    if (mask[ny * PSX_WS_MENU_LOGO_WIDE + nx] == 1) *one = 2;
                }
        }
}

/* The logo alone, a word a texel in its own colours: nothing where the mask is clear, which a textured rectangle then leaves undrawn. */
static inline void psx_ws_menu_keep_logo(const uint16_t *picture, int stride, const uint16_t *palette, const uint8_t *mask, uint16_t *kept) {
    for (int y = 0; y < PSX_WS_MENU_LOGO_ROWS; ++y)
        for (int x = 0; x < PSX_WS_MENU_LOGO_WIDE; ++x) {
            const uint16_t colour = palette[psx_ws_menu_texel(picture, stride, PSX_WS_MENU_LOGO_LEFT + x, PSX_WS_MENU_LOGO_TOP + y)];
            kept[y * PSX_WS_MENU_LOGO_WIDE + x] = !mask[y * PSX_WS_MENU_LOGO_WIDE + x] ? 0 : colour ? colour : 0x8000; /* black is drawn only with its top bit set */
        }
}

/* Where the kept logo is drawn: its columns squashed about the screen's centre. *right is exclusive. */
static inline void psx_ws_menu_logo_span(int32_t centre, int32_t numerator, int32_t denominator, int32_t *left, int32_t *right) {
    *left = psx_ws_squash_about(PSX_WS_MENU_LOGO_LEFT, centre, numerator, denominator);
    *right = psx_ws_squash_about(PSX_WS_MENU_LOGO_RIGHT, centre, numerator, denominator);
}

/* The palette entry nearest a colour, looked up once: `nearest` holds PSX_WS_MENU_NEAREST entries, -1 where not asked yet, for this palette alone. */
static inline int psx_ws_menu_nearest(const uint16_t *palette, int16_t *nearest, int red, int green, int blue) {
    int16_t *known = &nearest[red | (green << 5) | (blue << 10)];
    if (*known >= 0) return *known;
    int32_t least = INT32_MAX;
    for (int index = 0; index < 256; ++index) {
        const int32_t r = (palette[index] & 31) - red, g = ((palette[index] >> 5) & 31) - green, b = ((palette[index] >> 10) & 31) - blue;
        if (r * r + g * g + b * b >= least) continue;
        least = r * r + g * g + b * b;
        *known = (int16_t)index;
    }
    return *known;
}

/* Paints the logo's texels in the picture with a blend of the scene above and below its rows, so the stretched logo does not show behind the kept one. */
/* The blend is of a 5x4 block on either side, or every column would be a stripe of its own, and is dithered into the palette. */
static inline void psx_ws_menu_paint(uint16_t *picture, int stride, const uint16_t *palette, const uint8_t *mask, int16_t *nearest) {
    for (int x = 0; x < PSX_WS_MENU_LOGO_WIDE; ++x) {
        int above[3] = {0, 0, 0}, below[3] = {0, 0, 0}, any = 0;
        for (int y = 0; y < PSX_WS_MENU_LOGO_ROWS; ++y) any |= mask[y * PSX_WS_MENU_LOGO_WIDE + x];
        if (!any) continue;
        for (int row = 0; row < PSX_WS_MENU_PAINT_FROM; ++row)
            for (int beside = -2; beside <= 2; ++beside) {
                const int at = PSX_WS_MENU_LOGO_LEFT + x + beside;
                const int column = at < 0 ? 0 : at >= PSX_WS_MENU_WIDE ? PSX_WS_MENU_WIDE - 1 : at;
                const uint16_t top = palette[psx_ws_menu_texel(picture, stride, column, PSX_WS_MENU_LOGO_TOP - 1 - row)];
                const uint16_t bottom = palette[psx_ws_menu_texel(picture, stride, column, PSX_WS_MENU_LOGO_BOTTOM + row)];
                for (int part = 0; part < 3; ++part) {
                    above[part] += (top >> (5 * part)) & 31;
                    below[part] += (bottom >> (5 * part)) & 31;
                }
            }
        for (int y = 0; y < PSX_WS_MENU_LOGO_ROWS; ++y) {
            if (!mask[y * PSX_WS_MENU_LOGO_WIDE + x]) continue;
            const int noise = (((x * 73 + y * 151) ^ (x * y)) & 15) - 8; /* half a step of a colour part either way, in no pattern an eye picks up */
            int wanted[3];
            for (int part = 0; part < 3; ++part) { /* sums of twenty five-bit parts, mixed by the row, in sixteenths of a step, then rounded to a step */
                const int fine = ((above[part] * (PSX_WS_MENU_LOGO_ROWS - y) + below[part] * (y + 1)) * 16) / (20 * (PSX_WS_MENU_LOGO_ROWS + 1)) + noise + 8;
                wanted[part] = fine < 0 ? 0 : fine > 31 * 16 ? 31 : fine / 16;
            }
            psx_ws_menu_set_texel(picture, stride, PSX_WS_MENU_LOGO_LEFT + x, PSX_WS_MENU_LOGO_TOP + y,
                                  psx_ws_menu_nearest(palette, nearest, wanted[0], wanted[1], wanted[2]));
        }
    }
}

/* On a front-end frame a rectangle keeps its shape on the stretched screen: its columns are squashed about the centre. Returns its width, 1 at least. */
static inline int psx_ws_menu_squash_rect(int32_t *x, int w, int32_t numerator, int32_t denominator) {
    if (w <= 0 || (*x <= 0 && *x + w >= PSX_WS_MENU_WIDE)) return w; /* one from side to side lies over the whole screen */
    const int32_t right = psx_ws_squash_about(*x + w, PSX_WS_MENU_WIDE / 2, numerator, denominator);
    *x = psx_ws_squash_about(*x, PSX_WS_MENU_WIDE / 2, numerator, denominator);
    return right > *x ? (int)(right - *x) : 1;
}

/* The same for a polygon's or a line's corners, rounded away from its own middle: it backs what is drawn over it and must show on both sides. */
static inline void psx_ws_menu_squash_corners(int32_t *xs, int count, int32_t numerator, int32_t denominator) {
    int32_t low = xs[0], high = xs[0];
    for (int i = 1; i < count; ++i) {
        if (xs[i] < low) low = xs[i];
        if (xs[i] > high) high = xs[i];
    }
    if (low <= 0 && high >= PSX_WS_MENU_WIDE) return;
    for (int i = 0; i < count; ++i) {
        const int32_t scaled = (xs[i] - PSX_WS_MENU_WIDE / 2) * numerator;
        const int32_t down = scaled >= 0 ? scaled / denominator : -((-scaled + denominator - 1) / denominator);
        const int32_t up = scaled >= 0 ? (scaled + denominator - 1) / denominator : -(-scaled / denominator);
        if (2 * xs[i] == low + high) xs[i] = psx_ws_squash_about(xs[i], PSX_WS_MENU_WIDE / 2, numerator, denominator);
        else xs[i] = PSX_WS_MENU_WIDE / 2 + (2 * xs[i] < low + high ? down : up);
    }
}

/* Gives a rectangle of video memory back word by word: a word that still is what was put there becomes what it was before, one written since stays. */
static inline int psx_ws_menu_return(uint16_t *memory, int stride, int w, int h, const uint16_t *put, const uint16_t *before) {
    int returned = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (memory[y * stride + x] != put[y * w + x]) continue;
            memory[y * stride + x] = before[y * w + x];
            ++returned;
        }
    return returned;
}

/* Whether a rectangle of video memory, in words, is the whole picture. */
static inline int psx_ws_menu_is_picture(int x, int y, int w, int h) {
    return x == PSX_WS_MENU_X && y == 0 && w == PSX_WS_MENU_WORDS && h == PSX_WS_MENU_HIGH;
}

/* Whether a rectangle of video memory, in words, reaches the picture or the kept logo. One that runs off video memory wraps round, so it may. */
static inline int psx_ws_menu_reaches(int x, int y, int w, int h) {
    if (x + w > 1024 || y + h > 512) return 1;
    if (x < PSX_WS_MENU_X + PSX_WS_MENU_WORDS && x + w > PSX_WS_MENU_X && y < PSX_WS_MENU_HIGH) return 1;
    return x < PSX_WS_MENU_KEPT_X + PSX_WS_MENU_LOGO_WIDE && x + w > PSX_WS_MENU_KEPT_X && y < PSX_WS_MENU_LOGO_ROWS;
}

#endif /* PSXRECOMP_GPU_WS_MENU_H */
