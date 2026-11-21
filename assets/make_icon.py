"""Generates the ZIMTV IPTV icon0.png (512x512 RGB) - Invader Zim inspired, standard library only.
Stylized Irken face motif (green, big eyes, antenna hints) + ZIMTV text banner.
Usage: python -I assets/make_icon.py assets/icon0.png
"""
import math
import struct
import sys
import zlib

W = H = 512
SS = 3

BG_TOP = (10, 18, 12)
BG_BOT = (18, 40, 22)
GREEN = (140, 255, 90)
GREEN_DARK = (70, 170, 50)
PURPLE = (120, 60, 160)
MAGENTA = (255, 90, 190)
BLACK = (10, 10, 14)
WHITE = (245, 255, 240)
PINK = (255, 150, 210)

def lerp(a,b,t):
    t=min(max(t,0.0),1.0)
    return tuple(a[i]+(b[i]-a[i])*t for i in range(3))

def clamp(v): return max(0,min(255,int(round(v))))

def in_ellipse(x,y,cx,cy,rx,ry):
    if rx<=0 or ry<=0: return False
    dx=(x-cx)/rx; dy=(y-cy)/ry
    return dx*dx+dy*dy <= 1.0

def in_rect(x,y,x0,y0,x1,y1):
    return x0<=x<=x1 and y0<=y<=y1

# simple 5x7 bitmap font for ZIMTV
FONT = {
'Z':["11111","00001","00010","00100","01000","10000","11111"],
'I':["01110","00100","00100","00100","00100","00100","01110"],
'M':["10001","11011","10101","10101","10001","10001","10001"],
'T':["11111","00100","00100","00100","00100","00100","00100"],
'V':["10001","10001","10001","10001","01010","01010","00100"],
' ': ["00000","00000","00000","00000","00000","00000","00000"],
}

def draw_text_mask(x,y,scale,text):
    # returns function-like check via set not needed; we'll paint later
    pass

def text_pixels(x0,y0,scale,text):
    px=set()
    cx=x0
    for ch in text:
        g=FONT.get(ch,FONT[' '])
        for r,row in enumerate(g):
            for c,v in enumerate(row):
                if v=='1':
                    for sy in range(scale):
                        for sx in range(scale):
                            px.add((int(cx+c*scale+sx), int(y0+r*scale+sy)))
        cx += (6*scale)
    return px

TEXT = text_pixels(112, 392, 10, "ZIMTV")  # centered-ish banner

def sample(x,y):
    # background gradient with subtle radial glow
    t = y/H
    c = list(lerp(BG_TOP, BG_BOT, t))
    dx=(x-W*0.5)/(W*0.5); dy=(y-H*0.42)/(H*0.5)
    glow = max(0.0, 1.0 - (dx*dx+dy*dy))
    c = lerp(tuple(c), (30,70,35), glow*0.35)

    # antenna-like shapes
    # left antenna
    ax1,ay1,ax2,ay2 = 210,130,170,40
    # distance to segment
    def segd(px,py,x1,y1,x2,y2,rad):
        vx,vy=x2-x1,y2-y1; wx,wy=px-x1,py-y1
        l2=vx*vx+vy*vy
        t2=0 if l2==0 else max(0,min(1,(wx*vx+wy*vy)/l2))
        bx,by=x1+t2*vx,y1+t2*vy
        return (px-bx)**2+(py-by)**2 <= rad*rad
    if segd(x,y,210,150,165,55,7): c = GREEN_DARK
    if in_ellipse(x,y,165,50,10,10): c = MAGENTA
    # right antenna
    if segd(x,y,302,150,347,55,7): c = GREEN_DARK
    if in_ellipse(x,y,347,50,10,10): c = MAGENTA

    # head (Irken-like oval)
    hx,hy,hrx,hry = 256,205,105,95
    if in_ellipse(x,y,hx,hy,hrx,hry):
        # skin shading
        ht = (y-(hy-hry))/(2*hry)
        c = lerp(GREEN, GREEN_DARK, ht*0.45)
        # slight highlight
        if in_ellipse(x,y,hx-25,hy-30,30,18): c = lerp(c,(200,255,170),0.25)

    # eyes (big, Invader Zim inspired stylized)
    # left eye socket
    if in_ellipse(x,y,210,200,42,52):
        c = (245,245,240)
        # iris/pupil magenta
        if in_ellipse(x,y,218,208,20,26): c = MAGENTA
        if in_ellipse(x,y,222,212,9,12): c = BLACK
        if in_ellipse(x,y,226,204,5,6): c = WHITE
    # right eye
    if in_ellipse(x,y,302,200,42,52):
        c = (245,245,240)
        if in_ellipse(x,y,294,208,20,26): c = MAGENTA
        if in_ellipse(x,y,290,212,9,12): c = BLACK
        if in_ellipse(x,y,286,204,5,6): c = WHITE

    # mouth/scowl line
    # curved-ish simple line area
    mx,my = 256,262
    if 220 <= x <= 292 and 255 <= y <= 275:
        yy = 262 + 6*math.sin((x-220)/72*math.pi)
        if abs(y-yy) <= 3.2: c = (40,80,35)

    # collar/PAK hint
    if in_ellipse(x,y,256,330,120,45) and y > 285:
        c = lerp(PURPLE, (70,30,100), (y-285)/90)
        if in_ellipse(x,y,256,325,40,18): c = lerp(c, MAGENTA, 0.5)

    # text banner background
    if in_rect(x,y,70,372,442,468):
        c = lerp((12,18,14),(18,28,20),(y-372)/96)
        # border glow
        if x<74 or x>438 or y<376 or y>464: c = lerp(c, GREEN, 0.25)

    # ZIMTV text
    if (int(x),int(y)) in TEXT:
        c = GREEN
        # slight top highlight
        if (int(x),int(y-1)) not in TEXT: c = (200,255,170)

    return c

def main(path):
    raw=bytearray()
    for y in range(H):
        raw.append(0)
        for x in range(W):
            acc=[0.0,0.0,0.0]
            for sy in range(SS):
                for sx in range(SS):
                    col=sample(x+(sx+0.5)/SS, y+(sy+0.5)/SS)
                    acc[0]+=col[0]; acc[1]+=col[1]; acc[2]+=col[2]
            n=SS*SS
            raw.extend(bytes(clamp(v/n) for v in acc))
    def chunk(kind,data):
        c=struct.pack('>I',len(data))+kind+data
        return c+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',W,H,8,2,0,0,0))
    png+=chunk(b'IDAT',zlib.compress(bytes(raw),9))+chunk(b'IEND',b'')
    with open(path,'wb') as f: f.write(png)
    print('wrote', path)

if __name__=='__main__':
    main(sys.argv[1] if len(sys.argv)>1 else 'assets/icon0.png')
