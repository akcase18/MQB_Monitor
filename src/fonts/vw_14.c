/*******************************************************************************
 * Size: 14 px
 * Bpp: 2
 * Opts: 
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "../lvgl/lvgl.h"
#endif

#ifndef VW_14
#define VW_14 1
#endif

#if VW_14

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */

    /* U+0021 "!" */
    0x99, 0x99, 0x99, 0x40, 0x99,

    /* U+0022 "\"" */
    0xdc, 0xcc, 0xcc, 0x88,

    /* U+0023 "#" */
    0x1, 0x86, 0x0, 0x92, 0x40, 0x34, 0xd0, 0x7f,
    0xfd, 0x7, 0x1c, 0x2, 0x8a, 0x0, 0xd3, 0x41,
    0xff, 0xf4, 0x1c, 0x70, 0x9, 0x24, 0x0,

    /* U+0024 "$" */
    0x9, 0x2, 0xfc, 0xb9, 0x4d, 0x90, 0xa9, 0x3,
    0xe0, 0xb, 0xc0, 0x9e, 0x9, 0xa9, 0xad, 0x2f,
    0x40, 0x90, 0x4, 0x0,

    /* U+0025 "%" */
    0x2e, 0x1, 0x83, 0x24, 0x30, 0x61, 0x49, 0x3,
    0x25, 0xc0, 0x2e, 0x30, 0x0, 0xa, 0x7c, 0x1,
    0xcc, 0x60, 0x34, 0xc2, 0xa, 0xc, 0x60, 0xc0,
    0x7c,

    /* U+0026 "&" */
    0x7, 0xd0, 0x3, 0x4c, 0x1, 0xc3, 0x40, 0x35,
    0xc0, 0x7, 0xc0, 0x3, 0xb0, 0xc3, 0x4b, 0xb0,
    0xc0, 0xb0, 0x38, 0x7e, 0x3, 0xf4, 0xd0,

    /* U+0027 "'" */
    0xcc, 0xc8,

    /* U+0028 "(" */
    0x0, 0xc, 0x1c, 0x28, 0x34, 0x30, 0x30, 0x30,
    0x30, 0x30, 0x34, 0x28, 0x1c, 0xc, 0x0,

    /* U+0029 ")" */
    0x0, 0x20, 0x30, 0x28, 0x1c, 0xc, 0xc, 0xc,
    0xc, 0xc, 0x1c, 0x28, 0x30, 0x20, 0x0,

    /* U+002A "*" */
    0x5, 0x3, 0xfc, 0x1f, 0x42, 0xa8, 0x5, 0x0,

    /* U+002C "," */
    0x20, 0xc3, 0x18,

    /* U+002E "." */
    0x24, 0xd0,

    /* U+002F "/" */
    0x1, 0x80, 0xd0, 0x30, 0x18, 0xd, 0x3, 0x1,
    0x80, 0xd0, 0x30, 0x18, 0x0,

    /* U+0030 "0" */
    0xb, 0xe0, 0x28, 0x74, 0x30, 0x1c, 0x70, 0xc,
    0x70, 0xc, 0x70, 0xc, 0x70, 0xc, 0x30, 0x1c,
    0x28, 0x74, 0xb, 0xe0,

    /* U+0031 "1" */
    0x2e, 0x6, 0xa0, 0xa, 0x0, 0xa0, 0xa, 0x0,
    0xa0, 0xa, 0x0, 0xa0, 0xa, 0xb, 0xfe,

    /* U+0032 "2" */
    0x1f, 0xd0, 0x91, 0xc0, 0x3, 0x40, 0xd, 0x0,
    0x30, 0x3, 0x80, 0x38, 0x2, 0xc0, 0x2c, 0x0,
    0xff, 0xe0,

    /* U+0033 "3" */
    0x1f, 0xd0, 0x41, 0xc0, 0x3, 0x40, 0xc, 0xf,
    0xe0, 0x1, 0xc0, 0x2, 0x80, 0xa, 0x20, 0x70,
    0xbf, 0x40,

    /* U+0034 "4" */
    0x3, 0x0, 0x7, 0x0, 0xd, 0x0, 0xc, 0x30,
    0x28, 0x30, 0x34, 0x30, 0x70, 0x30, 0xbf, 0xfc,
    0x0, 0x30, 0x0, 0x30,

    /* U+0035 "5" */
    0xf, 0xf4, 0x30, 0x1, 0xc0, 0x6, 0x0, 0x1f,
    0xd0, 0x1, 0xd0, 0x2, 0x80, 0xa, 0x10, 0x70,
    0xbf, 0x40,

    /* U+0036 "6" */
    0x2, 0x80, 0xc, 0x0, 0x90, 0x3, 0xf8, 0x2c,
    0x74, 0xc0, 0x73, 0x1, 0xcc, 0x7, 0x2c, 0x74,
    0x2f, 0x40,

    /* U+0037 "7" */
    0xff, 0xe0, 0xd, 0x0, 0xc0, 0x28, 0x3, 0x40,
    0x30, 0xa, 0x0, 0xd0, 0xc, 0x2, 0x80,

    /* U+0038 "8" */
    0xb, 0xe0, 0xa0, 0xd3, 0x42, 0x8a, 0xd, 0xf,
    0xe0, 0xa1, 0xd3, 0x1, 0xcc, 0x7, 0x38, 0x38,
    0x2f, 0x80,

    /* U+0039 "9" */
    0xb, 0xd0, 0xb1, 0xd3, 0x2, 0xcc, 0x7, 0x30,
    0x18, 0xe0, 0xd0, 0xff, 0x0, 0x24, 0x1, 0xc0,
    0xd, 0x0,

    /* U+003A ":" */
    0xa9, 0x0, 0x9, 0xa0,

    /* U+003B ";" */
    0x28, 0x50, 0x0, 0x0, 0x53, 0x4c, 0x30,

    /* U+003C "<" */
    0x0, 0x0, 0x0, 0xa0, 0x2d, 0xb, 0x40, 0x78,
    0x0, 0x2e, 0x0, 0xb, 0x80, 0x1,

    /* U+003E ">" */
    0x0, 0x0, 0x38, 0x0, 0xb, 0x80, 0x0, 0xb8,
    0x0, 0x2c, 0x6, 0xd0, 0x3d, 0x0, 0x10, 0x0,

    /* U+003F "?" */
    0x2f, 0x41, 0x1c, 0x0, 0xd0, 0xd, 0x3, 0x80,
    0xa0, 0x8, 0x0, 0x0, 0x8, 0x0, 0xd0,

    /* U+0040 "@" */
    0x0, 0xbf, 0xd0, 0x7, 0x80, 0x28, 0xc, 0x0,
    0xd, 0x28, 0x3e, 0xc7, 0x30, 0xe1, 0xc3, 0x31,
    0xc1, 0xc3, 0x31, 0x82, 0x87, 0x31, 0xd7, 0x8d,
    0x34, 0xb9, 0xf4, 0x1c, 0x0, 0x0, 0xb, 0x40,
    0x0, 0x1, 0xbf, 0x40,

    /* U+0041 "A" */
    0x2, 0xc0, 0x0, 0xf4, 0x0, 0x7e, 0x0, 0x2d,
    0xc0, 0xd, 0x30, 0x7, 0xe, 0x2, 0xc1, 0xc0,
    0xff, 0xf0, 0x30, 0xe, 0x28, 0x1, 0xc0,

    /* U+0042 "B" */
    0xbf, 0xc2, 0x82, 0xca, 0x3, 0x28, 0xc, 0xa0,
    0x72, 0xff, 0x4a, 0x7, 0x68, 0x9, 0xa0, 0x36,
    0xff, 0x40,

    /* U+0043 "C" */
    0x2, 0xf8, 0xd, 0x4, 0x28, 0x0, 0x34, 0x0,
    0x30, 0x0, 0x30, 0x0, 0x34, 0x0, 0x28, 0x0,
    0xd, 0x4, 0x3, 0xf8,

    /* U+0044 "D" */
    0xbf, 0xd0, 0xa0, 0x74, 0xa0, 0x1c, 0xa0, 0xc,
    0xa0, 0xc, 0xa0, 0xc, 0xa0, 0xc, 0xa0, 0x1c,
    0xa0, 0x74, 0xbf, 0xd0,

    /* U+0045 "E" */
    0xbf, 0xfa, 0x0, 0xa0, 0xa, 0x0, 0xa0, 0xb,
    0xfd, 0xa0, 0xa, 0x0, 0xa0, 0xb, 0xff,

    /* U+0046 "F" */
    0xbf, 0xfa, 0x0, 0xa0, 0xa, 0x0, 0xa0, 0xb,
    0xfd, 0xa0, 0xa, 0x0, 0xa0, 0xa, 0x0,

    /* U+0047 "G" */
    0x7, 0xf8, 0xe, 0x5, 0x28, 0x0, 0x34, 0x0,
    0x30, 0x0, 0x30, 0x3e, 0x34, 0x7, 0x28, 0x7,
    0xd, 0xb, 0x7, 0xf8,

    /* U+0048 "H" */
    0xa0, 0xc, 0xa0, 0xc, 0xa0, 0xc, 0xa0, 0xc,
    0xa0, 0xc, 0xbf, 0xfc, 0xa0, 0xc, 0xa0, 0xc,
    0xa0, 0xc, 0xa0, 0xc,

    /* U+0049 "I" */
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa,

    /* U+004A "J" */
    0x0, 0xc0, 0x30, 0xc, 0x3, 0x0, 0xc0, 0x30,
    0xc, 0x7, 0x52, 0x8f, 0x80,

    /* U+004B "K" */
    0xa0, 0x32, 0x83, 0x4a, 0x28, 0x29, 0xc0, 0xac,
    0x2, 0xf0, 0xa, 0xb0, 0x28, 0xe0, 0xa0, 0xe2,
    0x80, 0xd0,

    /* U+004C "L" */
    0xa0, 0xa, 0x0, 0xa0, 0xa, 0x0, 0xa0, 0xa,
    0x0, 0xa0, 0xa, 0x0, 0xa0, 0xb, 0xfe,

    /* U+004D "M" */
    0xb0, 0x1, 0xed, 0x0, 0xfb, 0xc0, 0xbe, 0xb4,
    0x37, 0xa7, 0x39, 0xe8, 0xec, 0x7a, 0x1e, 0x1e,
    0x81, 0x7, 0xa0, 0x1, 0xe8, 0x0, 0x70,

    /* U+004E "N" */
    0xb0, 0xc, 0xb4, 0xc, 0xac, 0xc, 0x9a, 0xc,
    0xa3, 0xc, 0xa2, 0x8c, 0xa0, 0xcc, 0xa0, 0x6c,
    0xa0, 0x3c, 0xa0, 0x1c,

    /* U+004F "O" */
    0x7, 0xf8, 0x3, 0x43, 0x82, 0x80, 0x30, 0xd0,
    0x9, 0x30, 0x2, 0x8c, 0x0, 0xa3, 0x40, 0x24,
    0xa0, 0xc, 0xd, 0xe, 0x1, 0xfe, 0x0,

    /* U+0050 "P" */
    0xbf, 0xc2, 0x82, 0xca, 0x3, 0x28, 0xc, 0xa0,
    0x72, 0xff, 0x4a, 0x0, 0x28, 0x0, 0xa0, 0x2,
    0x80, 0x0,

    /* U+0051 "Q" */
    0x7, 0xf8, 0x3, 0x43, 0x82, 0x80, 0x30, 0xd0,
    0x9, 0x30, 0x2, 0x8c, 0x0, 0xa3, 0x40, 0x24,
    0xa0, 0xc, 0xd, 0xe, 0x1, 0xff, 0x0, 0x0,
    0xb8, 0x0, 0x5,

    /* U+0052 "R" */
    0xbf, 0xd0, 0xa0, 0x70, 0xa0, 0x34, 0xa0, 0x34,
    0xa0, 0x30, 0xbf, 0xd0, 0xa2, 0x80, 0xa0, 0xd0,
    0xa0, 0x70, 0xa0, 0x2c,

    /* U+0053 "S" */
    0xb, 0xe0, 0xe0, 0x43, 0x0, 0xd, 0x0, 0x1f,
    0x40, 0xb, 0x80, 0x3, 0x0, 0xd, 0x24, 0x70,
    0x7f, 0x40,

    /* U+0054 "T" */
    0xbf, 0xfc, 0xd, 0x0, 0x34, 0x0, 0xd0, 0x3,
    0x40, 0xd, 0x0, 0x34, 0x0, 0xd0, 0x3, 0x40,
    0xd, 0x0,

    /* U+0055 "U" */
    0x90, 0xc, 0x90, 0xc, 0x90, 0xc, 0x90, 0xc,
    0x90, 0xc, 0x90, 0xc, 0x90, 0xc, 0xa0, 0x1c,
    0x34, 0x74, 0x1f, 0xe0,

    /* U+0056 "V" */
    0xa0, 0x7, 0x1c, 0x2, 0x83, 0x40, 0xc0, 0xa0,
    0x70, 0x1c, 0x28, 0x3, 0x4c, 0x0, 0xa3, 0x0,
    0x1d, 0x80, 0x3, 0xc0, 0x0, 0xb0, 0x0,

    /* U+0057 "W" */
    0x70, 0x1c, 0x3, 0xc, 0xb, 0x40, 0xc3, 0x43,
    0x60, 0x60, 0x90, 0xcc, 0x24, 0x18, 0x63, 0xc,
    0x3, 0x24, 0x93, 0x0, 0xcc, 0x19, 0x80, 0x2b,
    0x3, 0x90, 0x7, 0x80, 0xf0, 0x0, 0xd0, 0x2c,
    0x0,

    /* U+0058 "X" */
    0x70, 0xc, 0x34, 0x38, 0xc, 0x70, 0xa, 0xd0,
    0x3, 0xc0, 0x3, 0xc0, 0xa, 0xd0, 0xc, 0x70,
    0x34, 0x38, 0x70, 0xc,

    /* U+0059 "Y" */
    0xa0, 0x1c, 0x30, 0x34, 0x28, 0x70, 0xc, 0xd0,
    0xb, 0xc0, 0x3, 0x40, 0x3, 0x40, 0x3, 0x40,
    0x3, 0x40, 0x3, 0x40,

    /* U+005A "Z" */
    0x3f, 0xf8, 0x0, 0xd0, 0xa, 0x0, 0x70, 0x3,
    0x80, 0x1c, 0x0, 0xd0, 0xb, 0x0, 0x34, 0x1,
    0xff, 0xe0,

    /* U+005B "[" */
    0xff, 0x4d, 0x34, 0xd3, 0x4d, 0x34, 0xd3, 0x4d,
    0x34, 0xfc,

    /* U+005D "]" */
    0x7d, 0xd, 0xd, 0xd, 0xd, 0xd, 0xd, 0xd,
    0xd, 0xd, 0xd, 0xd, 0x7d,

    /* U+0061 "a" */
    0x1f, 0x81, 0xa, 0x0, 0x71, 0xff, 0x30, 0x77,
    0xb, 0x2f, 0x70,

    /* U+0062 "b" */
    0xd0, 0xd, 0x0, 0xd0, 0xe, 0xf8, 0xe0, 0xdd,
    0x7, 0xd0, 0x7d, 0x7, 0xe0, 0xdd, 0xf8,

    /* U+0063 "c" */
    0xb, 0xd2, 0x80, 0x30, 0x3, 0x0, 0x30, 0x2,
    0x80, 0xb, 0xd0,

    /* U+0064 "d" */
    0x0, 0x28, 0x0, 0xa0, 0x2, 0x83, 0xfe, 0x38,
    0x38, 0xc0, 0xa3, 0x2, 0x8c, 0xa, 0x38, 0x38,
    0x3e, 0xa0,

    /* U+0065 "e" */
    0xb, 0xd0, 0xa1, 0xc3, 0x2, 0x4f, 0xfd, 0x30,
    0x0, 0xa0, 0x40, 0xbe, 0x0,

    /* U+0066 "f" */
    0xb, 0x47, 0x1, 0xc1, 0xfd, 0x1c, 0x7, 0x1,
    0xc0, 0x70, 0x1c, 0x7, 0x0,

    /* U+0067 "g" */
    0xf, 0xe8, 0xe0, 0xe3, 0x2, 0x8c, 0xa, 0x30,
    0x28, 0xe0, 0xe0, 0xfb, 0x80, 0x9, 0x0, 0x30,
    0x7f, 0x40,

    /* U+0068 "h" */
    0xd0, 0xd, 0x0, 0xd0, 0xe, 0xf8, 0xe0, 0xdd,
    0xa, 0xd0, 0xad, 0xa, 0xd0, 0xad, 0xa,

    /* U+0069 "i" */
    0x34, 0x40, 0xd, 0x34, 0xd3, 0x4d, 0x34, 0xd0,

    /* U+006A "j" */
    0xd, 0x4, 0x0, 0xd, 0xd, 0xd, 0xd, 0xd,
    0xd, 0xd, 0xd, 0xc, 0x78,

    /* U+006B "k" */
    0xd0, 0xd, 0x0, 0xd0, 0xd, 0x1c, 0xd7, 0xd,
    0xd0, 0xf8, 0xd, 0xd0, 0xd7, 0xd, 0x1c,

    /* U+006C "l" */
    0xdd, 0xdd, 0xdd, 0xdd, 0xdd,

    /* U+006D "m" */
    0xdf, 0x9f, 0x8e, 0xf, 0xa, 0xd0, 0x90, 0x6d,
    0xa, 0x6, 0xd0, 0xa0, 0x6d, 0xa, 0x6, 0xd0,
    0xa0, 0x60,

    /* U+006E "n" */
    0xdf, 0x8e, 0xd, 0xd0, 0xad, 0xa, 0xd0, 0xad,
    0xa, 0xd0, 0xa0,

    /* U+006F "o" */
    0xb, 0xd0, 0xe0, 0xd3, 0x1, 0x8c, 0x7, 0x30,
    0x18, 0xe0, 0xd0, 0xbd, 0x0,

    /* U+0070 "p" */
    0xdf, 0x8e, 0xd, 0xd0, 0x7d, 0x7, 0xd0, 0x7e,
    0xd, 0xef, 0x8d, 0x0, 0xd0, 0xd, 0x0,

    /* U+0071 "q" */
    0xf, 0xa8, 0xe0, 0xe3, 0x2, 0x8c, 0xa, 0x30,
    0x28, 0xe0, 0xe0, 0xfb, 0x80, 0xa, 0x0, 0x28,
    0x0, 0xa0,

    /* U+0072 "r" */
    0xdd, 0xe0, 0xd0, 0xd0, 0xd0, 0xd0, 0xd0,

    /* U+0073 "s" */
    0x1f, 0xc3, 0x0, 0x34, 0x1, 0xf4, 0x0, 0xc1,
    0xc, 0x2f, 0x80,

    /* U+0074 "t" */
    0x4, 0x7, 0x7, 0xf8, 0x70, 0x1c, 0x7, 0x1,
    0xc0, 0x30, 0xb, 0x80,

    /* U+0075 "u" */
    0xc0, 0x9c, 0x9, 0xc0, 0x9c, 0x9, 0xc0, 0x9a,
    0xd, 0x3e, 0x90,

    /* U+0076 "v" */
    0x60, 0x30, 0xc1, 0xc3, 0x4a, 0x6, 0x30, 0xd,
    0xc0, 0x2d, 0x0, 0x70, 0x0,

    /* U+0077 "w" */
    0x60, 0x70, 0x30, 0xc2, 0xd1, 0xc3, 0x4d, 0x8a,
    0xa, 0x63, 0x34, 0xe, 0x49, 0xc0, 0x3c, 0x1e,
    0x0, 0xb0, 0x34, 0x0,

    /* U+0078 "x" */
    0x70, 0x70, 0xa3, 0x0, 0xe8, 0x2, 0xc0, 0xe,
    0x80, 0xa3, 0x7, 0x7, 0x0,

    /* U+0079 "y" */
    0x60, 0x30, 0xc1, 0xc3, 0x4a, 0x6, 0x30, 0xd,
    0xc0, 0x2d, 0x0, 0x70, 0x1, 0xc0, 0xd, 0x1,
    0xe0, 0x0,

    /* U+007A "z" */
    0x3f, 0xd0, 0x1c, 0x3, 0x0, 0xa0, 0x1c, 0x3,
    0x40, 0x7f, 0xd0,

    /* U+00B0 "°" */
    0x1f, 0x43, 0xc, 0x30, 0xc1, 0xf4
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 55, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 0, .adv_w = 59, .box_w = 2, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 5, .adv_w = 82, .box_w = 4, .box_h = 4, .ofs_x = 1, .ofs_y = 6},
    {.bitmap_index = 9, .adv_w = 145, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 32, .adv_w = 123, .box_w = 6, .box_h = 13, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 52, .adv_w = 168, .box_w = 10, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 77, .adv_w = 148, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 100, .adv_w = 51, .box_w = 2, .box_h = 4, .ofs_x = 1, .ofs_y = 6},
    {.bitmap_index = 102, .adv_w = 63, .box_w = 4, .box_h = 15, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 117, .adv_w = 63, .box_w = 4, .box_h = 15, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 132, .adv_w = 97, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 5},
    {.bitmap_index = 140, .adv_w = 54, .box_w = 3, .box_h = 4, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 143, .adv_w = 56, .box_w = 3, .box_h = 2, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 145, .adv_w = 84, .box_w = 5, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 158, .adv_w = 123, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 178, .adv_w = 123, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 193, .adv_w = 123, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 211, .adv_w = 123, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 229, .adv_w = 123, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 249, .adv_w = 123, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 267, .adv_w = 123, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 285, .adv_w = 123, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 300, .adv_w = 123, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 318, .adv_w = 123, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 336, .adv_w = 60, .box_w = 2, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 340, .adv_w = 63, .box_w = 3, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 347, .adv_w = 123, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 361, .adv_w = 123, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 377, .adv_w = 98, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 392, .adv_w = 202, .box_w = 12, .box_h = 12, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 428, .adv_w = 133, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 451, .adv_w = 133, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 469, .adv_w = 127, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 489, .adv_w = 146, .box_w = 8, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 509, .adv_w = 125, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 524, .adv_w = 120, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 539, .adv_w = 140, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 559, .adv_w = 150, .box_w = 8, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 579, .adv_w = 62, .box_w = 2, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 584, .adv_w = 99, .box_w = 5, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 597, .adv_w = 128, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 615, .adv_w = 112, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 630, .adv_w = 180, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 653, .adv_w = 152, .box_w = 8, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 673, .adv_w = 152, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 696, .adv_w = 126, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 714, .adv_w = 152, .box_w = 9, .box_h = 12, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 741, .adv_w = 137, .box_w = 8, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 761, .adv_w = 113, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 779, .adv_w = 117, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 797, .adv_w = 148, .box_w = 8, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 817, .adv_w = 133, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 840, .adv_w = 205, .box_w = 13, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 873, .adv_w = 124, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 893, .adv_w = 118, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 913, .adv_w = 117, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 931, .adv_w = 73, .box_w = 3, .box_h = 13, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 941, .adv_w = 73, .box_w = 4, .box_h = 13, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 954, .adv_w = 111, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 965, .adv_w = 122, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 980, .adv_w = 104, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 991, .adv_w = 122, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1009, .adv_w = 114, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1022, .adv_w = 78, .box_w = 5, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1035, .adv_w = 122, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 1053, .adv_w = 122, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1068, .adv_w = 55, .box_w = 3, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1076, .adv_w = 55, .box_w = 4, .box_h = 13, .ofs_x = -1, .ofs_y = -3},
    {.bitmap_index = 1089, .adv_w = 108, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1104, .adv_w = 56, .box_w = 2, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1109, .adv_w = 189, .box_w = 10, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1127, .adv_w = 122, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1138, .adv_w = 122, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1151, .adv_w = 122, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 1166, .adv_w = 122, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 1184, .adv_w = 78, .box_w = 4, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1191, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1202, .adv_w = 82, .box_w = 5, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1214, .adv_w = 121, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1225, .adv_w = 108, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1238, .adv_w = 172, .box_w = 11, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1258, .adv_w = 105, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1271, .adv_w = 108, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 1289, .adv_w = 96, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1300, .adv_w = 94, .box_w = 6, .box_h = 4, .ofs_x = 0, .ofs_y = 5}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint8_t glyph_id_ofs_list_0[] = {
    0, 1, 2, 3, 4, 5, 6, 7,
    8, 9, 10, 0, 11, 0, 12, 13,
    14, 15, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 26
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 29, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = glyph_id_ofs_list_0, .list_length = 29, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL
    },
    {
        .range_start = 62, .range_length = 30, .glyph_id_start = 28,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 93, .range_length = 1, .glyph_id_start = 58,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 97, .range_length = 26, .glyph_id_start = 59,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 176, .range_length = 1, .glyph_id_start = 85,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};

/*-----------------
 *    KERNING
 *----------------*/


/*Map glyph_ids to kern left classes*/
static const uint8_t kern_left_class_mapping[] =
{
    0, 1, 0, 2, 0, 0, 0, 3,
    2, 4, 0, 5, 6, 6, 7, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 8, 8, 0, 0, 0, 9, 10,
    11, 12, 13, 14, 15, 16, 0, 0,
    17, 18, 19, 0, 0, 20, 21, 20,
    22, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 0, 32, 33, 34, 0, 35,
    36, 37, 38, 0, 0, 39, 0, 38,
    38, 40, 33, 37, 41, 42, 43, 37,
    44, 45, 46, 47, 48, 0
};

/*Map glyph_ids to kern right classes*/
static const uint8_t kern_right_class_mapping[] =
{
    0, 1, 0, 2, 0, 0, 0, 3,
    2, 4, 0, 5, 6, 6, 7, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 8, 8, 0, 0, 9, 10, 11,
    0, 12, 0, 0, 0, 12, 0, 0,
    13, 0, 0, 0, 0, 12, 0, 12,
    0, 14, 15, 16, 17, 18, 19, 20,
    21, 0, 22, 23, 0, 24, 25, 24,
    26, 25, 0, 0, 27, 0, 0, 28,
    28, 24, 28, 25, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 0
};

/*Kern values between classes*/
static const int8_t kern_class_values[] =
{
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -6, 0, -6, 0, -5, 0,
    -6, -5, 0, -6, 0, 0, 0, 0,
    0, -3, 0, 0, 0, -4, 0, -5,
    -4, 0, -5, 0, 0, 0, -3, 0,
    0, -26, -8, 0, 0, 0, -8, 0,
    -14, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -3, -4, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -6, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -7, 0,
    -4, -3, 0, -7, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -3,
    -2, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -3, -4, -4, 0, 1, -3,
    -3, -2, -3, -2, -2, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -8, 0, -14, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -4,
    -4, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -26, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -4,
    0, 0, -11, -3, -12, -9, 0, -11,
    0, 0, 0, 0, 0, -3, 0, 0,
    0, -4, 0, -8, -6, 0, -8, 0,
    0, 0, 0, 0, 0, 0, -30, 0,
    0, 0, -7, 0, -10, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -4, -5,
    -5, 0, 0, -3, -4, 0, -3, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -10, 0, -2, 0, 0, -5,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -4, 0, -3, 0,
    0, 0, 0, -2, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -6, -8, 0, 0,
    -8, 0, 0, 0, -3, 0, 0, -2,
    0, 0, -10, -2, -7, -5, 0, -9,
    0, -4, 0, 0, 0, -2, 0, 0,
    0, -3, 0, -4, -3, 0, -4, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -3, 0,
    -1, -1, -2, -2, 0, -4, 0, 0,
    0, 0, 0, 0, 0, -1, 0, 0,
    0, -1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -4,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -3, -3, -2, 0, -1,
    0, -4, -2, -4, -4, 0, -4, 0,
    0, 0, 0, 0, 0, -4, 0, 0,
    -2, 0, -2, 0, -4, 0, -5, 0,
    -2, -1, -4, -4, -2, -4, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -2,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -1, 0, -1, 0, 0,
    0, -2, 0, -2, -1, 0, -2, 0,
    -3, 0, 0, 0, 0, -14, -4, -2,
    0, 0, -8, -2, -20, -2, 0, 0,
    0, 0, 0, 0, 0, 0, -4, -2,
    -2, 0, 0, -2, -1, -1, -2, 0,
    0, -2, 0, -2, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -1, 0, 0,
    0, -2, 0, -1, -1, 0, -2, 0,
    0, 0, 0, 0, 0, -2, 0, 0,
    0, 0, -1, 0, -1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -7,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -4, -3, -2, 0, 0,
    0, -4, -2, -6, -6, 0, -6, 0,
    -5, -16, 0, 0, -17, 0, 0, 0,
    -3, 0, 0, -4, 0, 0, -17, -3,
    -14, -11, 0, -16, 0, -4, 0, -1,
    -1, -2, 0, 0, 0, -5, 0, -12,
    -9, 0, -13, 0, 0, 0, 0, 0,
    0, -4, 0, 0, 0, 0, -2, 0,
    -3, 0, -4, 0, -2, -1, -3, -4,
    -2, -4, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -4, 0, 0, 0, 0, -17, -5, 0,
    0, 0, -7, 0, -15, 0, 0, 0,
    0, 0, -3, 0, -1, 0, -1, -2,
    -2, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -2, 0, 0, 0, 0, -2,
    0, -4, 0, -1, -1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -2, 0, 0, 0, -3, 0, -3,
    -2, -1, -3, 0, -5, 0, -3, 0,
    0, -11, -8, -10, 0, -6, -10, -4,
    -15, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -15, -15, -15, -4, 0, -16,
    -15, -7, -16, -14, -13, -13, -15, -15,
    0, 0, 0, 0, 0, -3, 0, 0,
    0, 0, -2, 0, -3, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -1, 0,
    0, 0, 0, 0, -1, 0, -1, 0,
    0, 0, 0, 0, -6, 0, -2, 0,
    0, -12, -7, -2, 0, -2, -7, -2,
    -12, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -7, -8, -7, -1, 0, -6,
    -7, -1, -6, 0, -1, -1, 0, -3,
    -5, 0, 0, 0, 0, -9, -5, 0,
    0, 0, -5, -1, -10, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -5, -5,
    -5, 0, 0, -4, -4, 0, -4, 0,
    0, 0, 0, -2, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -3,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -4, -3, -2, 0, 0,
    0, -4, -2, -5, -5, 0, -5, 0,
    -6, 0, -3, 0, 0, -12, -8, -5,
    0, -5, -9, -4, -13, -1, 0, 0,
    0, 0, 0, 0, 0, 0, -12, -12,
    -11, -3, 0, -10, -12, -4, -10, -4,
    -4, -5, -4, -7, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -2,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -2, -2, -2, 0, 0,
    0, -3, -2, -3, -3, 0, -3, 0,
    0, 0, 0, -3, 0, 0, 0, 0,
    0, 0, -4, -4, -4, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -5, -6,
    -6, -3, 3, -5, -5, -4, -5, -4,
    -4, -3, 0, -4, 0, -2, 0, 0,
    -3, 0, 0, 0, -4, 0, 0, 0,
    0, 0, -16, 0, -7, -4, 0, -12,
    0, -5, 0, 0, 0, 0, 0, 0,
    0, -1, 0, -1, -1, 0, -1, 0,
    0, -4, 0, 0, -4, 0, 0, 0,
    -5, 0, 0, 0, 0, -1, -15, 0,
    -7, -5, -3, -11, -1, -6, 0, 0,
    0, -1, 0, 0, 0, -1, 0, -2,
    -1, -2, -2, -1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -1,
    0, 0, -20, -1, -3, -2, 0, -9,
    0, -4, -1, -3, -3, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -2, 0, 0, -2, 0, 0, 0,
    -4, 0, 0, 0, 0, 0, -16, 0,
    -7, -5, -1, -14, 0, -5, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -1,
    -1, -1, -1, 0, -4, 0, 0, 0,
    0, -6, -4, 0, 0, 0, -6, 0,
    -7, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -2, -1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -3, 0, 0, 0, 0, 0, -16, 0,
    -6, -4, 0, -10, 0, -5, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -2, 0, 0,
    -3, 0, 0, 0, -4, 0, 0, 0,
    0, 0, -16, 0, -7, -4, 0, -11,
    0, -5, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -1, 0, 0, -1, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -1, 0, 0, -15, 0,
    -1, 0, 0, -5, 0, -3, -1, -5,
    -4, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -3, 0, 0,
    -4, 0, 0, 0, -4, 0, 0, 0,
    -1, -1, -15, -1, -8, -5, -4, -12,
    -1, -6, 0, 0, 0, -1, 0, 0,
    0, -1, 0, -2, -1, -2, -2, -1,
    -5, 0, 0, 0, 0, -9, -5, 0,
    0, 0, -7, 0, -14, 0, -14, 0,
    0, 0, -5, -3, -4, -4, 0, -3,
    -2, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -3, 0, 0, 0,
    0, 0, -15, 0, -5, -3, 0, -8,
    0, -5, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -12, 0,
    -1, 0, 0, -5, 0, -3, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -5, 0, 0, 0,
    0, -8, -4, 0, 0, 0, -4, 0,
    -11, 0, -14, 0, 0, 0, -5, -4,
    -4, -4, -1, -2, -2, 0, 0, 0,
    -1, 0, 0, 0, 0, 0, 0, 0,
    -4, 0, 0, 0, 0, -6, -3, 0,
    0, 0, -3, 0, -7, 0, -13, 0,
    -1, 0, -5, -4, -3, -4, -1, -1,
    -1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -13, 0, -1, 0, 0, -5,
    0, -3, 0, -2, -2, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    -4, 0, 0, 0, 0, -7, -4, 0,
    0, 0, -3, 0, -9, 0, -14, 0,
    -1, 0, -5, -4, -4, -4, -1, -1,
    -1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -15, 0, -3, -2, 0, -7,
    0, -4, 0, -1, -1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};


/*Collect the kern class' data in one place*/
static const lv_font_fmt_txt_kern_classes_t kern_classes =
{
    .class_pair_values   = kern_class_values,
    .left_class_mapping  = kern_left_class_mapping,
    .right_class_mapping = kern_right_class_mapping,
    .left_class_cnt      = 48,
    .right_class_cnt     = 36,
};

/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = &kern_classes,
    .kern_scale = 16,
    .cmap_num = 5,
    .bpp = 2,
    .kern_classes = 1,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};


/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t vw_14 = {
#else
lv_font_t vw_14 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 15,          /*The maximum line height required by the font*/
    .base_line = 4,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
    .fallback = NULL,
    .user_data = NULL
};



#endif /*#if VW_14*/

