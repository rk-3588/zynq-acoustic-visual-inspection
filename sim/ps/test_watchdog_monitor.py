"""Host-only integration with regular-file fake UIO, a sleep process and fake
service. Production paths are replaced in test copies, never environment hooks.
No board hardware or real service is touched.
"""
from pathlib import Path
import json,struct,subprocess,sys,tempfile,time
src=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='watch-monitor-test-') as directory:
    root=Path(directory);sysfs=root/'sysfs';devices=root/'devices';devices.mkdir()
    for i,(name,addr) in enumerate((('video_preprocess@40040000',0x40040000),('video_mixer@44a30000',0x44a30000),('dma@44a00000',0x44a00000))):
        node=sysfs/f'uio{i}';mapping=node/'maps/map0';mapping.mkdir(parents=True)
        (node/'name').write_text(name)
        for key,value in (('addr',addr),('size',4096),('offset',0)):(mapping/key).write_text(hex(value))
        (devices/f'uio{i}').write_bytes(bytes(4096))
    def word(i,offset,value):
        with (devices/f'uio{i}').open('r+b',buffering=0) as f:f.seek(offset);f.write(struct.pack('<I',value))
    word(0,0,0x56505031);word(1,0x40,3);word(2,0,1);word(2,0x30,1)
    sleeper=subprocess.Popen(['sleep','60'])
    (root/'panel.pid').write_text(str(sleeper.pid))
    service=root/'service';service.write_text('#!/bin/sh\nprintf "%s %s %s\\n" "$1" "$2" "$3" >>"'+str(root/'calls')+'"\n');service.chmod(0o755)
    for name in ('video_ui_watchdog_core.h','video_vdma_status.h','video_mixer.h'):(root/name).write_bytes((src/name).read_bytes())
    probe=(src/'video_readonly_probe.h').read_text().replace('"/sys/class/uio"',json.dumps(str(sysfs))).replace('"/dev"',json.dumps(str(devices)))
    (root/'video_readonly_probe.h').write_text(probe)
    code=(src/'video_ui_watchdog.c').read_text()
    for old,new in (('/run/video-status-panel-watchdog.state',str(root/'state')),('/run/video-status-panel.pid',str(root/'panel.pid')),('/usr/bin/video_status_panel',str(Path(f'/proc/{sleeper.pid}/exe').resolve())),('/etc/init.d/video-status-panel',str(service)),('/run/video-status-panel-watchdog.lock',str(root/'lock'))):code=code.replace(old,new)
    (root/'watch.c').write_text(code)
    subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror',str(root/'watch.c'),'-o',str(root/'watch')],check=True)
    subprocess.run([str(root/'watch'),'--check'],check=True)
    watcher=subprocess.Popen([str(root/'watch'),'--run'])
    try:
        time.sleep(.2)
        duplicate=subprocess.run([str(root/'watch'),'--run'],timeout=2)
        assert duplicate.returncode==4
        # The same sustained 80/s must not recover while S2MM has an error.
        word(2,0x34,0x800)
        sequence=0;start=time.monotonic();switched=False
        while time.monotonic()-start<10:
            elapsed=time.monotonic()-start
            sequence=int(elapsed*80);word(0,0x1c,sequence)
            if elapsed>=4 and not switched:
                assert not (root/'calls').exists(),'Recovery despite camera error'
                word(2,0x34,0);switched=True
            time.sleep(.025)
        calls=(root/'calls').read_text().splitlines()
        assert calls==[f'recover {sleeper.pid} {watcher.pid}'],calls
        state=json.loads((root/'state').read_text())
        assert state['recovery_attempts']==1 and state['last_result']==0
        assert 5000<state['cooldown_remaining_ms']<=10000,state
        assert state['ui_on'] and state['camera_ok']
        print('PASS_MONITOR_FAKE_UIO_REAL_SAMPLING_ERROR_GUARD_RECOVERY_COOLDOWN_SINGLETON')
    finally:
        watcher.terminate();watcher.wait(timeout=3)
        sleeper.terminate();sleeper.wait(timeout=3)
