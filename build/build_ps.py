"""Linux host or sourced ARM SDK; builds only, never runs hardware commands."""
from pathlib import Path
import argparse,json,os,shlex,subprocess,hashlib
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=root/'work/bin');a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True)
cc=shlex.split(os.environ.get('CC','cc'))
flags=shlex.split(os.environ.get('CFLAGS',''))
ld=shlex.split(os.environ.get('LDFLAGS',''))
dirs=[root/'src/ps'/n for n in ('video','control','frequency','image','fsbl')]
programs={
 'video_pipeline_init':('video','video_init_half_clock_hflip.c'),
 'video_status_panel':('video','video_status_panel.c'),
 'video_ui_watchdog':('video','video_ui_watchdog.c'),
 'robot_control_daemon':('video','video_robot_service.c'),
 'camera_snapshot':('video','camera_snapshot.c'),'camera_album':('video','camera_album.c'),
 'video_display_diag':('video','video_display_diag.c'),
 'acoustic_display_control':('video','acoustic_display_control.c'),
 'pl_control_daemon':('control','pl_control_daemon.c'),
 'acoustic_frequency':('frequency','acoustic_frequency.c'),
 'acoustic_frequency_feed':('frequency','acoustic_frequency_feed.c'),
 'video_preprocess_control':('image','video_preprocess_control.c')}
result={}
for name,(folder,source) in programs.items():
    target=a.output/name
    subprocess.run(cc+flags+['-O2','-Wall','-Wextra','-Werror']+['-I'+str(d) for d in dirs]+[str(root/'src/ps'/folder/source),'-o',str(target)]+ld+['-lm'],check=True)
    result[name]=hashlib.sha256(target.read_bytes()).hexdigest()
(a.output/'sha256.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS_PS_BUILD',len(programs),'programs')
