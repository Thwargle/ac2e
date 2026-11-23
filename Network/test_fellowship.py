"""Native social/chat round trip with three real UDP clients and isolated state."""
import json
import os
import subprocess
import time
import uuid
from test_network import ROOT, SERVER, DAT_DIRECTORY, Peer
from test_services import fixture
from test_gameplay import until

def main():
    run=ROOT/'Saved/AC2NetworkTests'/('fellowship-'+uuid.uuid4().hex);run.mkdir(parents=True)
    content=fixture(run,isolated=True);peers=[]
    with (run/'server.log').open('w') as log:
        server=subprocess.Popen([str(SERVER),'serve','--bind','127.0.0.1:19389','--account','local',
            '--data',str(run/'world'),'--game-data',str(DAT_DIRECTORY),'--content',str(content),
            '--initialize-ui','true','--encounters','none','--seconds','100'],
            env=dict(os.environ,PYREALIUM_UNREAL_LEGIONS_DATA='1'),stdout=log,stderr=log,creationflags=0x08000000)
        try:
            time.sleep(1);assert server.poll() is None
            def join(name):
                p=Peer();peers.append(p);p.send('connect',host='127.0.0.1',port=19389,account='local');p.wait('characters')
                p.send('create',name=name,race=1,sex=4096,start_area=0,physical=[[1,1],[2,1]])
                p.id=p.wait('created')['character']['id'];p.send('enter',id=p.id);p.wait('player');p.send('ready')
                p.wait('status',lambda e:e['phase']=='world');until(p,0,'inventory');return p
            a=join('Fellowship Leader');b=join('Fellowship Recruit');c=join('Outside Observer')
            marks=[len(p.events) for p in peers]
            a.send('chat',channel='general',text='General delivery check')
            for p,mark in zip(peers,marks):
                message=until(p,mark,'chat',lambda e:'General delivery check' in str(e))
                assert message['kind']==0x80000 and message['speaker']['literal']=='Fellowship Leader',message
            a.send('fellowship',action='create',name='Dereth Travelers')
            snap=a.wait('fellowship');assert snap['name']=='Dereth Travelers' and snap['leader']==a.id and len(snap['members'])==1,snap
            ma,mb=len(a.events),len(b.events);a.send('fellowship',action='invite',id=b.id)
            invitation=until(b,mb,'fellowship_invitation');assert invitation['id']==a.id,invitation
            b.send('fellowship',action='accept',id=a.id)
            for p,mark in [(a,ma),(b,mb)]:
                snap=until(p,mark,'fellowship',lambda e:len(e['members'])==2)
                assert {m['id'] for m in snap['members']}=={a.id,b.id}
                assert all(m['health']==m['max_health']>0 and m['vigor']==m['max_vigor']>0 for m in snap['members']),snap
            time.sleep(.6);ma,mb,mc=len(a.events),len(b.events),len(c.events)
            a.send('chat',channel='fellowship',text='Private fellowship delivery')
            for p,mark in [(a,ma),(b,mb)]:
                message=until(p,mark,'chat',lambda e:'Private fellowship delivery' in str(e))
                assert message['kind']==0x40,message
            time.sleep(.3)
            assert not any('Private fellowship delivery' in str(e) for e in c.events[mc:])
            time.sleep(.6);ma,mb=len(a.events),len(b.events)
            a.send('chat',channel='tell',recipient='Fellowship Recruit',text='Private tell check')
            until(a,ma,'chat',lambda e:'Private tell check' in str(e))
            until(b,mb,'chat',lambda e:'Private tell check' in str(e))
            time.sleep(.6);mc=len(c.events)
            a.send('chat',channel='say',text='Nearby delivery check')
            until(c,mc,'chat',lambda e:'Nearby delivery check' in str(e))
            # Allegiance consent and channel isolation are independent of the
            # fellowship. A third party cannot accept another patron's request.
            ma,mb,mc=len(a.events),len(b.events),len(c.events)
            b.send('allegiance',action='swear',id=a.id)
            invitation=until(a,ma,'allegiance_invitation');assert invitation['id']==b.id,invitation
            c.send('allegiance',action='accept',id=b.id)
            until(c,mc,'chat',lambda e:'expired' in str(e).lower())
            a.send('allegiance',action='accept',id=b.id)
            for p,mark in [(a,ma),(b,mb)]:
                snap=until(p,mark,'allegiance',lambda e:e.get('total')==2)
                assert snap['monarch']['id']==a.id,snap
            time.sleep(.6);ma,mb,mc=len(a.events),len(b.events),len(c.events)
            b.send('chat',channel='allegiance',text='Private allegiance delivery')
            for p,mark in [(a,ma),(b,mb)]:
                message=until(p,mark,'chat',lambda e:'Private allegiance delivery' in str(e));assert message['kind']==0x80,message
            time.sleep(.3);assert not any('Private allegiance delivery' in str(e) for e in c.events[mc:])
            ma=len(a.events);a.send('allegiance',action='swear',id=b.id)
            until(a,ma,'chat',lambda e:'cycles' in str(e).lower())
            mc=len(c.events);c.send('allegiance',action='break',id=b.id)
            until(c,mc,'chat',lambda e:'own patron' in str(e).lower())
            # A new transport/character login receives the same persisted link.
            b.send('logout',id=b.id);b.wait('characters')
            mb=len(b.events);b.send('enter',id=b.id);b.wait('player');b.send('ready');b.wait('status',lambda e:e['phase']=='world')
            until(b,mb,'allegiance',lambda e:e.get('patron',{}).get('id')==a.id)
            ma,mb=len(a.events),len(b.events);b.send('allegiance',action='break',id=a.id)
            until(b,mb,'allegiance',lambda e:e.get('total')==0)
            until(a,ma,'allegiance',lambda e:e.get('total')==0)
            time.sleep(.2);mark=len(b.events);b.send('chat',channel='allegiance',text='Former allegiance cannot receive this')
            until(b,mark,'error',lambda e:'No allegiance chat room' in str(e))
            # Logout ends the recruit's session fellowship; rejoin it for the
            # remaining fellowship authorization/lifecycle checks.
            mb=len(b.events);a.send('fellowship',action='invite',id=b.id);until(b,mb,'fellowship_invitation')
            ma=len(a.events);b.send('fellowship',action='accept',id=a.id);until(a,ma,'fellowship',lambda e:len(e['members'])==2);until(b,mb,'fellowship',lambda e:len(e['members'])==2)
            mark=len(b.events);b.send('fellowship',action='dismiss',id=a.id)
            until(b,mark,'chat',lambda e:'leader' in str(e).lower())
            ma,mb=len(a.events),len(b.events);a.send('fellowship',action='dismiss',id=b.id)
            until(b,mb,'fellowship_clear');until(a,ma,'fellowship',lambda e:len(e['members'])==1)
            mark=len(b.events);b.send('chat',channel='fellowship',text='Must not reach former fellows')
            until(b,mark,'error',lambda e:'No fellowship chat room' in str(e))
            mb=len(b.events);a.send('fellowship',action='invite',id=b.id);until(b,mb,'fellowship_invitation')
            ma=len(a.events);b.send('fellowship',action='decline',id=a.id);until(a,ma,'chat',lambda e:'declined' in str(e))
            mb=len(b.events);a.send('fellowship',action='invite',id=b.id);until(b,mb,'fellowship_invitation')
            ma=len(a.events);b.send('fellowship',action='accept',id=a.id)
            until(a,ma,'fellowship',lambda e:len(e['members'])==2)
            until(b,mb,'fellowship',lambda e:len(e['members'])==2)
            ma,mb=len(a.events),len(b.events);b.send('fellowship',action='leave')
            until(b,mb,'fellowship_clear');until(a,ma,'fellowship',lambda e:len(e['members'])==1)
            ma=len(a.events);a.send('fellowship',action='disband');until(a,ma,'fellowship_clear')
            for i,p in enumerate(peers):(run/f'peer-{i}.json').write_text(json.dumps(p.events,indent=2))
            print('PASS: fellowship lifecycle/vitals/authority, allegiance consent/cycles/reconnect/break, private rooms and revocation, general/tells/local chat.',run)
        finally:
            for p in peers:p.close()
            if server.poll() is None:(run/'world/stop.request').write_text('stop\n');server.wait(timeout=10)

if __name__=='__main__':main()
