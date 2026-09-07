#!/usr/bin/env python3
"""Borlash City / A-07. Rebuild with Blender --background --factory-startup --python.

PDF pp21-30 is the dimensional authority; its SVG source was used to transcribe
coordinates, checked against the rendered PDF. SVG (330,330) becomes (0,0),
1 SVG unit = 1 metre: wall 130..530 =400, docks 38..622 =584, market 72x44.
Concept art supplies silhouette vocabulary, never economic rules or dimensions.
This is a city exterior blockout; solid building masses are intentional.
"""
import argparse
from collections import defaultdict, deque
import hashlib
import json
import math
from pathlib import Path
import sys
import time

import bpy
from mathutils import Vector
sys.path.insert(0, str(Path(__file__).resolve().parent))
from greybox_lib import point_at, setup_workbench, render_to

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUT = Path('D:/Documents/SpaceMMOAssets/Blender/Stations/BorlashCity')
DEFAULT_ENGINE = ROOT / 'client/RawContent/Stations/A07_BorlashCity'
# key suffix, collection, SVG x/y/w/d, max height, visual type
SCHEDULE = [
 ('administration','Core',300,260,60,38,22,'admin'),
 ('market','Trading',206,204,72,44,25,'market'),
 ('depot_n','Trading',206,272,48,30,15,'depot'),
 ('depot_s','Refining',416,424,44,28,15,'depot'),
 ('forgehall','Crafting',206,362,72,44,23,'forge'),
 ('refinehall','Refining',386,362,72,44,27,'refine'),
 ('guild_craft','Crafting',210,430,56,34,21,'guild'),
 ('guild_refine','Refining',352,430,56,34,21,'guild'),
 ('hq','Civil',372,196,60,48,45,'hq'),
 ('hotel','Civil',366,272,40,26,20,'hotel'),
 ('apartments','Civil',444,240,44,30,24,'apartments'),
 ('pub','Civil',412,274,30,22,12,'pub'),
 ('square','Core',306,306,48,48,8,'square'),
 ('dock_nw','Trading',38,50,64,40,14,'dock'),
 ('dock_ne','Civil',558,50,64,40,14,'dock'),
 ('dock_sw','Crafting',38,570,64,40,14,'dock'),
 ('dock_se','Refining',558,570,64,40,14,'dock'),
]
COLORS = {'Stone':(.49,.55,.57,1), 'Dark':(.12,.19,.23,1),
 'Roof':(.22,.34,.39,1), 'Trim':(.59,.48,.29,1), 'Glass':(.12,.57,.65,1),
 'Ground':(.26,.30,.29,1), 'Paving':(.43,.48,.47,1),
 'Garden':(.20,.31,.24,1), 'Water':(.09,.36,.43,1)}
ANCHORS = {'Core':(0,0,0), 'Trading':(-88,104,0), 'Civil':(72,110,0),
           'Crafting':(-88,-54,0), 'Refining':(92,-54,0)}
FACES = [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)]
DATA = defaultdict(lambda: [[],[]])
HULLS = defaultdict(list)
GROUND_OBS = []
SUPPORT = []
SITES = {}
OBJECTS = {}
COLLECTIONS = {}
SURFACES = defaultdict(list)


def convex_xy(points):
    p = sorted(set((round(v[0],8),round(v[1],8)) for v in points))
    def cross(o,a,b): return (a[0]-o[0])*(b[1]-o[1])-(a[1]-o[1])*(b[0]-o[0])
    lo=[]; hi=[]
    for v in p:
        while len(lo)>1 and cross(lo[-2],lo[-1],v)<=0: lo.pop()
        lo.append(v)
    for v in reversed(p):
        while len(hi)>1 and cross(hi[-2],hi[-1],v)<=0: hi.pop()
        hi.append(v)
    return lo[:-1]+hi[:-1]


def cut_half(poly,a,b,keep_inside):
    def value(p): return (b[0]-a[0])*(p[1]-a[1])-(b[1]-a[1])*(p[0]-a[0])
    out=[]
    for u,v in zip(poly,poly[1:]+poly[:1]):
        fu,fv=value(u),value(v)
        iu=fu>=-1e-8 if keep_inside else fu<=1e-8
        iv=fv>=-1e-8 if keep_inside else fv<=1e-8
        if iu: out.append(u)
        if iu!=iv and abs(fu-fv)>1e-10:
            t=fu/(fu-fv); out.append((u[0]+t*(v[0]-u[0]),u[1]+t*(v[1]-u[1])))
    return out


def subtract(poly,clip):
    # Convex difference partitioned into disjoint convex pieces. No external dependency.
    if (max(x for x,y in poly)<=min(x for x,y in clip)+1e-7 or
        max(x for x,y in clip)<=min(x for x,y in poly)+1e-7 or
        max(y for x,y in poly)<=min(y for x,y in clip)+1e-7 or
        max(y for x,y in clip)<=min(y for x,y in poly)+1e-7): return [poly]
    remainder=poly; pieces=[]
    for a,b in zip(clip,clip[1:]+clip[:1]):
        if len(remainder)<3: break
        outside=cut_half(remainder,a,b,False)
        if len(outside)>=3: pieces.append(outside)
        remainder=cut_half(remainder,a,b,True)
    return pieces


def emit(district, part, mat, verts, faces, solid=True):
    group = 'SM_Borlash_'+district+'_'+part+'_'+mat
    z0=min(v[2] for v in verts); z1=max(v[2] for v in verts)
    if solid:
        HULLS[group].append((district,verts,faces))
        if z0<1.80 and z1>.08: GROUND_OBS.append(convex_xy(verts))
        if abs(z1)<1e-5: SUPPORT.append(convex_xy(verts))
    key=(district,group,mat)
    vv,ff=DATA[key]
    if mat in ('Ground','Paving'):
        poly=convex_xy(verts); pieces=[poly]
        prior=SURFACES[(mat,round(z1,4))]
        for clip in prior:
            pieces=[p for item in pieces for p in subtract(item,clip)]
        prior.append(poly)
        for p in pieces:
            area=abs(sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(p,p[1:]+p[:1])))/2
            if area<1e-6: continue
            n=len(p); offset=len(vv)
            vv.extend((x,y,z) for z in (z0,z1) for x,y in p)
            f=[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
            ff.extend(tuple(offset+i for i in face) for face in f)
    else:
        offset=len(vv); vv.extend(verts)
        ff.extend(tuple(offset+i for i in face) for face in faces)
    return group


def box(dist,part,mat,x,y,w,d,z0,z1,angle=0,solid=True):
    c,s=math.cos(angle),math.sin(angle)
    ring=[(-w/2,-d/2),(w/2,-d/2),(w/2,d/2),(-w/2,d/2)]
    verts=[(x+c*a-s*b,y+s*a+c*b,z) for z in [z0,z1] for a,b in ring]
    return emit(dist,part,mat,verts,FACES,solid)


def frustum(dist,part,mat,x,y,r0,r1,z0,z1,n=16,solid=True,aspect=1):
    verts=[(x+r*math.cos(2*math.pi*i/n),y+aspect*r*math.sin(2*math.pi*i/n),z)
           for r,z in [(r0,z0),(r1,z1)] for i in range(n)]
    faces=[tuple(reversed(range(n))),tuple(range(n,2*n))]
    faces += [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    return emit(dist,part,mat,verts,faces,solid)


def dome(dist,part,x,y,r,z,h):
    # Convex stacked frusta; each owns its exterior band and collision.
    steps=6
    for j in range(steps):
        a=j*math.pi/(2*steps); b=(j+1)*math.pi/(2*steps)
        frustum(dist,part,'Roof',x,y,r*math.cos(a),max(.12,r*math.cos(b)),
                z+h*math.sin(a),z+h*math.sin(b),24)


def path_box(dist,part,mat,a,b,width,z0=-.7,z1=0,solid=True):
    dx=b[0]-a[0]; dy=b[1]-a[1]
    return box(dist,part,mat,(a[0]+b[0])/2,(a[1]+b[1])/2,
               math.hypot(dx,dy),width,z0,z1,math.atan2(dy,dx),solid)


def ring(dist,part,mat,x,y,ri,ro,z0,z1,n=128,solid=False):
    for i in range(n):
        a=2*math.pi*i/n; b=2*math.pi*(i+1)/n
        xy=[(x+ri*math.cos(a),y+ri*math.sin(a)),
            (x+ro*math.cos(a),y+ro*math.sin(a)),
            (x+ro*math.cos(b),y+ro*math.sin(b)),
            (x+ri*math.cos(b),y+ri*math.sin(b))]
        emit(dist,part,mat,[(u,v,z) for z in (z0,z1) for u,v in xy],FACES,solid)


def street(a,b,width=8):
    # Decorative surface is deliberately 3 cm above the collision floor.
    # No sidewalk risers: the character does not currently step up.
    path_box('Core','Streets','Paving',a,b,width,-.02,.03,False)


def build_site(row):
    key,dist,sx,sy,w,d,h,kind=row
    x=sx+w/2-330; y=330-sy-d/2
    station='station_capital_hub' if key=='administration' else 'station_borlash_'+key
    # Ground plate is exactly the scheduled envelope. Superstructure stays within it.
    group=box(dist,key+'_Footprint','Ground' if kind=='dock' else 'Stone',x,y,w,d,-.6,0 if kind=='dock' else .04)
    SITES[key]={'station_key':station,'district':dist,'center_m':[x,y,0],
        'footprint_m':[w,d],'height_m':h,'kind':kind,'plate_mesh':group,
        'source':'A-07 PDF station schedule pp22-23; SVG location checked against PDF'}
    if kind=='square':
        ring(dist,key,'Trim',x,y,14,14.65,-.05,.06,64)
        ring(dist,key,'Stone',x,y,6.2,7,-.08,.8,48,True)
        frustum(dist,key,'Water',x,y,6.18,6.18,-.1,.36,48,False)
        frustum(dist,key,'Stone',x,y,2.1,1.1,-.1,5.7)
        frustum(dist,key,'Trim',x,y,1.8,.12,5.6,8)
        return
    if kind=='dock':
        # Cradle fits the plan's 10 x 8 shuttle. Open apron and walk-out to perimeter road.
        for sign in [-1,1]:
            box(dist,key,'Dark',x+sign*27,y,6,34,-.1,5)
            frustum(dist,key,'Stone',x+sign*26,y+13,3,2.2,4.9,11)
            frustum(dist,key,'Trim',x+sign*26,y+13,2.4,.2,10.9,14)
        ring(dist,key,'Trim',x,y,12.1,12.5,-.05,.05,64)
        for sign in [-1,1]: box(dist,key,'Dark',x+sign*11,y+15,10,6,-.1,4)
        for sign in [-1,1]: box(dist,key,'Glass',x+sign*5.7,y,0.35,8,-.05,.05,solid=False)
        box(dist,key,'Glass',x,y-4.2,10,.25,-.05,.05,solid=False)
        return
    baseh={'hq':12,'market':12,'forge':12,'refine':12,'admin':12,
           'depot':8,'guild':10,'hotel':10,'apartments':10,'pub':6}[kind]
    # Main mass is a closed exterior study, with a dark entry panel (not an open door).
    box(dist,key,'Stone',x,y,w-2,d-2,-.1,baseh)
    box(dist,key,'Trim',x,y,w-1,d-1,baseh-.2,baseh+.5)
    doorw=min(7,w/4)
    box(dist,key,'Dark',x,y-d/2+.91,doorw,.22,-.05,5)
    box(dist,key,'Roof',x,y-d/2+1.5,doorw+2,2.4,5,5.7)
    # Engaged buttresses are fully seated in wall, no narrow pedestrian slots.
    for sign in [-1,1]:
        for fraction in [-.34,-.16,.16,.34]:
            box(dist,key,'Dark',x+fraction*w,y+sign*(d/2-.7),1.5,1.3,-.08,baseh+.1)
    if kind in ('market','admin','pub','depot'):
        r=min(w*.31,d*.39)
        frustum(dist,key,'Dark',x,y,r,r,baseh+.3,baseh+2)
        top=h-2
        dome(dist,key,x,y,r,baseh+1.9,top-baseh-1.9)
        frustum(dist,key,'Trim',x,y,1.5,.12,top-.1,h)
        if kind=='market':
            for sign in [-1,1]:
                for q in [-1,1]:
                    frustum(dist,key,'Dark',x+sign*(w/2-5),y+q*(d/2-5),3,2.4,baseh-.1,baseh+4)
                    dome(dist,key,x+sign*(w/2-5),y+q*(d/2-5),2.5,baseh+3.9,2.5)
    elif kind=='hq':
        for dx,dy,r,top in [(0,2,8.5,45),(-18,7,5.2,35),(18,7,5.2,38),(-12,-12,4,29),(12,-12,4,31)]:
            frustum(dist,key,'Stone',x+dx,y+dy,r,r*.72,baseh-.1,top-8)
            frustum(dist,key,'Dark',x+dx,y+dy,r*.74,r*.74,top-9,top-5)
            frustum(dist,key,'Roof',x+dx,y+dy,r*.83,.3,top-5.1,top)
            for angle in [0,math.pi/2,math.pi,3*math.pi/2]:
                frustum(dist,key,'Glass',x+dx+r*.73*math.cos(angle),y+dy+r*.73*math.sin(angle),.38,.25,baseh+2,top-7,6)
    elif kind in ('forge','refine'):
        for dx in [-w*.27,0,w*.27]:
            r=8 if kind=='forge' else 6
            frustum(dist,key,'Dark',x+dx,y+1,r,r,baseh-.1,baseh+4)
            dome(dist,key,x+dx,y+1,r,baseh+3.9,4)
        for dx in [-w*.36,w*.36]:
            frustum(dist,key,'Stone',x+dx,y+d*.28,3.2,2.5,baseh-.1,h-2)
            frustum(dist,key,'Trim',x+dx,y+d*.28,3,3,h-2.1,h)
    elif kind=='guild':
        box(dist,key,'Roof',x,y,w*.58,d*.66,baseh+.2,14)
        dome(dist,key,x,y,min(d*.28,10),13.9,4)
        for dx in [-w*.38,w*.38]:
            frustum(dist,key,'Stone',x+dx,y,3.8,3,baseh-.1,h-4)
            frustum(dist,key,'Roof',x+dx,y,3.8,.12,h-4.1,h)
    else:
        for dx,dy,top in [(-w*.28,0,h-3),(0,d*.12,h),(w*.28,0,h-1)]:
            box(dist,key,'Stone',x+dx,y+dy,w*.22,d*.6,baseh-.1,top-3)
            box(dist,key,'Dark',x+dx,y+dy,w*.23,d*.61,top-3.2,top-2.5)
            dome(dist,key,x+dx,y+dy,min(w*.105,4.5),top-2.6,2.6)


def build_city():
    # Clipped square 400 m wall centrelines. Four 8.49 m corner posterns from the plan.
    poly=[(-200,-180),(-180,-200),(180,-200),(200,-180),
          (200,180),(180,200),(-180,200),(-200,180)]
    # Ground is convex, so a single UCX hull faithfully holds the entire city.
    n=len(poly); verts=[(x,y,z) for z in [-2,0] for x,y in poly]
    faces=[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    emit('Core','Foundation','Ground',verts,faces)
    # Ring matches A-07 r188..200. Outer 2m meets the wall at gates; pedestrian route is r194.
    ring('Core','RingRoad','Paving',0,0,188,200,-.02,.03,256)
    for a,b in [((-220,0),(220,0)),((0,-220),(0,-24)),((0,70),(0,220))]: street(a,b,16)
    # Admin interrupts north axis: two 8m bypasses, matching the drawn branches.
    for s in [-1,1]:
        street((0,82),(s*38,82),8); street((s*38,82),(s*38,18),8); street((s*38,18),(0,18),8)
    # District side streets connect every frontage to the central circulation.
    for y,xend in [(74,-150),(21,-150),(-84,-150),(-142,-150),(78,112),(54,172),(21,112),(-84,150),(-142,150)]:
        street((0,y),(xend,y),8)
    for x in [-148,172]: street((x,-120),(x,100),8)
    # Square perimeter road and dock links are ground-level, no step-up requirement.
    for s in [-1,1]:
        path_box('Core','PerimeterRoad','Ground',(-260,s*220),(260,s*220),12)
        path_box('Core','PerimeterRoad','Ground',(s*220,-260),(s*220,260),12)
        path_box('Core','GateLink','Ground',(s*190,0),(s*220,0),28)
        path_box('Core','GateLink','Ground',(0,s*190),(0,s*220),28)
    for sx in [-1,1]:
        for sy in [-1,1]:
            dist={(-1,1):'Trading',(1,1):'Civil',(-1,-1):'Crafting',(1,-1):'Refining'}[sx,sy]
            for a,b in [((sx*260,sy*260),(sx*260,sy*220)),((sx*260,sy*220),(sx*220,sy*220)),
                        ((sx*220,sy*260),(sx*260,sy*260)),((sx*220,sy*220),(sx*133,sy*133))]:
                path_box(dist,'DockRoad','Ground',a,b,8)
            # each half of an outer wall belongs to its district
            for a,b in [((sx*14,sy*200),(sx*180,sy*200)),((sx*200,sy*14),(sx*200,sy*180)),
                        ((sx*180,sy*200),(sx*187,sy*193)),((sx*193,sy*187),(sx*200,sy*180))]:
                path_box(dist,'Wall','Stone',a,b,2,-.1,11.6)
                path_box(dist,'WallCoping','Dark',a,b,2.7,11.5,12)
            for u in [45,85,125,165]:
                for x,y in [(sx*u,sy*200),(sx*200,sy*u)]:
                    box(dist,'WallPiers','Dark',x,y,4,4,-.1,12)
            # Corner marker towers stop below HQ.
            frustum(dist,'CornerTower','Stone',sx*180,sy*190,4.5,3.5,-.1,15)
            frustum(dist,'CornerTower','Roof',sx*180,sy*190,4,.1,14.9,19)
    # Four gates: 28x16 plan envelope; two 6m towers leave an actual 16m clear opening.
    # Ring centreline detours inward behind each gate tower, rather than through it.
    for a in [0,math.pi/2,math.pi,3*math.pi/2]:
        c,s=math.cos(a),math.sin(a)
        pts=[(192,-28),(182,-22),(182,22),(192,28)]
        pts=[(c*x-s*y,s*x+c*y) for x,y in pts]
        for u,v in zip(pts,pts[1:]): street(u,v,12)
    for angle,label in [(0,'South'),(math.pi/2,'East'),(math.pi,'North'),(3*math.pi/2,'West')]:
        c,s=math.cos(angle),math.sin(angle)
        for dx in [-11,11]:
            x=c*dx+s*200; y=s*dx-c*200
            box('Core','Gate'+label,'Stone',x,y,6,16,-.1,16,angle)
            frustum('Core','Gate'+label,'Dark',x,y,2.8,2.2,15.9,18)
            frustum('Core','Gate'+label,'Roof',x,y,2.9,.15,17.9,20)
        box('Core','Gate'+label,'Dark',s*200,-c*200,16,6,10,12,angle)
    for row in SCHEDULE: build_site(row)
    # Courtyard landscape stays away from measured movement corridors.
    for x,y in [(-56,132),(-150,40),(-158,-26),(30,-100),(137,-103),(28,132)]:
        box('Core','Gardens','Garden',x,y,12,10,-.1,.35)
        frustum('Core','GardenObelisk','Stone',x,y,1.1,.4,.3,4,8)
    # Shallow flush avenue inlay, separate from collision. No elevated kerbs.
    for y in [-165,-130,-100]:
        box('Core','AvenueInlays','Trim',0,y,1.1,12,.025,.045,solid=False)


def inside(poly,x,y,margin=0):
    # Conservative convex inflation. CCW halfplanes; margin expands each edge.
    return all((b[0]-a[0])*(y-a[1])-(b[1]-a[1])*(x-a[0]) >= -margin*math.hypot(b[0]-a[0],b[1]-a[1])-1e-7
               for a,b in zip(poly,poly[1:]+poly[:1]))


def route_checks():
    # Rasterize the BUILT collision hulls, not the station schedule. A 1m cell
    # is conservatively expanded by its diagonal plus half the required 1.2m route.
    clearance=.60; cell=1; pad=clearance+math.sqrt(.5)
    support=set(); blocked=set()
    for polygons,target,margin in [(SUPPORT,support,-math.sqrt(.5)),(GROUND_OBS,blocked,pad)]:
        for p in polygons:
            xmin=math.floor(min(x for x,y in p)-abs(margin)); xmax=math.ceil(max(x for x,y in p)+abs(margin))
            ymin=math.floor(min(y for x,y in p)-abs(margin)); ymax=math.ceil(max(y for x,y in p)+abs(margin))
            for x in range(xmin,xmax+1):
                for y in range(ymin,ymax+1):
                    if inside(p,x,y,margin): target.add((x,y))
    free=support-blocked
    start=(0,-18); assert start in free
    distance={start:0}; q=deque([start])
    while q:
        x,y=q.popleft()
        for p in [(x-1,y),(x+1,y),(x,y-1),(x,y+1)]:
            if p in free and p not in distance: distance[p]=distance[(x,y)]+1; q.append(p)
    access={}
    for key,site in SITES.items():
        x,y,_=site['center_m']; w,d=site['footprint_m']
        target=(round(x),round(y if site['kind']=='dock' else y-d/2-3))
        if key=='square': target=start
        assert target in distance, ('Unreachable frontage',key,target)
        access[key]={'arrival_xy_m':target,'grid_route_to_square_m':distance[target]}
    for name,target in [('SouthGate',(0,-210)),('NorthGate',(0,210)),('WestGate',(-210,0)),('EastGate',(210,0))]:
        assert target in distance, ('Unreachable gate',name)
    # Entire circular centreline must connect; specifically catches wall/gate pinches.
    for i in range(720):
        a=2*math.pi*i/720
        nearest=abs((a+math.pi/4)%(math.pi/2)-math.pi/4)
        radius=182 if nearest<.12 else (182+12*(nearest-.12)/.06 if nearest<.18 else 194)
        p=(round(radius*math.cos(a)),round(radius*math.sin(a)))
        assert p in distance, ('Ring road pinched',p)
    # Negative control: a point inside the constructed market must not pass.
    assert (-88,104) not in free
    print('PASS collision-derived circulation: 17 frontages, 4 gates, full ring; >=1.20m route width')
    return {'minimum_route_width_m':1.2,'pawn_diameter_m':.68,'pawn_height_m':1.8,
            'method':'1m conservative ground grid from generated convex collision, 0.60m clearance plus cell diagonal',
            'limitations':'Ring route detours inward at gate towers. Exterior ground routes only; no interiors. Manhattan route lengths are upper estimates, not the PDF 85s assertion.',
            'reachable_cells':len(distance),'frontages':access}


def material(name,color):
    m=bpy.data.materials.new('MAT_Borlash_'+name); m.diffuse_color=color; m.use_nodes=True
    p=m.node_tree.nodes.get('Principled BSDF'); p.inputs['Base Color'].default_value=color
    p.inputs['Roughness'].default_value=.72
    if name in ('Trim','Roof'): p.inputs['Metallic'].default_value=.35
    if name=='Glass':
        p.inputs['Emission Color'].default_value=color; p.inputs['Emission Strength'].default_value=.45
    return m


def make_mesh(name,verts,faces,coll,mat=None):
    mesh=bpy.data.meshes.new(name); mesh.from_pydata(verts,[],faces); mesh.update()
    assert not mesh.validate(verbose=False), ('Invalid geometry',name)
    obj=bpy.data.objects.new(name,mesh); coll.objects.link(obj)
    if mat: mesh.materials.append(mat)
    return obj


def realize():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene=bpy.context.scene; scene.unit_settings.system='METRIC'; scene.unit_settings.scale_length=1
    scene.unit_settings.length_unit='METERS'
    mats={k:material(k,v) for k,v in COLORS.items()}
    root=bpy.data.collections.new('BORLASH_CITY_A07'); scene.collection.children.link(root)
    for d in ANCHORS:
        coll=bpy.data.collections.new(d); root.children.link(coll); COLLECTIONS[d]=coll
    for (dist,group,mat),(verts,faces) in DATA.items():
        assert group not in OBJECTS
        obj=make_mesh(group,verts,faces,COLLECTIONS[dist],mats[mat]); OBJECTS[group]=obj
    # Convex collision includes each curved frustum and rotated wall, not just boxes.
    for dist in ANCHORS:
        coll=bpy.data.collections.new('UCX_'+dist); root.children.link(coll); COLLECTIONS['UCX_'+dist]=coll
    for group,hulls in HULLS.items():
        for i,(dist,verts,faces) in enumerate(hulls):
            obj=make_mesh('UCX_'+group+'_'+str(i).zfill(3),verts,faces,COLLECTIONS['UCX_'+dist])
            obj.display_type='WIRE'; obj.hide_render=True
    # Durable semantic stations and named inspection cameras remain in Blender only.
    markers=bpy.data.collections.new('Station_Anchors'); root.children.link(markers)
    for key,site in SITES.items():
        obj=bpy.data.objects.new(site['station_key'],None); obj.location=site['center_m']
        obj.empty_display_type='PLAIN_AXES'; obj.empty_display_size=3; markers.objects.link(obj)
        obj['station_key']=site['station_key']; obj['phase']='exterior blockout; no authored gameplay row'
    return scene


def verify_built():
    measured={}
    for row in SCHEDULE:
        key,dist,sx,sy,w,d,h,kind=row; site=SITES[key]; obj=OBJECTS[site['plate_mesh']]
        actual=[max(v.co[i] for v in obj.data.vertices)-min(v.co[i] for v in obj.data.vertices) for i in range(3)]
        assert abs(actual[0]-w)<1e-4 and abs(actual[1]-d)<1e-4,(key,actual)
        meshes=[o for n,o in OBJECTS.items() if n.startswith('SM_Borlash_'+dist+'_'+key+'_')]
        top=max(v.co.z for o in meshes for v in o.data.vertices)
        assert abs(top-h)<1e-3,(key,top,h)
        measured[key]={'footprint_m':actual[:2],'built_height_m':top}
    # Exact duplicate polygon check, all orientations/materials, on built render mesh.
    seen={}; duplicates=[]
    for name,obj in OBJECTS.items():
        for p in obj.data.polygons:
            verts=tuple(sorted(tuple(round(c,4) for c in obj.data.vertices[i].co) for i in p.vertices))
            normal=tuple(round(c,4) for c in p.normal)
            k=(verts,normal)
            if k in seen and seen[k][1]!=obj.data.materials[0].name:
                duplicates.append((seen[k][0],name))
            seen[k]=(name,obj.data.materials[0].name)
    assert not duplicates,duplicates[:10]
    names=set(OBJECTS); matnames={m.name for m in bpy.data.materials}; assert not names & matnames
    coords=[v.co for o in OBJECTS.values() for v in o.data.vertices]
    bounds=[[min(v[i] for v in coords),max(v[i] for v in coords)] for i in range(3)]
    assert abs(bounds[0][1]-bounds[0][0]-584)<.001,bounds
    assert abs(bounds[1][1]-bounds[1][0]-560)<.001,bounds
    assert abs(bounds[2][1]-45)<.001,bounds
    print('PASS built mesh: 17 scheduled footprints/heights; 584x560m overall, HQ 45m; distinct names; no exact duplicate cross-material faces')
    return {'sites':measured,'bounds_m':bounds,'render_meshes':len(OBJECTS),
        'faces':sum(len(o.data.polygons) for o in OBJECTS.values()),
        'convex_hulls':sum(len(v) for v in HULLS.values()),
        'coplanar_check':'Exact duplicate polygons only, including rotated shapes. Partial polygon overlaps are not exhaustively proven; visually inspect previews and orbit in Blender.'}


def exports(engine):
    engine.mkdir(parents=True,exist_ok=True)
    results={}
    # District meshes are authored relative to their anchor on export; all transforms restored.
    for dist,anchor in ANCHORS.items():
        objects=list(COLLECTIONS[dist].objects)+list(COLLECTIONS['UCX_'+dist].objects)
        bpy.ops.object.select_all(action='DESELECT')
        for o in objects: o.select_set(True); o.location=-Vector(anchor)
        path=engine/('Borlash_'+dist+'.fbx')
        bpy.ops.export_scene.fbx(filepath=str(path),use_selection=True,object_types={'MESH'},
            apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',
            axis_forward='-Z',axis_up='Y',mesh_smooth_type='FACE',bake_anim=False)
        for o in objects: o.location=(0,0,0); o.select_set(False)
        results[dist]={'file':path.name,'city_local_translation_m':anchor,'mesh_count':len(COLLECTIONS[dist].objects),
                       'convex_hulls':len(COLLECTIONS['UCX_'+dist].objects)}
    return results


def verify_exports(engine):
    results={}
    for dist,anchor in ANCHORS.items():
        expected={}
        for (owner,name,mat),(vv,ff) in DATA.items():
            if owner==dist:
                expected[name]=[[min(v[i] for v in vv)-anchor[i],max(v[i] for v in vv)-anchor[i]] for i in range(3)]
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.context.scene.unit_settings.system='METRIC'
        bpy.context.scene.unit_settings.scale_length=1
        bpy.ops.import_scene.fbx(filepath=str(engine/('Borlash_'+dist+'.fbx')))
        imported={o.name:o for o in bpy.context.scene.objects if o.type=='MESH'}
        renders={n:o for n,o in imported.items() if not n.startswith('UCX_')}
        assert set(renders)==set(expected), (dist,'render name mismatch',set(renders)^set(expected))
        max_error=0
        for n,obj in renders.items():
            verts=[obj.matrix_world @ v.co for v in obj.data.vertices]
            actual=[[min(v[i] for v in verts),max(v[i] for v in verts)] for i in range(3)]
            error=max(abs(actual[i][j]-expected[n][i][j]) for i in range(3) for j in range(2))
            max_error=max(error,max_error)
            assert error<.002,(dist,n,'FBX bounds/anchor mismatch',error)
        hulls={n:o for n,o in imported.items() if n.startswith('UCX_')}
        expected_hulls={f'UCX_{group}_{i:03d}' for group,items in HULLS.items()
                        for i,(owner,v,f) in enumerate(items) if owner==dist}
        assert set(hulls)==expected_hulls,(dist,'collision names/count mismatch')
        for name in hulls: assert name[4:].rsplit('_',1)[0] in renders,name
        results[dist]={'render_meshes':len(renders),'convex_hulls':len(hulls),
                       'max_bounds_error_m':max_error,'status':'PASS Blender FBX roundtrip'}
        print('PASS FBX roundtrip',dist,len(renders),'meshes',len(hulls),'hulls',max_error,'m error')
    return results


def previews(out):
    scene=bpy.context.scene
    coll=bpy.data.collections.new('Review_Cameras'); scene.collection.children.link(coll)
    def camera(name,loc,target,ortho=None,lens=38):
        d=bpy.data.cameras.new(name); o=bpy.data.objects.new(name,d); coll.objects.link(o)
        o.location=loc; point_at(o,target); d.lens=lens; d.clip_start=.3; d.clip_end=5000
        if ortho: d.type='ORTHO'; d.ortho_scale=ortho
        return o
    hero=camera('01_City_Overview',(555,-700,610),(0,5,0),920)
    plan=camera('02_Plan_North_Up',(0,0,900),(0,0,0),650)
    arrival=camera('03_South_Arrival',(0,-218,1.7),(0,-25,10),lens=24)
    square=camera('04_Town_Square',(-20,-22,1.7),(15,72,18),lens=24)
    district=camera('05_Trading_Quarter',(-177,7,80),(-77,90,6),200)
    scene.world=bpy.data.worlds.new('Borlash_Sky')
    setup_workbench(); scene.world.color=(.12,.12,.12)
    s=scene.display.shading; s.background_type='WORLD'; s.studiolight_rotate_z=.35
    s.show_object_outline=False; s.show_shadows=False; s.show_cavity=True; s.cavity_type='BOTH'
    scene.view_settings.view_transform='Standard'
    scene.view_settings.exposure=1.25
    for cam,name,w,h in [(hero,'Borlash_Overview',1800,1400),(plan,'Borlash_Plan',1700,1700),(district,'Borlash_Trading',1500,1100)]:
        scene.camera=cam; render_to(str(out/(name+'.png')),w,h)
    scene.render.engine='CYCLES'; scene.cycles.samples=24; scene.cycles.use_denoising=True
    scene.world.use_nodes=True
    scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.5,.64,.8,1)
    scene.world.node_tree.nodes['Background'].inputs[1].default_value=.45
    sun_data=bpy.data.lights.new('Daylight',type='SUN'); sun_data.energy=2.5; sun_data.angle=.15
    sun=bpy.data.objects.new('Daylight',sun_data); scene.collection.objects.link(sun)
    sun.rotation_euler=(math.radians(25),math.radians(-25),math.radians(-30))
    scene.view_settings.view_transform='AgX'
    scene.view_settings.exposure=0
    for cam,name in [(arrival,'Borlash_Arrival'),(square,'Borlash_Square')]:
        scene.camera=cam; render_to(str(out/(name+'.png')),1500,950)
    scene.camera=hero
    for name,coll in COLLECTIONS.items():
        if name.startswith('UCX_'): coll.hide_viewport=True
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=='VIEW_3D':
                area.spaces.active.clip_end=5000; area.spaces.active.region_3d.view_distance=700
                area.spaces.active.region_3d.view_location=(0,0,0)
                area.spaces.active.region_3d.view_rotation=hero.rotation_euler.to_quaternion()
    bpy.ops.wm.save_as_mainfile(filepath=str(out/'BorlashCity.blend'))


def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--out',type=Path,default=DEFAULT_OUT)
    ap.add_argument('--engine-out',type=Path,default=DEFAULT_ENGINE); ap.add_argument('--check-only',action='store_true'); ap.add_argument('--verify-exports-only',action='store_true')
    args=ap.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    # Never create a working Blender project or previews in the game repository.
    assert not args.out.resolve().is_relative_to(ROOT.resolve()), 'Working assets must stay outside repository'
    build_city(); circulation=route_checks(); scene=realize(); built=verify_built()
    if args.check_only: print('ALL CHECKS PASSED'); return
    if args.verify_exports_only:
        result=verify_exports(args.engine_out)
        for f in [args.engine_out/'Borlash_manifest.json',args.out/'Borlash_build_report.json']:
            report=json.loads(f.read_text(encoding='utf-8')); report['fbx_roundtrip']=result
            f.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        return
    args.out.mkdir(parents=True,exist_ok=True)
    export=exports(args.engine_out)
    roundtrip=verify_exports(args.engine_out)
    OBJECTS.clear(); COLLECTIONS.clear(); realize()
    report={'asset':'Borlash City / A-07','phase':'exterior blockout',
        'sources':['Origin Station Plans.pdf pp21-30','CapitalGrandDistrict.png','docs/design-bible.md'],
        'deviations':['Hotel schedule 40m used over drawing 38m.',
          'All heights other than wall12/gate20/HQ45 and silhouette details are design assumptions.',
          'Five district FBXs with local anchors; no gameplay content, Blueprint or terrain changes.',
          'Market is per-station; bank images interpreted as storage depots; no invented loans or NPC supply.',
          '400m wall centrelines retained; 2m wall thickness assumed. Ring bypasses at gates added because the drawn gate towers interrupt the ring.',
          'Interiors are not designed in A-07. All building masses intentionally closed; access validation ends at frontages.'],
        'fbx_roundtrip':roundtrip,'circulation':circulation,'built':built,'district_exports':export,'stations':SITES}
    (args.engine_out/'Borlash_manifest.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    (args.out/'Borlash_build_report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    previews(args.out)
    print('BORLASH COMPLETE',args.out,flush=True)

if __name__=='__main__': main()
