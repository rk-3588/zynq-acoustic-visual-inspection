"""Only PTYs/temp Unix sockets: never opens a board UART or controls an arm."""
import fcntl,json,os,pty,select,socket,subprocess,sys,tempfile,termios,threading,time
from pathlib import Path

class Rig:
    def __init__(self,binary):
        self.temp=tempfile.TemporaryDirectory(prefix='arm-modes-pty-');self.root=Path(self.temp.name)
        self.master,self.slave=pty.openpty();self.tty=os.ttyname(self.slave);self.binary=binary
        self.config=self.root/'regions';self.config.write_text('1 1\n2 2\n3 3\n');self.config.chmod(0o600)
        self.target={'col':32,'row':18,'valid':True};self.end=threading.Event();self.process=None
        self.pl=socket.socket(socket.AF_UNIX);self.pl.bind(str(self.root/'pl'));self.pl.listen(4);self.pl.settimeout(.1)
        def serve():
            while not self.end.is_set():
                try:c,_=self.pl.accept()
                except socket.timeout:continue
                with c:
                    if c.recv(100)!=b'status\n':continue
                    v=self.target['valid'];p=self.target
                    data=dict(version='20261013-pl-control-enable-v3',sound_state=p.get('sound','active' if v else 'quiet'),
                        position_state=p.get('position','stable' if v else 'quiet'),position_valid=v,filtered_col=p['col'] if v else None,
                        filtered_row=p['row'] if v else None,hotspot_value=20000,observation_age_ms=p.get('age',0),
                        gate_health='fresh',acoustic_enabled=True,enable_applied=True)
                    raw=p.get('raw',v)
                    data.update(raw_x=p['col'] if raw else None,raw_y=p['row'] if raw else None,read_quality='best_effort')
                    try:c.sendall((json.dumps(data)+'\n').encode())
                    except BrokenPipeError:pass
        self.thread=threading.Thread(target=serve,daemon=True);self.thread.start()
        self.log=(self.root/'log').open('wb');self.buffer=b'';self.history=[];self.start()
    def command(self,text):
        with socket.socket(socket.AF_UNIX) as c:
            c.settimeout(2);c.connect(str(self.root/'sock'));c.sendall(text.encode()+b'\n');raw=b''
            while b'\n' not in raw:
                data=c.recv(4096);assert data;raw+=data
        head,data=raw.decode().split(' ',1);return head,json.loads(data)
    def status(self):return self.command('status')[1]
    def start(self):
        fcntl.ioctl(self.slave,termios.TIOCNXCL)
        self.process=subprocess.Popen([self.binary,self.tty,str(self.root/'sock'),str(self.root/'state'),
            str(self.root),str(self.root/'pl'),'4000',str(self.config)],stdout=self.log,stderr=self.log)
        end=time.monotonic()+3
        while time.monotonic()<end:
            assert self.process.poll() is None
            try:
                if self.status()['uart_ready']:return
            except OSError:pass
            time.sleep(.01)
        raise AssertionError('service did not start')
    def halt(self):
        if self.process:
            self.process.terminate();self.process.wait(timeout=3);self.process=None
    def frame(self,timeout=1):
        end=time.monotonic()+timeout
        while len(self.buffer)<8 and time.monotonic()<end:
            if select.select([self.master],[],[],min(.05,max(0,end-time.monotonic())))[0]:self.buffer+=os.read(self.master,4096)
        assert len(self.buffer)>=8,('no frame',self.status())
        f,self.buffer=self.buffer[:8],self.buffer[8:]
        assert f[:2]==b'\xaa\x55' and f[-2:]==b'\r\n',f.hex()
        result=(f[2],int.from_bytes(f[3:5],'little'),f[5]);self.history.append((time.monotonic(),result));return result
    def quiet(self,seconds=.12):
        assert not self.buffer and not select.select([self.master],[],[],seconds)[0],'unexpected UART traffic'
    def reply(self,kind,task,payload):
        if isinstance(payload,int):payload=bytes([payload])
        os.write(self.master,b'\xaa\x55'+bytes([kind])+task.to_bytes(2,'little')+payload+b'\r\n')
    def wait(self,predicate,timeout=3):
        end=time.monotonic()+timeout;s={}
        while time.monotonic()<end:
            s=self.status()
            if predicate(s):return s
            time.sleep(.015)
        raise AssertionError(s)
    def inspect(self,mask=4095,same=False):
        assert self.command('inspect 111')[0]=='OK'
        kind,task,value=self.frame();assert (kind,value)==(0x34,0)
        payload=b'\x01'+mask.to_bytes(2,'little')+b''.join((1500+n).to_bytes(2,'little')*5 for n in range(12))
        self.reply(0x26,task,payload)
        kind,task,value=self.frame();assert (kind,value)==(0x44,0)
        payload=b'\x01\x03'+(100).to_bytes(2,'little')+b'\x00\x00'+(1500).to_bytes(2,'little')*5+(1500 if same else 1600).to_bytes(2,'little')*5
        self.reply(0x28,task,payload)
        return self.wait(lambda s:not s['transaction_pending'])
    def lease(self,nonce):assert self.command(f'lease {nonce:x}')[0]=='OK'
    def track(self,nonce=0x222,zone=7):
        assert self.command(f'track-start {zone} {nonce:x}')[0]=='OK'
        kind,task,value=self.frame();assert (kind,value)==(0x30,zone)
        assert self.status()['track_region']==0 # not accepted before ACK
        self.reply(0x02,task,0);time.sleep(.025)
        assert self.status()['phase']==1 # unrelated grab result cannot resolve a track
        self.reply(0x25,task,0);self.wait(lambda s:s['phase']==8);return task
    def cycle(self,count=2,nonce=0x333):
        assert self.command(f'cycle-start {count} {nonce:x}')[0]=='OK'
        kind,task,value=self.frame();assert (kind,value)==(0x40,count)
        self.reply(0x27,task,0);return task
    def cycle_reply(self,task,active=1,stage=1,reason=1,limit=2,count=0):
        self.reply(0x29,task,bytes([active,stage,reason,limit])+count.to_bytes(2,'little'))
    def close(self):
        self.halt();self.end.set();self.thread.join(timeout=1);self.pl.close();self.log.close()
        if sys.exc_info()[0]:print((self.root/'log').read_text())
        os.close(self.master);os.close(self.slave);self.temp.cleanup()

def run(binary):
    r=Rig(binary)
    try:
        r.quiet();assert r.status()['mode']==0
        assert r.command('track-start 7 1')[0]=='ERR';assert r.command('cycle-start 2 2')[0]=='ERR';r.quiet()
        s=r.inspect();assert s['track_mask']==4095 and s['cycle_ready'];r.quiet()
        assert [f[1][0] for f in r.history]==[0x34,0x44] # no 47 heartbeat/probe
        assert r.command('track-start 6 222')[0]=='ERR';r.quiet()
        r.track();assert r.command('execute 2 444')[0]=='ERR';assert r.command('cycle-start 2 555')[0]=='ERR'
        assert r.command('track-start 6 222')[0]=='ERR' # nonce cannot change target/type
        assert r.command('execute 2 222')[0]=='ERR'
        for _ in range(3):
            r.lease(0x222);kind,task,zone=r.frame();assert (kind,zone)==(0x30,7);r.reply(0x25,task,0)
        times=[t for t,f in r.history if f[0]==0x30];assert min(b-a for a,b in zip(times,times[1:]))>=.185
        # 2 fresh observations plus smoothing/hysteresis before changing zone.
        r.target['col']=0;r.target['row']=0
        zones=[]
        for _ in range(7):
            r.lease(0x222);kind,task,zone=r.frame();assert kind==0x30;zones.append(zone);r.reply(0x25,task,0)
        assert zones[-1]==1 and zones[0]!=1,zones
        r.target['valid']=False;r.lease(0x222)
        kind,task,zone=r.frame();assert (kind,zone)==(0x30,0);r.reply(0x25,task,0)
        s=r.wait(lambda s:s['phase']==3);assert s['mode_error']==6 and not s['track_armed']
        r.target['valid']=True;r.quiet(.3) # no automatic resume
        r.wait(lambda s:not s['transaction_pending']);r.quiet()
        # UI lease expiry cuts serial refreshes; late ACK cannot rearm tracking.
        r.target['col']=32;r.target['row']=18;r.track(0x223)
        kind,task,zone=r.frame();assert kind==0x30;r.reply(0x25,task,0)
        kind,task,zone=r.frame();assert kind==0x30 # deliberately lose ACK
        r.wait(lambda s:s['phase']==2,timeout=2);r.reply(0x25,task,0);r.quiet(.3)
        assert r.status()['phase']==2
        assert r.command('cycle-start 1 334')[0]=='ERR'
        assert r.command('track-hold 224')[0]=='OK'
        kind,task,zone=r.frame();assert (kind,zone)==(0x30,0);r.reply(0x25,task,0)
        r.wait(lambda s:s['phase']==3);r.wait(lambda s:not s['transaction_pending'])
        # Test start ACK is not completion or a report of active motion.
        r.cycle();assert r.status()['phase']!=3 and not r.status()['cycle_active']
        kind,task,value=r.frame();assert (kind,value)==(0x47,0);r.cycle_reply(task,count=0)
        r.wait(lambda s:s['cycle_active']==1)
        assert r.command('execute 2 999')[0]=='ERR'
        for n in (1,2):
            r.lease(0x333);kind,task,_=r.frame();assert kind==0x47
            r.cycle_reply(task,active=n<2,stage=2,reason=1 if n<2 else 2,count=n)
        s=r.wait(lambda s:s['phase']==3);assert s['cycle_completed']==2
        r.quiet(.4)
        # Explicit stop is 41, not a new 40 and not a fabricated completion.
        r.cycle(0,0x335);kind,task,_=r.frame();r.cycle_reply(task,limit=0)
        r.wait(lambda s:s['cycle_active']==1)
        assert r.command('cycle-stop 336')[0]=='OK'
        kind,task,_=r.frame();assert kind==0x41
        assert r.command('cycle-stop 336')[0]=='OK' # local dedup, no second 41
        r.reply(0x27,task,0);kind,task,_=r.frame();assert kind==0x47
        r.cycle_reply(task,active=0,reason=3,limit=0)
        r.wait(lambda s:s['phase']==3);assert r.status()['cycle_reason']==3
        r.wait(lambda s:not s['transaction_pending']);r.quiet()
        # Lease keepalive is bound to peer PID, not status readers.
        r.track(0x337)
        # A different PID cannot renew the lease even with the nonce.
        code="import socket,sys;s=socket.socket(socket.AF_UNIX);s.connect(sys.argv[1]);s.sendall(b'lease 337\\n');print(s.recv(4096).decode().split()[0])"
        assert subprocess.check_output([sys.executable,'-c',code,str(r.root/'sock')],text=True).strip()=='ERR'
        end=time.monotonic()+2
        while time.monotonic()<end:
            if select.select([r.master],[],[],.03)[0]:
                kind,task,_=r.frame();assert kind==0x30;r.reply(0x25,task,0)
            if r.status()['phase']==2:break
        assert r.status()['phase']==2;r.quiet(.35)
        # Restart keeps an unfinished mode unknown and never resumes traffic.
        r.halt();r.start();r.quiet(.35);assert r.status()['phase']==2
        assert r.command('lease 337')[0]=='ERR'
        assert r.command('cycle-start 1 338')[0]=='ERR'
        assert r.command(f"ack-stopped {r.status()['id']}")[0]=='OK';r.quiet()
        s=r.inspect(mask=0,same=True);assert not s['cycle_ready'] and s['track_mask']==0
        assert r.command('track-start 7 339')[0]=='ERR';assert r.command('cycle-start 1 340')[0]=='ERR';r.quiet()
        # Configuration timeout sends once, with no hidden query/automatic retry.
        assert r.command('inspect 444')[0]=='OK';kind,task,_=r.frame();assert kind==0x34
        r.wait(lambda s:s['mode_error']==1 and not s['transaction_pending']);r.quiet(.3)
        print('PASS_MODES_PTY_CONFIG_ONLY_START_GUARDS_RATE_ACK_TARGET_HYSTERESIS_QUIET_HOLD_LEASE_NO_RETRY_CYCLE_PROGRESS_STOP_UNKNOWN_RESTART')
    finally:r.close()

def run_ui(binary,client):
    r=Rig(binary);ui=None
    try:
        with (r.root/'ui.log').open('wb') as log:
            ui=subprocess.Popen([client,str(r.root/'sock'),str(r.root/'state')],stdin=subprocess.PIPE,stdout=log,stderr=log,text=True)
            def send(text):ui.stdin.write(text+'\n');ui.stdin.flush()
            time.sleep(.25);send('OPEN');r.quiet(.2)
            send('INSPECT');kind,task,_=r.frame();assert kind==0x34
            r.reply(0x26,task,b'\x01\xff\x0f'+(1500).to_bytes(2,'little')*60)
            kind,task,_=r.frame();assert kind==0x44
            r.reply(0x28,task,b'\x01\x03'+(100).to_bytes(2,'little')+b'\x00\x00'+(1500).to_bytes(2,'little')*5+(1600).to_bytes(2,'little')*5)
            r.wait(lambda s:s['cycle_known'] and not s['transaction_pending']);time.sleep(.25)
            send('TRACK');kind,task,zone=r.frame();assert (kind,zone)==(0x30,7);r.reply(0x25,task,0)
            for _ in range(6):
                kind,task,zone=r.frame();assert (kind,zone)==(0x30,7);r.reply(0x25,task,0)
            assert r.status()['lease_valid']==1 # actual UI IPC lease, not test-side renewal
            send('CLOSE')
            for _ in range(2):
                kind,task,zone=r.frame();assert (kind,zone)==(0x30,7);r.reply(0x25,task,0)
            send('OPEN');send('HOLD')
            for _ in range(6):
                kind,task,zone=r.frame();assert kind==0x30;r.reply(0x25,task,0)
                if not zone:break
            assert zone==0;r.wait(lambda s:s['phase']==3);r.wait(lambda s:not s['transaction_pending'])
            time.sleep(.25);send('CYCLE');kind,task,limit=r.frame();assert (kind,limit)==(0x40,2);r.reply(0x27,task,0)
            for n in (0,1,2):
                kind,task,_=r.frame();assert kind==0x47
                r.cycle_reply(task,active=n<2,reason=1 if n<2 else 2,count=n)
            r.wait(lambda s:s['phase']==3);time.sleep(.25);send('TRACK')
            kind,task,_=r.frame();assert kind==0x30;r.reply(0x25,task,0)
            for _ in range(3):
                kind,task,_=r.frame();assert kind==0x30;r.reply(0x25,task,0)
            send('QUIT');ui.wait(timeout=2);ui=None
            end=time.monotonic()+1.5
            while time.monotonic()<end:
                if select.select([r.master],[],[],.03)[0]:
                    kind,task,_=r.frame();assert kind==0x30;r.reply(0x25,task,0)
                if r.status()['phase']==2:break
            assert r.status()['phase']==2;r.quiet(.3)
            print('PASS_REAL_UI_IPC_START_CONFIRM_LEASE_CLOSE_WINDOW_KEEPALIVE_HOLD_CYCLE_UI_EXIT_STOPS_HEARTBEATS')
    finally:
        if ui:ui.terminate();ui.wait(timeout=3)
        if sys.exc_info()[0] and (r.root/'ui.log').exists():print((r.root/'ui.log').read_text())
        r.close()

def run_faults(binary):
    r=Rig(binary)
    try:
        r.inspect();assert r.command('track-start 7 1')[0]=='OK'
        kind,task,_=r.frame();assert kind==0x30;r.reply(0x25,task,3)
        r.wait(lambda s:s['phase']==4);assert not r.status()['track_known'];r.quiet()
        assert r.command('track-start 7 2')[0]=='ERR'
        r.inspect();assert r.command('cycle-start 1 3')[0]=='OK'
        kind,task,_=r.frame();assert kind==0x40;r.reply(0x27,task,5)
        r.wait(lambda s:s['phase']==2);r.quiet(.25) # no heartbeat after a busy rejection
        assert r.command(f"ack-stopped {r.status()['id']}")[0]=='OK'
        r.cycle(2,4);kind,task,_=r.frame();assert kind==0x47
        r.cycle_reply(task,active=0,reason=2,count=0) # inconsistent completion must not unlock
        r.wait(lambda s:s['phase']==2);r.quiet(.25)
        assert r.command(f"ack-stopped {r.status()['id']}")[0]=='OK'
        r.cycle(2,5);kind,task,_=r.frame();assert kind==0x47
        r.wait(lambda s:s['phase']==2);r.quiet(.25) # lost query is not blindly retried
        assert r.command(f"ack-stopped {r.status()['id']}")[0]=='OK'
        assert r.command('inspect 6')[0]=='OK';kind,task,_=r.frame();assert kind==0x34
        r.reply(0x26,task,b'\x01\xff\xff'+(1500).to_bytes(2,'little')*60)
        r.wait(lambda s:s['mode_error']==8);assert not r.status()['track_known'];r.quiet()
        # Global sequence wraps safely and skips the known cached grab ID.
        r.halt();(r.root/'robot-task.state').write_text('ROBOT4 12 0 3 00000000000000ab 3 0 1 7 65535 0 1\n')
        r.start();r.quiet();assert r.command('execute 2 7')[0]=='OK'
        kind,task,zone=r.frame();assert (kind,task,zone)==(1,1,2)
        r.reply(2,task,0);r.wait(lambda s:s['phase']==3)
        print('PASS_MODE_FAULTS_UNCALIBRATED_BUSY_UNKNOWN_INVALID_COMPLETION_CONFIG_MASK_TIMEOUT_WRAP_CACHED_GRAB')
    finally:r.close()

if __name__=='__main__':
    run(sys.argv[1])
    run_faults(sys.argv[1])
    if len(sys.argv)>2:run_ui(sys.argv[1],sys.argv[2])
