"""Generate an uncompressed LVGL 4bpp font from the supplied licensed TTF.

Usage: python tools/generate-ui-font.py source.ttf
Requires Pillow. The generated font has a project-specific name (OFL reserved names).
"""
import sys
from pathlib import Path
from PIL import ImageFont

font = ImageFont.truetype(sys.argv[1], 16)
ranges = [(0x20, 0x7e), (0xc0, 0x1b0), (0x1ea0, 0x1ef9), (0x20ab, 0x20ab)]
bitmap = []
glyphs = [(0, 0, 0, 0, 0, 0)]
cmaps = []
for first, last in ranges:
    cmaps.append((first, last - first + 1, len(glyphs)))
    for cp in range(first, last + 1):
        mask, (x, y) = font.getmask2(chr(cp), mode="L", anchor="ls")
        width, height = mask.size
        glyphs.append((len(bitmap), round(font.getlength(chr(cp)) * 16), width, height, x, -y-height))
        pixels = list(mask)
        pixels += [0] * (len(pixels) % 2)
        bitmap.extend((((pixels[i] + 8) // 17) << 4) | ((pixels[i+1] + 8) // 17)
                      for i in range(0, len(pixels), 2))
lines = ['/* Generated from Source Code Pro Semibold; SIL OFL 1.1. */', '#include "lvgl.h"',
         'static const uint8_t bitmap[] = {']
for i in range(0, len(bitmap), 16):
    lines.append('    ' + ', '.join(f'0x{v:02x}' for v in bitmap[i:i+16]) + ',')
lines += ['};', 'static const lv_font_fmt_txt_glyph_dsc_t glyphs[] = {']
for index, advance, w, h, x, y in glyphs:
    lines.append(f'    {{.bitmap_index={index}, .adv_w={advance}, .box_w={w}, .box_h={h}, .ofs_x={x}, .ofs_y={y}}},')
lines += ['};', 'static const lv_font_fmt_txt_cmap_t cmaps[] = {']
for first, length, index in cmaps:
    lines.append(f'    {{.range_start={first}, .range_length={length}, .glyph_id_start={index}, .type=LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY}},')
lines += ['};', 'static const lv_font_fmt_txt_dsc_t descriptor = {',
          '    .glyph_bitmap=bitmap, .glyph_dsc=glyphs, .cmaps=cmaps,',
          '    .cmap_num=4, .bpp=4, .bitmap_format=LV_FONT_FMT_TXT_PLAIN,', '};',
          'const lv_font_t hisu_font_vi_semibold_16 = {',
          '    .get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,',
          '    .get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,',
          '    .line_height=22, .base_line=4, .dsc=&descriptor,', '};']
out = Path(__file__).resolve().parent.parent / 'components/hisu_ui/fonts/font_vi_semibold_16.c'
out.write_text('\n'.join(lines) + '\n', encoding='utf-8')
print(f'{out}: {len(bitmap)} bitmap bytes, {len(glyphs)} glyphs')
