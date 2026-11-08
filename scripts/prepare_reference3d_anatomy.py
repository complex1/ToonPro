#!/usr/bin/env python3
"""Pack pinned CC0 MPFB graphical data for Toon Pro's anatomical study generator.

This independent converter reads only asset files, never MPFB application code.
The packed JSON is SHA256-checked and qCompress-compatible; runtime is offline.
"""
import argparse, gzip, hashlib, json, math, struct, subprocess, zlib
from pathlib import Path
REVISION='6ff1eb358f6c1959ced77e79e6dd2406d9a71f49'

def prepare(source, output):
    revision=subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()
    if revision!=REVISION: raise ValueError('Wrong source revision')
    data=source/'src/mpfb/data'; used={}
    def read(path):
        b=path.read_bytes(); used[str(path.relative_to(source))]=hashlib.sha256(b).hexdigest(); return b
    read(source/'LICENSE.ASSETS.md')
    positions=[]; triangles=[]; groups={}; group=''
    for line in read(data/'3dobjs/base.obj').decode().splitlines():
        f=line.split()
        if not f: continue
        if f[0]=='v': positions.append([float(x) for x in f[1:4]])
        elif f[0]=='g': group=f[1]
        elif f[0]=='f':
            poly=[int(x.split('/')[0])-1 for x in f[1:]];groups.setdefault(group,set()).update(poly)
            if group=='body': triangles += [[poly[0],poly[i],poly[i+1]] for i in range(1,len(poly)-1)]
    ymin=min(positions[i][1] for i in groups['body']); ymax=max(positions[i][1] for i in groups['body']); scale=1.75/(ymax-ymin)
    positions=[[x*scale,(y-ymin)*scale,z*scale] for x,y,z in positions]
    rig=json.loads(read(data/'rigs/standard/rig.mixamo.json'))['bones']
    wd=json.loads(read(data/'rigs/standard/weights.mixamo.json'))
    if wd.get('license')!='CC0': raise ValueError('Weights are not CC0')
    names=['Root','hips','spine','chest','neck','head']; parents=[-1,0,1,2,3,4]
    source_names=['','Hips','Spine','Spine1','Neck','Head']
    for side in ['right','left']:
        offset=len(names); cap=side.capitalize()
        for sem,src,parent in [('Shoulder','Shoulder',3),('UpperArm','Arm',offset),('LowerArm','ForeArm',offset+1),('Hand','Hand',offset+2)]:
            names.append(side+sem);source_names.append(cap+src);parents.append(parent)
        for finger in ['Thumb','Index','Middle','Ring','Little']:
            start=len(names)
            for j,part in enumerate(['Metacarpal','Proximal','Distal'] if finger=='Thumb' else ['Proximal','Intermediate','Distal']):
                names.append(side+finger+part);source_names.append(cap+'Hand'+('Pinky' if finger=='Little' else finger)+str(j+1));parents.append(offset+3 if j==0 else start+j-1)
        start=len(names)
        for j,(sem,src) in enumerate([('UpperLeg','UpLeg'),('LowerLeg','Leg'),('Foot','Foot'),('Toes','ToeBase')]):
            names.append(side+sem);source_names.append(cap+src);parents.append(1 if j==0 else start+j-1)
    full_names={n.split(':')[-1]:n for n in rig}; lookup={s:i for i,s in enumerate(source_names) if s};lookup['Spine2']=3
    anchors=[[]]
    for s in source_names[1:]:
        definition=rig[full_names[s]]['head']; anchors.append(definition['vertex_indices'] if definition['strategy']=='MEAN' else sorted(groups[definition['cube_name']]))
    joints=[[sum(positions[i][a] for i in v)/len(v) for a in range(3)] if v else [0,0,0] for v in anchors]
    influences=[{} for _ in positions]
    for n,values in wd['weights'].items():
        bone=lookup[n.split(':')[-1]]
        for i,w in values:
            if w>0: influences[i][bone]=influences[i].get(bone,0)+w
    discarded=[];weights=[]
    for i,items in enumerate(influences):
        ordered=sorted(items.items(),key=lambda x:(-x[1],x[0])); discarded.append(sum(w for _,w in ordered[4:]))
        kept=ordered[:4];total=sum(w for _,w in kept)
        weights.append([[j,round(w/total,9)] for j,w in kept] if total else [[0,1]])
    # Helpers follow their anatomical joint during custom regional transforms.
    for j,ids in enumerate(anchors):
        for i in ids:
            if i not in groups['body']: weights[i]=[[j,1]]
    def dot(a,b):return sum(x*y for x,y in zip(a,b))
    def sub(a,b):return [x-y for x,y in zip(a,b)]
    def norm(v):
        length=math.sqrt(dot(v,v));return [x/length for x in v]
    def extract(normal,origin,filter_face=lambda f:True):
        samples=[];index={};faces=[];cap_vertices=set()
        def add(recipe):
            recipe=tuple((int(i),round(float(w),9)) for i,w in sorted(recipe) if w>1e-8)
            if recipe not in index:index[recipe]=len(samples);samples.append(recipe)
            return index[recipe]
        for f in triangles:
            if not filter_face(f):continue
            poly=[]
            for n,a in enumerate(f):
                b=f[(n+1)%3];da=dot(sub(positions[a],origin),normal);db=dot(sub(positions[b],origin),normal)
                if da>=0:poly.append(add([(a,1)]))
                if (da>=0)!=(db>=0):
                    t=da/(da-db);poly.append(add([(a,1-t),(b,t)]))
            for n in range(1,len(poly)-1):faces.append([poly[0],poly[n],poly[n+1]])
        # Cap only cut-plane boundary loops; original eyelid/mouth openings remain.
        edges={}
        for a,b,c in faces:
            for x,y in [(a,b),(b,c),(c,a)]:
                key=tuple(sorted((x,y)));edges.setdefault(key,[]).append((x,y))
        def point(i):return [sum(positions[v][a]*w for v,w in samples[i]) for a in range(3)]
        boundary={x:y for items in edges.values() if len(items)==1 for x,y in items if abs(dot(sub(point(x),origin),normal))<1e-6 and abs(dot(sub(point(y),origin),normal))<1e-6}
        while boundary:
            start=next(iter(boundary));loop=[start];cur=boundary.pop(start)
            while cur!=start and cur in boundary:
                loop.append(cur);cur=boundary.pop(cur)
            if cur!=start or len(loop)<3:raise ValueError('Nonmanifold extraction boundary')
            combined={}
            for v in loop:
                for i,w in samples[v]:combined[i]=combined.get(i,0)+w/len(loop)
            center=add(list(combined.items()));cap_vertices.update(loop);cap_vertices.add(center)
            for n,a in enumerate(loop):faces.append([a,center,loop[(n+1)%len(loop)]])
        return {'samples':samples,'triangles':faces,'capVertices':sorted(cap_vertices)}
    head=extract([0,1,0],[0,joints[4][1]+.015,0])
    wrist=joints[names.index('leftHand')];middle=joints[names.index('leftMiddleProximal')];forward=norm(sub(middle,wrist))
    hand=extract(forward,[wrist[a]-forward[a]*.035 for a in range(3)],lambda f: sum(positions[i][0] for i in f)/3>wrist[0]-.10)
    body={'samples':[[[i,1]] for i in sorted(groups['body'])],'triangles':[]}
    body_map={s[0][0]:i for i,s in enumerate(body['samples'])};body['triangles']=[[body_map[i] for i in f] for f in triangles]
    targets={}
    for directory in ['head','chin','eyes','ears','eyebrows','nose','mouth','neck','torso','hip','arms','legs','hands','feet']:
        for path in sorted((data/'targets'/directory).glob('*.target.gz')):
            target=[]
            for line in gzip.decompress(read(path)).decode().splitlines():
                f=line.split()
                if f and not f[0].startswith('#'):
                    i=int(f[0]);delta=[float(v)*scale for v in f[1:4]]
                    if i>=len(positions):raise ValueError('Target vertex out of range')
                    target.append([i,*[round(x,9) for x in delta]])
            targets[directory+'/'+path.name.replace('.target.gz','')]=target
    eyes={s:sorted(groups['joint-'+s+'-eye']) for s in ['l','r']}
    document={'version':1,'positions':positions,'names':names,'parents':parents,'anchors':anchors,'weights':weights,'targets':targets,'body':body,'head':head,'hand':hand,'eyes':eyes}
    raw=json.dumps(document,separators=(',',':'),allow_nan=False).encode();packed=b'TOONANA1'+hashlib.sha256(raw).digest()+struct.pack('>I',len(raw))+zlib.compress(raw,9)
    output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(packed)
    report={'source':'https://github.com/makehumancommunity/mpfb2','revision':revision,'assetLicense':'CC0-1.0','inputSha256':used,'outputSha256':hashlib.sha256(packed).hexdigest(),'uncompressedBytes':len(raw),'packedBytes':len(packed),'sourceVertices':len(positions),'bodyVertices':len(body['samples']),'bodyTriangles':len(body['triangles']),'headVertices':len(head['samples']),'headTriangles':len(head['triangles']),'handVertices':len(hand['samples']),'handTriangles':len(hand['triangles']),'targets':len(targets),'discardedInfluenceVertices':sum(discarded[i]>0 for i in groups['body']),'maxDiscardedWeight':max(discarded[i] for i in groups['body']),'notes':'Graphical CC0 data only. Spine2 weights merged into legacy-compatible chest. Offline top-four normalized weights; fixed cut-plane extraction recipes and neck/wrist caps. Parameter and family recipes authored separately in Toon Pro.'}
    output.with_suffix('.provenance.json').write_text(json.dumps(report,indent=2)+'\n'); print(json.dumps({k:v for k,v in report.items() if k!='inputSha256'},indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source-root',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args();prepare(a.source_root.resolve(),a.output.resolve())
