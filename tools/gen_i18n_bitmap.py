#!/usr/bin/env python3
"""Extract native 1-bit 16px EBLC/EBDT glyphs from AR PL UMing CN.
No outline rasterization, rescaling, thresholding or stroke edits.
The extracted bitmap subset is under the Arphic Public License.
See docs/ARPHICPL.TXT and docs/C3_UI_bitmap_UMing.txt.
"""
import argparse,json,hashlib
from pathlib import Path
from fontTools.ttLib import TTCollection
p=argparse.ArgumentParser();p.add_argument('--font',required=True);p.add_argument('--index',type=int,default=0);a=p.parse_args()
root=Path(__file__).resolve().parent.parent
strings=json.loads((root/'tools/i18n_zh.json').read_text())
chars=sorted({c for s in strings.values() for c in s if ord(c)>127}|{'?'})
font=TTCollection(a.font).fonts[a.index]
assert font['name'].getDebugName(1)=='AR PL UMing CN', 'expected UMing CN face'
indices=[i for i,s in enumerate(font['EBLC'].strikes) if (s.bitmapSizeTable.ppemX,s.bitmapSizeTable.ppemY,s.bitmapSizeTable.bitDepth)==(16,16,1)]
assert len(indices)==1
idx=indices[0]; strike=font['EBLC'].strikes[idx]; data=font['EBDT'].strikeData[idx]; cmap=font.getBestCmap()
subtables={name:t for t in strike.indexSubTables for name in t.names}
baseline=strike.bitmapSizeTable.hori.ascender
lines=['/* C3 UI bitmap subset, extracted 2026-11-27 from AR PL UMing CN',
       ' * version 0.2.20080216.1, native 16px 1-bit strike.',
       ' * Changes: subset, placement in 16x16 cells and C array serialization only.',
       ' * (C) 1994-1999 Arphic Technology Co., Ltd.; (C) 2003-2007 Arne Goetje.',
       ' * Glyph data: Arphic Public License (docs/ARPHICPL.TXT).',
       ' * Translation table is separate project data. */',
       'static const struct { uint16_t cp; uint16_t rows[16]; } zh_glyphs[] = {']
for c in chars:
    name=cmap[ord(c)]; g=data[name]; table=subtables[name]
    assert table.imageFormat==5 and table.indexFormat in (2,5), (c,table.imageFormat,table.indexFormat)
    g.ensureDecompiled(); m=table.metrics
    x0=m.horiBearingX; y0=baseline-m.horiBearingY; w=m.width; h=m.height
    assert x0>=0 and y0>=0 and x0+w<=16 and y0+h<=16,(c,x0,y0,w,h)
    raw=g.imageData; assert len(raw)==(w*h+7)//8
    rows=[0]*16
    for y in range(h):
        for x in range(w):
            bit=y*w+x
            if raw[bit//8] & (128>>(bit%8)): rows[y0+y]|=1<<(15-x0-x)
    assert any(rows),c
    # All native set pixels survive extraction, including the last row.
    assert sum(v.bit_count() for v in rows)==sum(bool(raw[i//8]&(128>>(i%8))) for i in range(w*h))
    lines.append('    {0x%04Xu,{%s}}, /* %s */'%(ord(c),','.join('0x%04Xu'%r for r in rows),c))
lines+=['};','static const struct { const char *en,*zh; } translations[] = {']
for en,zh in sorted(strings.items()):lines.append('    {%s,%s},'%(json.dumps(en,ensure_ascii=False),json.dumps(zh,ensure_ascii=False)))
lines+=['};','']
(root/'C3M1_Camera/Core/Src/c3_i18n_data.inc').write_text('\n'.join(lines))
print(f'{len(strings)} translations; {len(chars)} native 16px glyphs; {len(chars)*32} bitmap bytes; no clipping')
print('Source SHA256:',hashlib.sha256(Path(a.font).read_bytes()).hexdigest())
