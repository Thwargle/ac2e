"""Read-only UI and script metadata catalogue for the installed Legions DATs."""
import json, re, struct
from pathlib import Path
from dat_probe import Archive
from population_probe import Reader

def token(s):
    h = shift = 0
    for c in s.encode():
        v = (c << 25) & 0xffffffff
        h ^= v >> shift
        if shift > 25: h ^= (v << (32-shift)) & 0xffffffff
        shift = (shift+7)&31
    return h

def layout(data, names):
    r=Reader(data)
    def short(): return struct.unpack('<H',r.take(2))[0]
    def byte(): return r.take(1)[0]
    def rect(): return struct.unpack('<4h',r.take(8))
    def named(n): return names.get(n,f'{n:08X}')
    def element(depth=0):
        assert depth<64
        e={'id':named(r.word()),'kind':named(r.word()),'default':named(r.word()),'rect':rect(),'z':short(),'anchors':list(r.take(4))}
        n=short();r.align();e['states']=[]
        for _ in range(n):
            s={'id':named(r.word()),'flags':byte(),'rect':rect(),'z':short()}; r.align();s['attrs']={}
            for _ in range(r.word()):
                k=named(r.word());kind=byte();r.align()
                s['attrs'][k]=r.string() if kind==5 else r.word() if 1<=kind<=7 else None
                r.align()
            s['art']=[]
            for _ in range(r.word()):
                kind=r.word();assert kind==r.word();v={'kind':kind}
                if kind==1:
                    r.take(8);v['offset']=struct.unpack('<2i',r.take(8));v['images']=[f'{r.word():08X}' for _ in range(r.word())]
                elif kind in (3,4,10):
                    v['images']=[f'{r.word():08X}']
                    if kind==3:r.word()
                    v['offset']=struct.unpack('<2i',r.take(8))
                elif kind in (2,6,7):r.take(8)
                elif kind in (5,8):r.word()
                elif kind==9:r.take(8);r.string()
                else:raise ValueError(kind)
                s['art'].append(v)
            for _ in range(r.word()):
                k=r.word();r.take(8);r.take({1:28,2:28,3:20,4:24}.get(k,0))
            s['children']=[element(depth+1) for _ in range(r.word())];r.align();e['states'].append(s)
        r.align();return e
    did=r.word();r.take(r.word()*4);table=r.word();size=[short(),short()];n=short();r.align()
    out={'did':f'{did:08X}','strings':f'{table:08X}','size':size,'elements':[element() for _ in range(n)]}
    assert r.pos==len(data)
    return out

def packages(portal):
    r=Reader(portal.read(0x56000005)); assert r.word()==0x56000005 and r.word()==0x1A015342 and r.word()==15
    out=[]; names={}
    while r.pos<len(r.data):
        k=r.word();b=r.take(r.word());r.align()
        if k==6:
            q=Reader(b)
            for _ in range(2):
                for _ in range(q.word()):q.take(q.word()*4)
            q.take(q.word()*8);q.take(q.word()*4)
            for _ in range(q.word()&0xffffff):
                name=q.string();names[q.word()]=name
        if k!=4:continue
        q=Reader(b)
        for _ in range(q.word()):
            e={'name':q.string(),'base':q.string()};e['words']=[q.word() for _ in range(5)];e['fields']=[]
            for _ in range(q.word()):
                e['fields'].append([q.string(),q.word(),q.take(1)[0]]);q.align()
            for _ in range(q.word()):
                q.string();q.take(16);q.take(q.word()*4)
            out.append(e)
        assert q.pos==len(b)
    for e in out:e['name']=names.get(e['words'][3],e['name'])
    return out

def matrix(portal):
    r=Reader(portal.read(0x70000390));r.word();size=r.word();end=8+size
    ids=[r.word() for _ in range(r.word())];nodes={}
    for rid in ids:
        flags=r.word();e={'flags':flags};nodes[rid]=e
        if flags&0x300!=0x100:e['value']=r.word();continue
        if flags&0x20000000:e['did']=r.word();continue
        if flags&0x8000000 and not flags&0x10000000:continue
        e['state']=r.word();kind=r.word();e['pkg']=kind>>16;e['native']=kind&65535;k=e['native']
        if k in (1,3,0x11):e['pairs']=[[r.word(),r.word()] for _ in range(r.word()&0xffffff)]
        elif k in (5,0x0a,0x61,0x65):e['list']=[r.word() for _ in range(r.word())]
        elif k==0x6a:
            e['string']=[r.word(),r.word()];flags2=r.word();assert flags2==0
        elif k==0xa2:
            e['race_info']=[r.word(),r.word(),r.word()];e['pairs']=[[r.word(),r.word()] for _ in range(r.word()&0xffffff)]
        elif k==0x76:e['text']=r.string(2)
        elif k==0x5a:e['position']=r.take(32).hex()
        elif k:raise ValueError((rid,k,r.pos))
        n=r.word();e['slots']=[r.word() for _ in range(n)];e['tags']=list(r.take(n));r.align()
    assert r.pos==end
    return nodes

if __name__=='__main__':
    directory=Path("Asheron's Call 2 - Legions");portal=Archive(directory/'portal.dat');local=Archive(directory/'local_English.dat')
    names={}
    for path in Path('References/AC2_Rusted/crates/ac2-ui/src').rglob('*.rs'):
        for s in re.findall(r'"([A-Za-z_][A-Za-z_0-9 ]*)"',path.read_text(encoding='utf-8')):names[token(s)]=s
    layouts=[layout(local.chain(a,n),names) for did,a,n,_ in local.entries() if did>>24==0x22]
    ps=packages(portal)
    nodes=matrix(portal)
    output=Path('Unreal/Saved/Diagnostics/AC2UI-source.json');output.write_text(json.dumps({'layouts':layouts,'packages':ps,'matrix':nodes},indent=2))
    print('Matrix root',nodes[0]);print('Matrix roots',[(i,nodes[i]) for i in nodes[0]['slots']])
    for l in layouts:print(l['did'],l['size'],[e['id'] for e in l['elements']][:20])
    for p in ps:
        if any(s in p['name'].lower() for s in ('chargen','racesex','raceinfo','physique','character')):print(p)
