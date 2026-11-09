#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Source geometry check for the pannable triangle constellation. Not a desktop test.

Reads the actual position arrays and viewport constants. Checks player node
separation at minimum/default/maximum zoom and safe coordinate ranges for the
line renderer. It does not mistake offscreen nodes in a pannable view for bugs.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
def table(source, name):
    m = re.search(name+r'\[\s*\w+\s*\]\s*=\s*\{(.*?)\};',source,re.S)
    if not m: raise ValueError('missing '+name)
    body=re.sub(r'/\*.*?\*/','',m[1],flags=re.S)
    return [float(value.strip().rstrip('f')) for value in body.split(',') if value.strip()]
def constant(source,name):
    m=re.search(r'#define\s+'+name+r'\s+([0-9.]+)',source)
    if not m: raise ValueError('missing '+name)
    return float(m[1])
def main():
    skills=(ROOT/'src/skills.c').read_text(encoding='utf-8')
    ui=(ROOT/'src/ui.c').read_text(encoding='utf-8')
    common=(ROOT/'include/common.h').read_text(encoding='utf-8')
    nx,ny=table(skills,'kNodeNX'),table(skills,'kNodeNY')
    count=int(constant(common,'SKILL_PLAYER_COUNT'))
    definitions=int(constant(common,'SKILL_COUNT'))
    if len(nx)!=definitions or len(ny)!=definitions: raise ValueError('incomplete position table')
    wx,wy=constant(ui,'SK_WX'),constant(ui,'SK_WY')
    nominal=constant(ui,'SK_NODE_K')
    failure=False
    for zoom in (constant(ui,'SK_K_MIN'),nominal,constant(ui,'SK_K_MAX')):
        width=max(26,int(wx*.55*min(zoom,nominal)))
        height=max(18,int(wy*.62*min(zoom,nominal)))
        ids=[i for i in range(definitions) if i!=40]
        points=[(nx[i]*wx*6*zoom,ny[i]*wy*6*zoom) for i in ids]
        bad=[]
        for a in range(count):
            for b in range(a+1,count):
                dx=abs(points[a][0]-points[b][0]);dy=abs(points[a][1]-points[b][1])
                if dx<width and dy<height and (width-dx)*(height-dy)>=width*height*.08: bad.append((ids[a],ids[b]))
        unsafe=[i for i,(x,y) in enumerate(points) if abs(x)>12000 or abs(y)>12000]
        print('zoom %.2f: %d player nodes / %dx%d boxes / overlaps %s / unsafe %s'%(zoom,count,width,height,bad,unsafe))
        failure |= bool(bad or unsafe)
    return int(failure)
if __name__=='__main__':
    try: sys.exit(main())
    except (OSError,ValueError) as error:
        print('ERROR: '+str(error));sys.exit(1)
