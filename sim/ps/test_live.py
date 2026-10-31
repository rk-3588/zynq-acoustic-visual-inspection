"""Real daemon sockets/PTY, no physical UART and no robot motion."""
import sys,time
from test_modes import Rig

def live(binary):
    r=Rig(binary)
    try:
        r.quiet();r.target.update(valid=False,raw=True,col=0,row=18,sound='active',position='moving')
        # Hint is stale right, live measurement is left. Service must resolve it.
        assert r.command('execute 3 a01')[0]=='OK'
        kind,task,region=r.frame();assert (kind,region)==(1,1)
        assert r.command('execute 2 a02')[0]=='ERR';r.quiet()
        r.reply(2,task,0);r.wait(lambda s:s['phase']==3)
        # An idempotent retry after region change never transmits again.
        r.target['col']=63;assert r.command('execute 1 a01')[0]=='OK';r.quiet()
        assert r.command('execute 1 a03')[0]=='OK'
        kind,task,region=r.frame();assert (kind,region)==(1,3)
        r.reply(2,task,1);r.wait(lambda s:s['phase']==4)
        for update in ({'sound':'quiet'},{'sound':'active','age':151},
                       {'age':0,'raw':False}):
            r.target.update(update);assert r.command('execute 2 a04')[0]=='ERR';r.quiet()
        r.target.update(raw=True,col=32)
        assert r.command('execute 1 a05')[0]=='OK'
        kind,task,region=r.frame();assert (kind,region)==(1,2)
        # Missing result becomes UNKNOWN, then clears without one extra byte.
        s=r.wait(lambda s:s['phase']==2,timeout=5);assert s['unknown_hold_ms']==5000
        r.reply(2,task,2);time.sleep(.05);assert r.status()['phase']==2
        r.wait(lambda s:s['phase']==0,timeout=6);r.quiet(.3)
        s=r.status();assert not s['feedback_seen'] and s['auto_mode']==0
        assert s['event']=='unknown_expired_ready_no_retry'
        r.reply(2,task,0);r.quiet(.2);assert r.status()['phase']==0
        r.halt();r.start();r.quiet(.2);assert r.status()['phase']==0
        # A persisted unknown from an earlier boot clears with production 5s.
        r.halt();p=r.root/'robot-task.state';parts=p.read_text().split();parts[3]='2';p.write_text(' '.join(parts)+'\n')
        r.start();assert r.status()['phase']==2;r.quiet(.2)
        r.wait(lambda s:s['phase']==0,timeout=6);r.quiet(.2)
        assert r.status()['tx_frames']==0
        assert r.command('execute 3 a06')[0]=='OK'
        kind,task,region=r.frame();assert (kind,region)==(1,2)
        r.reply(2,task,0);r.wait(lambda s:s['phase']==3)
        print('PASS_LIVE_SERVICE_MOVING_HINT_REPLACED_DUPLICATE_QUIET_STALE_TIMEOUT_LATE_REPLY_REBOOT_UNKNOWN_EXPIRY_NO_RESEND')
    finally:r.close()

if __name__=='__main__':live(sys.argv[1])
