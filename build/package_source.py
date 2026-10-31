"""Local source archive only. Uses reviewed allowlist rules, hashes every file
and tests the resulting archive. Does not mark unresolved permissions complete.
"""
from pathlib import Path
import hashlib,json,subprocess,sys,zipfile
from audit_release import ROOT,deliverable_files
subprocess.run([sys.executable,str(ROOT/'build/audit_release.py')],check=True)
manifest=ROOT/'build/files_sha256.json'
source={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in deliverable_files() if p!=manifest}
manifest.write_text(json.dumps(source,indent=2)+'\n',encoding='utf-8',newline='\n')
target=ROOT/'work/ac16_final_20261031_source.zip'
with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED) as z:
    for p in deliverable_files():
        rel=p.relative_to(ROOT).as_posix();entry=zipfile.ZipInfo('ac16_final_20261031/'+rel,(2026,10,31,0,0,0))
        entry.compress_type=zipfile.ZIP_DEFLATED
        entry.external_attr=(0o100755 if p.suffix in ('.sh','.py') or p.name in ('video-status-panel','video-pipeline-init','robot-control','acoustic-display-control','pl-control-init','acoustic-frequency-feed') else 0o100644)<<16
        z.writestr(entry,p.read_bytes())
with zipfile.ZipFile(target) as z:
    assert z.testzip() is None
    for name,digest in source.items():assert hashlib.sha256(z.read('ac16_final_20261031/'+name)).hexdigest()==digest,name
digest=hashlib.sha256(target.read_bytes()).hexdigest()
target.with_suffix('.zip.sha256').write_text(digest+'  '+target.name+'\n',encoding='ascii',newline='\n')
print('SOURCE_ZIP_READY',target.name,'bytes='+str(target.stat().st_size),'sha256='+digest)
