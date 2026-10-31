#!/usr/bin/env python3
"""Package the validated offline UART skill and check its embedded hashes."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parent
PACKAGE = ROOT / "linux-uart-receive"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    report = json.loads((PACKAGE / "validation/result.json").read_text(encoding="utf-8"))
    if report.get("status") != "pass" or not report.get("c_probes_built") or report.get("skipped"):
        raise ValueError("run the complete scripts/verify.py successfully before packaging")
    current = {
        p.relative_to(PACKAGE).as_posix(): sha(p.read_bytes())
        for p in sorted(PACKAGE.rglob("*")) if p.is_file() and
        p.relative_to(PACKAGE).parts[0] != "validation" and "__pycache__" not in p.parts
    }
    if current != report["input_sha256"]:
        raise ValueError("skill inputs changed after testing; run scripts/verify.py again")
    files = [ROOT / "README.md", Path(__file__).resolve()] + [
        p for p in sorted(PACKAGE.rglob("*")) if p.is_file() and "__pycache__" not in p.parts
    ]
    payload = {}
    for path in files:
        if path.is_symlink():
            raise ValueError(f"symlink not packaged: {path.name}")
        payload["skill/" + path.relative_to(ROOT).as_posix()] = path.read_bytes()
    manifest = {"schema_version": 1, "case_date": "2026-10-24", "package_date": "2026-10-31",
                "tests_run": report["tests_run"], "hardware_io_in_this_build": False,
                "files": {name: sha(data) for name, data in sorted(payload.items())}}
    manifest_data = (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    (ROOT / "manifest-sha256.json").write_bytes(manifest_data)
    payload["skill/manifest-sha256.json"] = manifest_data
    target = ROOT / "linux-uart-receive-v1.zip"
    with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(payload.items()):
            entry = zipfile.ZipInfo(name, date_time=(2026, 10, 31, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, data)
    with zipfile.ZipFile(target) as archive:
        if archive.testzip() is not None or set(archive.namelist()) != set(payload):
            raise ValueError("archive content verification failed")
        for name, expected in manifest["files"].items():
            if sha(archive.read(name)) != expected:
                raise ValueError(f"archive checksum mismatch: {name}")
    digest = sha(target.read_bytes())
    target.with_suffix(".zip.sha256").write_text(f"{digest}  {target.name}\n", encoding="ascii")
    print(json.dumps({"archive": str(target), "bytes": target.stat().st_size,
                      "files": len(payload), "sha256": digest, "checksums": "pass"}, indent=2))


if __name__ == "__main__":
    main()
