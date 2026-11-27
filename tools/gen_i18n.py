#!/usr/bin/env python3
"""Generate embedded 16x16 monochrome UI bitmaps; no full font is bundled.
Input: user-installed Noto Sans CJK Regular (SC face, index 2 by default).
The generated glyph data is an OFL-1.1 derivative (see docs license).
"""
import argparse,json
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
p=argparse.ArgumentParser();p.add_argument('--font',required=True);p.add_argument('--index',type=int,default=2);a=p.parse_args()
root=Path(__file__).resolve().parent.parent
strings=json.loads((root/'tools/i18n_zh.json').read_text())
chars=sorted({c for s in strings.values() for c in s if ord(c)>127}|{'?'})
# At 4x raster resolution use a 15px em, fitting the common glyph baseline.
# A small supersampling pass avoids truncating descenders at a 16px cell.
f=ImageFont.truetype(a.font,60,index=a.index)
lines=['/* Generated C3 UI glyph data (16x16, MSB=left). OFL-1.1. */','static const struct { uint16_t cp; uint16_t rows[16]; } zh_glyphs[] = {']
for c in chars:
 im=Image.new('L',(64,64));d=ImageDraw.Draw(im);bb=f.getbbox(c,anchor='ls')
 # Preserve baseline as far as possible while ensuring glyph fits cell.
 x=max(0,min(2,64-bb[2])); y=max(-bb[1],min(52,64-bb[3]));d.text((x,y),c,font=f,fill=255,anchor='ls')
 im=im.resize((16,16),Image.Resampling.LANCZOS)
 rows=[sum((im.getpixel((x,y))>=96)<<(15-x) for x in range(16)) for y in range(16)]
 assert any(rows),c
 lines.append('    {0x%04Xu,{%s}}, /* %s */'%(ord(c),','.join('0x%04Xu'%r for r in rows),c))
lines+=['};','static const struct { const char *en,*zh; } translations[] = {']
for en,zh in sorted(strings.items()):lines.append('    {%s,%s},'%(json.dumps(en,ensure_ascii=False),json.dumps(zh,ensure_ascii=False)))
lines+=['};','']
(root/'C3M1_Camera/Core/Src/c3_i18n_data.inc').write_text('\n'.join(lines))
print(f'{len(strings)} translations; {len(chars)} glyphs; {len(chars)*32} bitmap bytes')
