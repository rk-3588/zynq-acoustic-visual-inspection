"""Reproduce the board's batched 9600 response, without physical hardware."""
import os,sys,time
from test_modes import Rig
def main(binary):
    r=Rig(binary)
    try:
        r.quiet();assert r.command('inspect 123')[0]=='OK'
        kind,task,_=r.frame();assert kind==0x34
        payload=b'\x01\xff\x0f'+(1500).to_bytes(2,'little')*60
        raw=b'\xaa\x55\x26'+task.to_bytes(2,'little')+payload+b'\r\n'
        assert len(raw)==130
        os.write(r.master,raw[:56]);time.sleep(.064)
        os.write(r.master,raw[56:112]);time.sleep(.060)
        os.write(r.master,raw[112:])
        kind,task,_=r.frame();assert kind==0x44
        r.reply(0x28,task,b'\x01\x03'+(100).to_bytes(2,'little')+b'\x00\x00'+(1500).to_bytes(2,'little')*5+(1600).to_bytes(2,'little')*5)
        s=r.wait(lambda s:s['cycle_known'] and not s['transaction_pending'])
        assert s['track_mask']==4095 and s['cycle_ready'] and s['mode_error']==0
        assert s['rx_frames']==2 and s['expired_frames']==0 and s['rx_bytes']==163
        assert s['rx_max_fragment_gap_ms']>=50 and s['host_rx_idle_ms']==200
        r.quiet();assert [x[1][0] for x in r.history]==[0x34,0x44]
        print('PASS_PTY_FIFO_CONFIG_59MS_BATCH_GAPS_COMPLETE_MASKS_NO_MOTION_NO_RETRY')
    finally:r.close()
if __name__=='__main__':main(sys.argv[1])
