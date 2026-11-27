"""Execute the actual firmware's decoder against original and generated DNGs.
Requires Python 3, numpy, a host cc. Usage: python3 tests/test_v207_decode.py --archives /mnt/data
Without --archives, synthetic fixtures still test both BMP/DNG and malformed input.
"""
import argparse, ctypes as C, pathlib, struct, subprocess, tempfile, zipfile
import numpy as np
root=pathlib.Path(__file__).resolve().parents[1]; core=root/'C3M1_Camera/Core'
P=argparse.ArgumentParser();P.add_argument('--archives',type=pathlib.Path);P.add_argument('--preview',type=pathlib.Path);args=P.parse_args()
READ=C.CFUNCTYPE(C.c_uint8,C.c_void_p,C.c_uint32,C.POINTER(C.c_uint8),C.c_uint32)
class File(C.Structure):_fields_=[('read',READ),('ctx',C.c_void_p),('size',C.c_uint32)]
class Img(C.Structure):_fields_=[('offset',C.c_uint32),('row_bytes',C.c_uint32),('width',C.c_uint16),('raw',C.c_uint8),('sample_bytes',C.c_uint8),('reverse_y',C.c_uint8),('reverse_x',C.c_uint8),('lut',(C.c_uint16*256)*4),('matrix',C.c_int32*9)]
def load(lib,data,dng):
    @READ
    def read(ctx,offset,dst,n):
        assert offset+n<=len(data)
        C.memmove(dst,bytes(data[offset:offset+n]),n);return 1
    f=File(read,None,len(data));im=Img();ok=lib.c3_play_parse(C.byref(f),dng,C.byref(im));return ok,im

def render(lib,data,im):
    a=np.empty((240,320),dtype=np.uint16);out=(C.c_uint8*640)()
    for y in range(240):
        offset=lib.c3_play_row_offset(C.byref(im),y);assert offset==im.offset+im.row_bytes*y
        b=(C.c_uint8*im.row_bytes).from_buffer_copy(data[offset:offset+im.row_bytes])
        lib.c3_play_render_row(C.byref(im),b,out)
        a[lib.c3_play_display_y(C.byref(im),y)]=np.frombuffer(out,dtype='>u2')
    return a

def pack(r,g,b):return ((r.astype(np.uint16)&248)<<8)|((g.astype(np.uint16)&252)<<3)|(b.astype(np.uint16)>>3)
def unpack(a):return np.stack([(a>>11)*255//31,((a>>5)&63)*255//63,(a&31)*255//31],axis=-1).astype(np.uint8)

def bmp(top):
    y,x=np.indices((240,320));img=np.stack([(x*3+y)%256,(y*7)%256,(x+y*2)%256],axis=-1).astype(np.uint8)
    raw=img[:,:,::-1] if top else img[::-1,:,::-1]
    header=bytearray(54);struct.pack_into('<2sIHHIIiiHHIIIIII',header,0,b'BM',54+raw.size,0,0,54,40,320,-240 if top else 240,1,24,0,raw.size,2835,2835,0,0)
    return bytes(header)+raw.tobytes(),pack(*img.transpose(2,0,1))

def tags(data):
    off=struct.unpack_from('<I',data,4)[0];n=struct.unpack_from('<H',data,off)[0]
    t={}
    for i in range(n):
        id,ty,num,val=struct.unpack_from('<HHII',data,off+2+12*i);sz={1:1,2:1,3:2,4:4,5:8,10:8,11:4}[ty];p=struct.pack('<I',val) if num*sz<=4 else data[val:val+num*sz]
        if ty in (3,4):v=struct.unpack_from('<'+('H' if ty==3 else 'I')*num,p)
        elif ty in (5,10):
            v=tuple((a,b) for a,b in struct.iter_unpack('<II' if ty==5 else '<ii',p[:num*sz]))
        else:v=p[:num*sz]
        t[id]=(ty,v)
    return t

def make_dng(bits=8,orient=3,black=(8,),white=255,neutral=((1,1),(1,1),(1,1)),width=642):
    """No forward matrix: exact identity preview; simple spatial and chroma pattern."""
    h=480;raw=np.empty((h,width),np.uint8);y,x=np.indices((240,width//2));raw[0::2,0::2]=(x*3+y)%256;raw[0::2,1::2]=(x+y*4)%256;raw[1::2,0::2]=(x*2+y)%256;raw[1::2,1::2]=(x+y*3)%256
    b=raw.tobytes() if bits==8 else (raw.astype('<u2')<<8).tobytes()
    off=512;tail=off+len(b);scale=256 if bits==16 else 1
    ts=[(256,4,1,width),(257,4,1,h),(258,3,1,bits),(259,3,1,1),(262,3,1,32803),(273,4,1,off),(274,3,1,orient),(277,3,1,1),(278,4,1,h),(279,4,1,len(b)),(284,3,1,1),(33421,3,2,0x20002),(33422,1,4,0x10102),(50706,1,4,0x401),(50713,3,2,0x10001 if len(black)==1 else 0x20002),(50714,5,len(black),tail),(50717,4,1,white*scale),(50728,5,3,tail+8*len(black))]
    pre=bytearray(off);struct.pack_into('<2sHIH',pre,0,b'II',42,8,len(ts))
    for i,t in enumerate(ts):struct.pack_into('<HHII',pre,10+12*i,*t)
    return bytes(pre)+b+b''.join(struct.pack('<II',v*scale,1) for v in black)+b''.join(struct.pack('<II',a,b) for a,b in neutral)

def expected_dng(data):
    t=tags(data);get=lambda k,default=None:t[k][1][0] if k in t else default
    w,bits,off=get(256),get(258),get(273);orient=get(274,1)
    white=get(50717,255 if bits==8 else 65535);black=t.get(50714,(3,(0,)))
    def q_values(tag,frac):
        ty,v=tag
        return [a*(1<<frac)//b for a,b in v] if ty in (5,10) else [a*(1<<frac) for a in v]
    bl=q_values(black,8);bl=bl*4 if len(bl)==1 else bl
    n=q_values(t.get(50728,(5,((1,1),)*3)),12)
    wb=[n[1]*4096//v for v in n]
    if 50964 in t:
        fw=np.array([int(abs(a)*4096//b)*(1 if a>=0 else -1) for a,b in t[50964][1]],dtype=np.int64).reshape(3,3)
        xyz=np.array([12836,-6623,-2010,-4009,7849,137,295,-938,5756],dtype=np.int64).reshape(3,3)
        m=np.trunc((xyz@fw)/4096).astype(np.int64)
    else:m=np.eye(3,dtype=np.int64)*4096
    a=np.frombuffer(data,dtype='u1' if bits==8 else '<u2',count=w*480,offset=off).reshape(480,w)[:,:640].astype(np.int64)
    if bits==16:a=(a>>8)<<8
    l=[]
    for c,(dy,dx,ch) in enumerate([(0,0,2),(0,1,1),(1,0,1),(1,1,0)]):
        q=np.clip((a[dy::2,dx::2]*256-bl[c])*4096//(white*256-bl[c]),0,4096)
        l.append((q*wb[ch]+2048)//4096)
    rgb=np.stack([l[3],(l[1]+l[2]+1)//2,l[0]],axis=-1)
    linear=np.clip(np.trunc((rgb@m.T)/4096).astype(np.int64),0,4096)
    idx=(linear*255+2048)//4096;v=idx/255.
    disp=np.rint(255*np.where(v<=0.0031308,12.92*v,1.055*v**(1/2.4)-.055)).astype(np.uint8)
    out=pack(*disp.transpose(2,0,1));return out[::-1,::-1] if orient==3 else out

with tempfile.TemporaryDirectory() as d:
    so=pathlib.Path(d)/'decoder.so'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-shared','-fPIC','-I'+str(core/'Inc'),str(core/'Src/c3_play_decode.c'),str(core/'Src/c3_font.c'),'-o',str(so)],check=True)
    lib=C.CDLL(str(so));lib.c3_play_parse.argtypes=[C.POINTER(File),C.c_uint8,C.POINTER(Img)];lib.c3_play_parse.restype=C.c_uint8
    lib.c3_play_row_offset.argtypes=[C.POINTER(Img),C.c_uint16];lib.c3_play_row_offset.restype=C.c_uint32
    lib.c3_play_display_y.argtypes=[C.POINTER(Img),C.c_uint16];lib.c3_play_display_y.restype=C.c_uint16
    lib.c3_play_render_row.argtypes=[C.POINTER(Img),C.POINTER(C.c_uint8),C.POINTER(C.c_uint8)]
    lib.c3_play_name_key.argtypes=[C.c_char_p];lib.c3_play_name_key.restype=C.c_uint32
    lib.c3_font_glyph.argtypes=[C.c_char];lib.c3_font_glyph.restype=C.POINTER(C.c_uint8)
    for s,want in [(b'C3_00001.DNG',3),(b'c3_00001.bmp',2),(b'C3_99999.DNG',199999),(b'C3_00000.BMP',0),(b'C3_00001.TMP',0),(b'X3_00001.DNG',0),(b'',0),(b'C3_0001.DNG',0),(b'C3_12345.DNG.old',0)]:assert lib.c3_play_name_key(s)==want
    for top in [False,True]:
        data,expect=bmp(top);ok,im=load(lib,data,0);assert ok;assert np.array_equal(render(lib,data,im),expect)
        for n in [0,53,54,len(data)-1]:assert not load(lib,data[:n],0)[0]
    print('PASS BMP: top-down + bottom-up RGB565 orientation and truncation; strict 8.3 name parsing')
    for bits in [8,16]:
        for o in [1,3]:
            for black in [(8,),(2,3,4,5)]:
                for w in [640,642]:
                    data=make_dng(bits,o,black,width=w);ok,im=load(lib,data,1);assert ok
                    assert np.array_equal(render(lib,data,im),expected_dng(data))
    print('PASS 16 synthetic DNGs: 640/642, RAW8/RAW16, single/4 black, orientation 1/3; exact independent NumPy reference')
    data=make_dng();t=tags(data);# malformed critical TIFF metadata
    for n in [0,7,9,100,511,len(data)-1,len(data)-30]:assert not load(lib,data[:n],1)[0]
    def mutate(id,field,v):
        b=bytearray(data);n=struct.unpack_from('<H',b,8)[0]
        for i in range(n):
            p=10+12*i
            if struct.unpack_from('<H',b,p)[0]==id:struct.pack_into('<I',b,p+field,v);return bytes(b)
        assert False
    for id,field,v in [(256,8,0xffffffff),(257,8,0),(258,8,12),(259,8,7),(273,8,0xffffffff),(279,8,1),(33422,8,0x02010100),(50714,4,0xffffffff),(50714,8,0xffffffff),(50728,4,10000),(50728,8,0xffffffff),(274,8,6),(50717,8,256)]:
        assert not load(lib,mutate(id,field,v),1)[0],(id,field,v)
    b=bytearray(data);struct.pack_into('<I',b,len(data)-4,0);assert not load(lib,b,1)[0]
    print('PASS malformed DNG: truncated RAW/tail, unsupported compression/orientation/CFA, overflow offsets/counts, zero rational denominator')
    if args.archives:
        count=0
        for name in ['Black Levels.zip','Black Levels BLC.zip','C3M1_CameraRaw兼容性对照DNG.zip','C3M1_00076_DNG发紫_白平衡与黑电平对照.zip']:
            with zipfile.ZipFile(args.archives/name) as z:
                for fn in z.namelist():
                    if not fn.lower().endswith('.dng'):continue
                    b=z.read(fn);ok,im=load(lib,b,1);assert ok,(name,fn)
                    got=render(lib,b,im);assert np.array_equal(got,expected_dng(b)),(name,fn)
                    count+=1
                    if args.preview and fn.startswith('E_'):
                        from PIL import Image,ImageDraw
                        img=Image.fromarray(unpack(got));draw=ImageDraw.Draw(img)
                        def text(x,y,txt,scale):
                            for ch in txt.encode('ascii'):
                                glyph=lib.c3_font_glyph(bytes([ch]))
                                if glyph:
                                    for xx in range(5):
                                        for yy in range(7):
                                            if glyph[xx]&(1<<yy):
                                                draw.rectangle((x+xx*scale,y+yy*scale,x+(xx+1)*scale-1,y+(yy+1)*scale-1),fill='white')
                                x+=6*scale
                        draw.rectangle((0,0,319,19),fill=(0,0,0));text(6,3,'PLAY',2);text(96,3,'00076.DNG',2)
                        draw.rectangle((0,224,319,239),fill=(0,0,0));text(6,228,'DIAL PREV/NEXT   ISO INFO   SHOT BACK',1)
                        img.resize((640,480),Image.Resampling.NEAREST).save(args.preview)
        print(f'PASS {count} original/test DNGs from uploaded archives: all 320x240 decoded pixels match independent reference')
