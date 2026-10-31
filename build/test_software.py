"""Offline Linux regression. Only mocks, files, sockets and PTYs; no board I/O."""
from pathlib import Path
import argparse,os,shlex,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--quick',action='store_true');a=p.parse_args()
cc=shlex.split(os.environ.get('HOST_CC','cc'))
includes=['-I'+str(root/'src/ps'/n) for n in ('video','control','frequency','image','fsbl')]
def run(args,**kwargs):subprocess.run([str(x) for x in args],check=True,**kwargs)
with tempfile.TemporaryDirectory(prefix='ac16-test-') as temporary:
    out=Path(temporary)
    for name in ('test_ui_watchdog','test_fft','test_usb_phy','test_live','test_live_update'):
        target=out/name
        run(cc+['-O2','-Wall','-Wextra','-Werror']+includes+[root/'sim/ps'/(name+'.c'),'-o',target,'-lm'])
        fixture=out/(name+'-fixture');fixture.mkdir()
        run([target,fixture] if name=='test_live_update' else [target])
    run(['python3',root/'sim/ps/test_watchdog_monitor.py',root/'src/ps/video'])
    run(['python3',root/'sim/ps/test_panel_boot.py',root/'src/ps/video/video-status-panel'])
    if not a.quick:
        run(['python3',root/'sim/ps/test_panel_boot.py',root/'src/ps/video/video-status-panel'],env=dict(os.environ,PANEL_TEST_BUSYBOX='1'))
        service=out/'robot_test_service'
        run(cc+['-O2','-Wall','-Wextra','-Werror','-DROBOT_SERVICE_TEST']+includes+[root/'src/ps/video/video_robot_service.c','-o',service,'-lm'])
        run(['python3',root/'sim/ps/test_live.py',service])
        run(['python3',root/'sim/ps/test_fifo.py',service])
    run(['python3',root/'skill/linux-uart-receive/scripts/verify.py'])
run(['python3',root/'build/generate_delay_rom.py'])
print('PASS_RELEASE_OFFLINE_SOFTWARE_TESTS')
