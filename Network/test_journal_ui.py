"""Rendered native skills, quests, fellowships, expanded hotbars and window controls."""
import argparse
import os
import shutil
import subprocess
import time
import uuid
from test_network import ROOT, SERVER, DAT_DIRECTORY, Peer
from test_services import fixture
from test_gameplay import until
from prepare_content import ContentProfile
import json


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--quests-only', action='store_true', help='Run only the native quest journal controls and content checks.')
    options=parser.parse_args()
    run=ROOT/'Saved/AC2NetworkTests'/('journal-ui-'+uuid.uuid4().hex);run.mkdir(parents=True)
    content=fixture(run,isolated=True);peer=None
    # Use real server-side NPC conversations to populate the journal before
    # logging in with the rendered client. Positions here are test fixtures only.
    exploration=json.loads((ROOT/'AC2E/emulator/content/exploration-content-v1.json').read_text())
    entities=[q['npc'] for q in exploration['quests']]+[0x470015c0,0x470015c6]
    profile=ContentProfile(DAT_DIRECTORY)
    try:
        with content.open('a') as f:
            for index,entity in enumerate(entities):
                f.write(f'{0x8140000000030d00+index:016x}\t{entity:08x}\t{profile.visual(entity):08x}\t02990109\t60\t241\t-44.606205\t1\t0\t0\t0\t-\n')
    finally:profile.close()
    env=dict(os.environ,PYREALIUM_UNREAL_LEGIONS_DATA='1',AC2_JOURNAL_SMOKE='1',
             AC2_SMOKE_USER_SETTINGS=str(run/'GameUserSettings.ini'),
             AC2_QUEST_WINDOW_SMOKE='1' if options.quests_only else '0')
    with (run/'server.log').open('w') as log:
        args=[str(SERVER),'serve','--bind','127.0.0.1:19388','--account','local',
            '--data',str(run/'world'),'--game-data',str(DAT_DIRECTORY),'--content',str(content),
            '--initialize-ui','true','--encounters','none','--portals','false',
            '--lifestones','false','--seconds','180']
        server=subprocess.Popen(args,env=env,stdout=log,stderr=log,creationflags=0x08000000)
        try:
            time.sleep(1);assert server.poll() is None,(run/'server.log').read_text()
            peer=Peer();peer.send('connect',host='127.0.0.1',port=19388,account='local');peer.wait('characters')
            peer.send('create',name='Journal Render Tester',start_area=0,race=1,sex=4096,physical=[[1,1],[2,1]])
            character=peer.wait('created')['character']['id'];assert character!='0'
            peer.send('enter',id=character);peer.wait('player');peer.send('ready');peer.wait('status',lambda e:e['phase']=='world')
            # Initial inventory now arrives during portal loading, before the
            # world-ready status. Include events already consumed by that wait.
            until(peer,0,'inventory')
            for index,entity in enumerate(entities):
                iid=str(0x8140000000030d00+index);until(peer,0,'object',lambda e:e['id']==iid)
                time.sleep(1.1);mark=len(peer.events);peer.send('use',item=iid,target='0')
                until(peer,mark,'quests')
            peer.send('logout',id=character);peer.wait('selection');peer.close();peer=None
            (run/'world/stop.request').write_text('stop\n');server.wait(timeout=10)
            # Test-only progression, written while this isolated store is closed.
            # Human magic: Mind Lash -> Robustify -> Ease Suffering (selected in UI).
            for command,event in [('player-state','x,10000'),('skill-state','t,59')]:
                subprocess.run([str(SERVER),command,'--account','local','--data',str(run/'world'),
                    '--character-id',character,'--event',event],check=True,env=env,stdout=log,stderr=log,creationflags=0x08000000)
            server=subprocess.Popen(args,env=env,stdout=log,stderr=log,creationflags=0x08000000)
            time.sleep(1);assert server.poll() is None,(run/'server.log').read_text()
            result=subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(ROOT/'Launch-AC2.ps1'),
                '-SkipBuild','-NetworkSmoke','-Server','127.0.0.1','-Port','19388','-Account','local','-DatDirectory',str(DAT_DIRECTORY)],
                cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace',timeout=190,creationflags=0x08000000)
            shutil.copyfile(ROOT/'Unreal/Saved/Logs/AC2Network-smoke.log',run/'client.log')
            print(result.stdout);result.check_returncode()
            assert 'AC2 journal render smoke PASS' in (run/'client.log').read_text(encoding='utf-8',errors='replace')
            assert 'Cannot decode native skills' not in (run/'client.log').read_text(encoding='utf-8',errors='replace')
            for shot in (ROOT/'Unreal/Saved/Screenshots').glob('AC2Journal-*.png'):
                if not options.quests_only or shot.stem in ('AC2Journal-3','AC2Journal-4'):shutil.copyfile(shot,run/shot.name)
            if options.quests_only:
                print('PASS: live Unreal quest journal opened, restored five quest entries, switched native tabs, expanded and scrolled quest details. Logs:',run)
                return
            print('PASS: native skill training, advancement, untraining/cancellation, quest tabs, fellowship controls, window drag/resize, item shortcuts, live skill equipment shading, five-row hotbar resizing, keyboard paging/second-row activation, text focus guards, options scrolling, confirmation dialogs, radar filters, coordinate preference, shortcut locking/Shift override, skill-book quick-add, smooth/instant hotbar scrolling and native height dropdown. Logs:',run)
        finally:
            if peer:peer.close()
            if server.poll() is None:
                (run/'world/stop.request').write_text('stop\n')
                try:server.wait(timeout=8)
                except subprocess.TimeoutExpired:server.terminate();server.wait()


if __name__=='__main__':main()
