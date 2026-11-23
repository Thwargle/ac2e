"""Real UDP: whole-item world drops, ownership, observers and restart recovery."""
import json
import os
import subprocess
import time
import uuid
from test_network import Peer, ROOT, SERVER, DAT_DIRECTORY, prepare_test_content
from test_gameplay import until


def main():
    data = ROOT / 'Saved/AC2NetworkTests' / ('world-drops-' + uuid.uuid4().hex)
    data.mkdir(parents=True)
    content = prepare_test_content(data)
    args = [str(SERVER), 'serve', '--bind', '127.0.0.1:19482', '--account', 'local',
            '--data', str(data/'world'), '--game-data', str(DAT_DIRECTORY), '--content', str(content),
            '--initialize-ui', 'true', '--seconds', '150']
    peers = []
    log = (data/'server.log').open('w')
    def start():
        server = subprocess.Popen(args, env=dict(os.environ, PYREALIUM_UNREAL_LEGIONS_DATA='1'),
                                  stdout=log, stderr=log, creationflags=0x08000000)
        time.sleep(1)
        assert server.poll() is None
        return server
    def login(name=None, identity=None):
        p = Peer(); peers.append(p)
        p.send('connect', host='127.0.0.1', port=19482, account='local'); p.wait('characters')
        if name:
            p.send('create', name=name, start_area=0, race=1, sex=4096, physical=[[1,1],[2,1]])
            identity = p.wait('created')['character']['id']
        p.id = identity
        p.send('enter', id=identity);p.wait('player')
        p.spawn = p.wait('object', lambda e:e['id']==identity)
        p.send('ready');p.wait('status', lambda e:e['phase']=='world')
        p.bag = until(p,0,'inventory')['items']
        return p
    def stop(server):
        for p in peers:p.close()
        peers.clear()
        (data/'world/stop.request').write_text('stop\n')
        server.wait(timeout=10)
    server = start()
    try:
        a,b = login('Drop Owner'),login('Drop Observer')
        aid,bid = a.id,b.id
        original = a.bag[0]
        object_item = until(a,0,'object',lambda e:e['id']==original)
        # A foreign IID and a stale source must not remove anything.
        for item,source in [(b.bag[0],0),(original,77)]:
            mark=len(a.events)
            a.send('drop_item',id=item,**{'from':source},quantity=1,landblock=0x02990000)
            until(a,mark,'chat',lambda e:'Drop rejected' in str(e))
        # Movement before contact cancels the action; it cannot remove the item
        # later, and the observer must receive the matching native stop.
        mark=len(a.events);obs=len(b.events)
        a.send('drop_item',id=original,**{'from':0},quantity=object_item['quantity'],landblock=0x02990000)
        until(a,mark,'behavior',lambda e:e['behavior']==0x41000021)
        pose=a.spawn['placement'];moved=pose['position'][:];moved[0]+=1.25
        a.send('move',cell=pose['cell'],position=moved,heading=0,motion=[0,1,0])
        until(a,mark,'chat',lambda e:'Item action interrupted' in str(e))
        until(b,obs,'stop_behavior',lambda e:e['id']==aid and e['behavior']==0x41000021)
        time.sleep(1.9)
        assert not any(e.get('event')=='inventory_remove' and e.get('id')==original for e in a.events[mark:])
        a.send('move',cell=pose['cell'],position=pose['position'],heading=0,motion=[0,0,0]);time.sleep(.15)
        # Leaving the avatar before contact also discards the pending transfer.
        mark=len(a.events)
        a.send('drop_item',id=original,**{'from':0},quantity=object_item['quantity'],landblock=0x02990000)
        until(a,mark,'behavior',lambda e:e['behavior']==0x41000021)
        a.send('logout',id=aid);until(a,mark,'selection');time.sleep(1)
        a.send('enter',id=aid);until(a,mark,'player');a.send('ready')
        assert original in until(a,mark,'inventory')['items']
        until(a,mark,'status',lambda e:e['phase']=='world')
        mark=len(a.events);obs=len(b.events)
        started=time.monotonic()
        a.send('drop_item',id=original,**{'from':0},quantity=object_item['quantity'],landblock=0x02990000)
        until(a,mark,'behavior',lambda e:e['id']==aid and e['behavior']==0x41000021)
        until(b,obs,'behavior',lambda e:e['id']==aid and e['behavior']==0x41000021)
        until(a,mark,'inventory_remove',lambda e:e['id']==original)
        assert time.monotonic()-started>=.65,'Drop committed before the reach animation'
        dropped=until(a,mark,'object',lambda e:int(e['id'])>>48==0x8180)
        ground=dropped['id']
        assert until(b,obs,'object',lambda e:e['id']==ground)['placement']==dropped['placement']
        assert dropped['entity']==object_item['entity']
        # It must survive the dropping account disconnecting and the server restarting.
        stop(server);server=start()
        a,b=login(identity=aid),login(identity=bid)
        assert original not in a.bag
        until(b,0,'object',lambda e:e['id']==ground)
        mark=len(b.events);obs=len(a.events)
        b.send('use',target='0',item=ground)
        until(b,mark,'behavior',lambda e:e['id']==bid and e['behavior']==0x41000020)
        until(a,obs,'behavior',lambda e:e['id']==bid and e['behavior']==0x41000020)
        # Both reaches may begin, but ownership is checked again at contact.
        time.sleep(.2);a.send('use',target='0',item=ground)
        until(a,obs,'behavior',lambda e:e['id']==aid and e['behavior']==0x41000020)
        picked=until(b,mark,'object',lambda e:e.get('container')==bid and e.get('entity')==object_item['entity'])
        assert picked['id']!=original
        until(b,mark,'inventory_acquired',lambda e:e['owner']==bid and e['id']==picked['id'] and e['status']==0)
        until(a,obs,'remove',lambda e:e['id']==ground)
        until(a,obs,'chat',lambda e:'Item action rejected' in str(e))
        assert not any(e.get('event')=='object' and e.get('container')==aid for e in a.events[obs:])
        time.sleep(.15);mark=len(a.events)
        a.send('use',target='0',item=ground)
        until(a,mark,'chat',lambda e:'Pickup rejected' in str(e))
        stop(server);server=start()
        a,b=login(identity=aid),login(identity=bid)
        assert picked['id'] in b.bag
        assert not any(e.get('event')=='object' and e.get('id')==ground for e in a.events+b.events)
        # Advance just this disposable store's deadline; actual server startup,
        # replication and maintenance use their normal production entry points.
        mark=len(b.events)
        b.send('drop_item',id=picked['id'],**{'from':b.bag.index(picked['id'])},
               quantity=object_item['quantity'],landblock=0x02990000)
        expiring=until(b,mark,'object',lambda e:int(e['id'])>>48==0x8180)['id']
        stop(server)
        journal=data/'world/characters.journal'
        lines=journal.read_text().splitlines()
        index=next(i for i in reversed(range(len(lines))) if lines[i].startswith('wd2\t'))
        fields=lines[index].split('\t')
        fields[-2]=str(int(time.time())+20)
        record='\t'.join(fields[:-1])
        checksum=2166136261
        for byte in record.encode():
            checksum=((checksum^byte)*16777619)&0xffffffff
        lines[index]=f'{record}\t{checksum:08x}'
        journal.write_bytes(('\n'.join(lines)+'\n').encode())
        server=start()
        a,b=login(identity=aid),login(identity=bid)
        for peer in (a,b):
            until(peer,0,'object',lambda e:e['id']==expiring)
        for peer in (a,b):
            until(peer,0,'remove',lambda e:e['id']==expiring,timeout=36)
        stop(server);server=start()
        a,b=login(identity=aid),login(identity=bid)
        assert picked['id'] not in b.bag
        assert not any(e.get('event')=='object' and e.get('id')==expiring for e in a.events+b.events)
        report={'result':'PASS','directory':str(data),'checks':['foreign/stale drops rejected',
            'movement and logout cancel uncommitted transfers','competing reaches grant one owner only',
            'both observers see pickup/putdown animations before item transfer',
            'both observers see dropped model','drop survives restart','other player picks up once',
            'pickup and ownership survive restart','both observers see ground expiry, still absent after restart']}
        (data/'result.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
    finally:
        if server.poll() is None:stop(server)
        log.close()


if __name__=='__main__':main()
