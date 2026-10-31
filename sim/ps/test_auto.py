"""Automatic operation regression on PTYs, never a physical UART."""
import os,select,subprocess,sys,time
from test_modes import Rig

def wait_lease(r,nonce,seconds):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        r.lease(nonce);r.quiet(min(.12,max(0,end-time.monotonic())))

def tracking(binary):
    r=Rig(binary)
    try:
        r.quiet();r.target['valid']=False
        assert r.command('auto-track 121')[0]=='ERR';r.inspect()
        assert r.command('auto-track 121')[0]=='OK';assert r.status()['auto_mode']==1
        wait_lease(r,0x121,.5) # enabled in silence emits no movement
        r.target['valid']=True
        kind,task,zone=r.frame();assert (kind,zone)==(0x30,7);r.reply(0x25,task,0)
        for _ in range(3):
            r.lease(0x121);kind,task,zone=r.frame();assert kind==0x30;r.reply(0x25,task,0)
        assert r.command('execute 2 444')[0]=='ERR'
        assert r.command('auto-grab 445')[0]=='ERR'
        r.target['col']=0;r.target['row']=0;zones=[]
        for _ in range(7):
            r.lease(0x121);kind,task,zone=r.frame();zones.append(zone);r.reply(0x25,task,0)
        assert zones[-1]==1
        r.target['valid']=False;r.lease(0x121)
        kind,task,zone=r.frame();assert (kind,zone)==(0x30,0);r.reply(0x25,task,0)
        r.wait(lambda s:s['phase']==3);assert r.status()['auto_mode']==1
        wait_lease(r,0x121,1.8);r.target['valid']=True;r.lease(0x121)
        kind,task,zone=r.frame();assert (kind,zone)==(0x30,1);r.reply(0x25,task,0)
        r.lease(0x121);assert r.command('auto-off 122')[0]=='OK'
        for _ in range(3):
            kind,task,zone=r.frame();r.reply(0x25,task,0)
            if not zone:break
        assert not zone;r.wait(lambda s:s['phase']==3);assert r.status()['auto_mode']==0
        r.quiet(.3);r.halt();r.start();r.quiet(.3);assert not r.status()['auto_mode']
        print('PASS_AUTO_TRACK_SILENT_ON_REALTIME_SWITCHING_QUIET_HOLD_AUTO_RESUME_OFF_NO_RESTART')
    finally:r.close()

def grasp(binary):
    r=Rig(binary)
    try:
        assert r.command('auto-grab 220')[0]=='OK';wait_lease(r,0x220,.4)
        assert r.command('auto-off 219')[0]=='OK';r.quiet(1.8)
        assert r.command('auto-grab 221')[0]=='OK'
        assert r.command('auto-grab 221')[0]=='OK' # duplicate intent does not reset dwell/latch
        # A moving region cannot satisfy the 1.5s continuous dwell.
        wait_lease(r,0x221,.8);r.target['col']=0
        wait_lease(r,0x221,.8);r.target['col']=32
        wait_lease(r,0x221,1.1);r.lease(0x221)
        kind,task,region=r.frame(.8);assert (kind,region)==(1,2)
        r.reply(2,task,2);r.wait(lambda s:s['phase']==8)
        assert r.status()['auto_latched'];assert r.command('execute 2 abc')[0]=='ERR'
        # Quiet during blocking workflow cannot rearm the next grab.
        r.target['valid']=False;wait_lease(r,0x221,2.2)
        r.target['valid']=True;r.reply(2,task,0);r.wait(lambda s:s['phase']==3)
        wait_lease(r,0x221,2.1);assert r.status()['auto_latched']
        # Invalid/moving target is not a quiet event: only explicit quiet arms.
        r.target.update(valid=False,sound='active',position='moving');wait_lease(r,0x221,2.3)
        assert r.status()['auto_latched'];r.target.pop('sound');r.target.pop('position')
        r.target['valid']=False;wait_lease(r,0x221,2.5)
        assert not r.status()['auto_latched']
        r.target['valid']=True;wait_lease(r,0x221,3.8);r.lease(0x221)
        kind,task2,region=r.frame(.9);assert (kind,region)==(1,2) and task2!=task
        assert r.command('auto-off 222')[0]=='OK';r.quiet(.2)
        r.reply(2,task2,0);r.wait(lambda s:s['phase']==3);r.quiet(.3)
        assert not r.status()['auto_mode'];r.halt();r.start();r.quiet();assert not r.status()['auto_mode']
        print('PASS_AUTO_GRAB_DWELL_ONE_PER_SOUND_NO_REARM_WHILE_BUSY_QUIET_COOLDOWN_OFF_CURRENT_WORKFLOW_FINISHES')
    finally:r.close()

def faults(binary):
    r=Rig(binary)
    try:
        assert r.command('auto-grab 321')[0]=='OK'
        time.sleep(1.1);r.wait(lambda s:not s['auto_mode']);r.quiet(.3) # lease expires before dwell
        assert r.status()['mode_error']==5
        r.inspect();assert r.command('auto-track 322')[0]=='OK'
        kind,task,zone=r.frame();assert kind==0x30 # lose ACK, never retry motion
        r.wait(lambda s:s['phase']==2);assert not r.status()['auto_mode'];r.reply(0x25,task,0);r.quiet(.3)
        r.halt();r.start();r.quiet();assert not r.status()['auto_mode'] and r.status()['phase']==2
        print('PASS_AUTO_LEASE_EXPIRE_WAITING_LOST_ACK_UNKNOWN_NO_RETRY_RESTART_OFF')
    finally:r.close()

def ui_grab(binary,client):
    r=Rig(binary);ui=None
    try:
        with (r.root/'ui.log').open('wb') as log:
            ui=subprocess.Popen([client,str(r.root/'sock'),str(r.root/'state')],stdin=subprocess.PIPE,stdout=log,stderr=log,text=True)
            def send(s):ui.stdin.write(s+'\n');ui.stdin.flush()
            time.sleep(.3);send('OPEN');r.quiet(.2);send('AUTO_GRAB')
            kind,task,region=r.frame(3);assert (kind,region)==(1,2);r.reply(2,task,0)
            r.wait(lambda s:s['phase']==3);r.quiet(1.1);assert r.status()['auto_mode']==2
            send('CLOSE');r.quiet(.5);assert r.status()['auto_mode']==2 # running UI owns lease, not open window
            send('QUIT');ui.wait(timeout=2);ui=None
            r.wait(lambda s:not s['auto_mode']);r.quiet(.3)
            print('PASS_REAL_UI_AUTOGRAB_NO_CONFIRM_LEASE_AFTER_COMPLETE_CLOSE_WINDOW_UI_EXIT_OFF')
    finally:
        if ui:ui.terminate();ui.wait(timeout=2)
        if sys.exc_info()[0] and (r.root/'ui.log').exists():print((r.root/'ui.log').read_text())
        r.close()

if __name__=='__main__':
    tracking(sys.argv[1]);grasp(sys.argv[1]);faults(sys.argv[1])
    if len(sys.argv)>2:ui_grab(sys.argv[1],sys.argv[2])
