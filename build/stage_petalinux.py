"""Stage the final meta-user overlay into an explicitly chosen PetaLinux tree.
Existing different files require --replace and are backed up before replacement.
No build, image write, device access or remote login is performed.
"""
from pathlib import Path
import argparse,re,shutil,time
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('project',type=Path);p.add_argument('--replace',action='store_true');a=p.parse_args()
project=a.project.resolve();assert (project/'project-spec').is_dir(),'Expected an existing PetaLinux project'
files={}
for name,folders in {
 'video-pipeline-init':['video'], 'acoustic-display-control':['video'],
 'pl-control-daemon':['control'],'pl-control-init':['control'],
 'acoustic-frequency':['frequency'],'video-preprocess-control':['image']}.items():
    recipe=root/'src/petalinux/recipes'/(name+'.bb')
    files['meta-user/recipes-apps/'+name+'/'+name+'.bb']=recipe.read_bytes()
    for dep in re.findall(r'file://([^\s"\\;]+)',recipe.read_text()):
        found=[root/'src/ps'/folder/dep for folder in folders+['control'] if (root/'src/ps'/folder/dep).is_file()]
        assert found,(name,dep)
        files['meta-user/recipes-apps/'+name+'/files/'+dep]=found[0].read_bytes()
for source in (root/'src/petalinux').rglob('*'):
    if not source.is_file() or 'recipes' in source.relative_to(root/'src/petalinux').parts:continue
    rel=source.relative_to(root/'src/petalinux').as_posix()
    if rel=='config/project_config.in':dest='configs/config'
    elif rel=='config/rootfs_config.in':dest='configs/rootfs_config'
    else:
        dest='meta-user/'+rel
        if dest.endswith('.bbappend.in'):dest=dest[:-len('.bbappend.in')]+'_%.bbappend'
    files[dest]=source.read_bytes().replace(b'@PROJECT_ROOT@',str(project).encode())
targets=[(project/'project-spec'/name,data) for name,data in files.items()]
different=[dest for dest,data in targets if dest.exists() and dest.read_bytes()!=data]
if different and not a.replace:raise SystemExit('Existing files differ; use a new project or --replace to back up and replace: '+str(different[:5]))
backup=project/'final-release-backup'/time.strftime('%Y%m%d-%H%M%S')
for dest,data in targets:
    if dest.exists() and dest.read_bytes()!=data:
        saved=backup/dest.relative_to(project);saved.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(dest,saved)
    dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
print('STAGED',len(targets),'files; configure your own local account in petalinux-config -c rootfs before building.')
