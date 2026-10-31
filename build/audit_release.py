"""Audit the candidate tree locally. --publish additionally enforces permission
status; no commit, network operation or deployment is performed.
"""
from pathlib import Path
import argparse,fnmatch,hashlib,json,re,sys,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
SKIP_DIRS={'work','release_assets','.git','.Xil','__pycache__','xsim.dir'}
SKIP_PATTERNS=('*.log','*.jou','*.pb','*.wdb','*.pyc','*.exe','*.o')
def deliverable_files():
    for p in sorted(ROOT.rglob('*')):
        if not p.is_file():continue
        rel=p.relative_to(ROOT)
        if any(s in SKIP_DIRS for s in rel.parts) or any(fnmatch.fnmatch(p.name,s) for s in SKIP_PATTERNS):continue
        yield p
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--publish',action='store_true');args=ap.parse_args()
    files=list(deliverable_files());errors=[]
    for p in files:
        rel=p.relative_to(ROOT).as_posix()
        if p.is_symlink():errors.append('symlink: '+rel)
        for part in p.relative_to(ROOT).parts:
            if not re.fullmatch(r'[a-z0-9_.-]+|README.md|SKILL.md|LICENSE',part):errors.append('filename: '+rel)
        if p.stat().st_size>50*1024*1024:errors.append('large file: '+rel)
        if p.suffix.lower() in ('.bit','.dcp','.elf','.xsa','.ltx','.ub','.ext4','.bin'):errors.append('binary release asset in sources: '+rel)
        try:t=p.read_text(encoding='utf-8')
        except UnicodeDecodeError:continue
        # Report locations, never reproduce detected credential values.
        for n,l in enumerate(t.splitlines(),1):
            if re.search(r'192\.168\.\d+\.\d+|/home/r[k]/|[CF]:[/\\](?:Users|vivado_project)',l,re.I):errors.append(f'private host path/address: {rel}:{n}')
            if re.search(r'(?:password|passwd)\s*[:=]\s*[\x22\x27][^\x22\x27]+[\x22\x27]',l,re.I):errors.append(f'possible embedded credential: {rel}:{n}')
            if re.match(r'^CONFIG_ADD_EXTRA_USERS=',l) and l.split('=',1)[1].strip()!='""':errors.append('private account configuration: '+rel)
            if re.match(r'^-----BEGIN (?:OPENSSH |RSA |EC |ENCRYPTED )?PRIVATE KEY-----',l.strip()):errors.append('private key: '+rel)
    ns={'s':'http://www.spiritconsortium.org/XMLSchema/SPIRIT/1685-2009'}
    for component in (ROOT/'src/vivado/ip_catalog').glob('*/component.xml'):
        for item in ET.parse(component).getroot().findall('.//s:file/s:name',ns):
            if not (component.parent/item.text).is_file():errors.append('missing packaged IP source: '+str(component.relative_to(ROOT))+': '+item.text)
    recipe=(ROOT/'src/petalinux/recipes/video-pipeline-init.bb').read_text()
    if '-o video_pipeline_init video_init_half_clock_hflip.c' not in recipe:errors.append('camera final wrapper not used')
    core=(ROOT/'src/ps/video/video_ui_watchdog_core.h').read_text()
    if 'UI_WATCHDOG_COOLDOWN_NS 10000000000ull' not in core:errors.append('watchdog cooldown is not final 10 seconds')
    init=(ROOT/'src/ps/video/video-status-panel').read_text()
    if 'boot_start_panel' in init or 'BOOT_SETTLE_SECONDS' in init:errors.append('fixed boot UI restart still present')
    manifest=ROOT/'build/files_sha256.json'
    if manifest.exists():
        expected=json.loads(manifest.read_text())
        actual={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in files if p!=manifest}
        if expected!=actual:errors.append('file manifest differs; regenerate only after reviewing changed inputs')
    status=json.loads((ROOT/'board/release_manifest.json').read_text())
    permission_ready=status.get('third_party_permissions')=='confirmed' and status.get('publish_ready') is True
    result={'source_files':len(files),'bytes':sum(p.stat().st_size for p in files),'technical_audit_passed':not errors,
            'errors':errors,'third_party_permissions':status.get('third_party_permissions'),'publish_ready':permission_ready and not errors}
    (ROOT/'work').mkdir(exist_ok=True)
    (ROOT/'work/audit_result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
    return 1 if errors else 2 if args.publish and not permission_ready else 0
if __name__=='__main__':sys.exit(main())
