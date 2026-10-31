#!/usr/bin/env python3
"""Build frozen C receive probes, run offline tests, and write actual results."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from uart_rx import analyze, load_trace, replay


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def integrity():
    evidence = ROOT / "references/evidence"
    source = json.loads((evidence / "provenance.json").read_text(encoding="utf-8"))
    checked = {}
    for name in ("protocol_before.h", "protocol_after.h", "board_validation.json"):
        expected = source["sources"][name]["sha256"]
        actual = digest(evidence / name)
        if actual != expected:
            raise ValueError(f"frozen evidence hash mismatch: {name}")
        checked[name] = actual
    view = json.loads((evidence / "board_validation.json").read_text(encoding="utf-8"))
    states = [r["selected_fields"] for r in view["records"] if "selected_fields" in r]
    before = next(s for s in states if s["version"].endswith("v5"))
    after = next(s for s in states if s.get("rx_bytes") == 163)
    if not (before["expired_frames"] == 5 and before["rx_frames"] == 0 and
            after["rx_frames"] == 2 and after["rx_chunks"] == 4 and
            after["rx_max_fragment_gap_ms"] == 59 and
            after["expired_frames"] == after["rejected_frames"] == after["ignored_frames"] == 0):
        raise ValueError("archived evidence fields do not support the documented case")
    return checked


def compile_probe(cc, temp, which):
    header = (ROOT / f"references/evidence/protocol_{which}.h").as_posix()
    executable = temp / (which + (".exe" if os.name == "nt" else ""))
    command = [cc, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
               f'-DUART_PROTOCOL_HEADER="{header}"', str(ROOT / "scripts/reference_parser_probe.c"),
               "-o", str(executable)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise RuntimeError(f"C probe compilation failed ({which}): {result.stderr}")
    return executable


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", help="GCC/Clang executable path; no shell command")
    parser.add_argument("--python-only", action="store_true", help="explicitly skip C; marks result partial")
    args = parser.parse_args(argv)
    output = ROOT / "validation"
    output.mkdir(exist_ok=True)
    report = {"session_date": "2026-10-31", "generated_utc": datetime.now(timezone.utc).isoformat(),
              "hardware_access": False, "real_serial_or_controller_io": False,
              "python": platform.python_version(), "platform": platform.platform(),
              "historical_board_validation_repeated": False}
    try:
        report["evidence_sha256"] = integrity()
        cc = None if args.python_only else (args.cc or shutil.which("gcc") or shutil.which("clang") or shutil.which("cc"))
        if not args.python_only and not cc:
            raise ValueError("C compiler missing; provide --cc or explicitly choose --python-only")
        with tempfile.TemporaryDirectory(prefix="uart-skill-build-") as temporary:
            if cc:
                version = subprocess.run([cc, "--version"], capture_output=True, text=True, timeout=10, check=True)
                report["compiler"] = version.stdout.splitlines()[0]
                os.environ["UART_RX_BEFORE_EXE"] = str(compile_probe(cc, Path(temporary), "before"))
                os.environ["UART_RX_AFTER_EXE"] = str(compile_probe(cc, Path(temporary), "after"))
            else:
                os.environ.pop("UART_RX_BEFORE_EXE", None)
                os.environ.pop("UART_RX_AFTER_EXE", None)
            suite = unittest.defaultTestLoader.discover(str(ROOT / "tests"), pattern="test_*.py")
            buffer = io.StringIO()
            result = unittest.TextTestRunner(stream=buffer, verbosity=2).run(suite)
            text = buffer.getvalue()
            print(text, end="")
            (output / "test_output.txt").write_text(text, encoding="utf-8")
            report.update(tests_run=result.testsRun, failures=len(result.failures), errors=len(result.errors),
                          skipped=len(result.skipped), c_probes_built=bool(cc),
                          status=("pass" if cc else "partial") if result.wasSuccessful() else "fail")
        case = load_trace(ROOT / "fixtures/fifo_config.json")
        before, after = replay(case, 50), replay(case, 200)
        report["case_replay"] = {"before": before["summary"], "after": after["summary"],
                                 "matched_after": len(after["matched_frames"]),
                                 "timing": analyze(case), "fixture_kind": "synthetic_with_archived_chunk_timing"}
        for name, value in (("replay_before.json", before), ("replay_after.json", after)):
            (output / name).write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
        report["input_sha256"] = {
            p.relative_to(ROOT).as_posix(): digest(p)
            for p in sorted(ROOT.rglob("*")) if p.is_file() and
            p.relative_to(ROOT).parts[0] != "validation" and "__pycache__" not in p.parts
        }
        (output / "result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"UART_SKILL_VERIFICATION={report['status']} tests={report['tests_run']} C={bool(cc)}")
        return 0 if result.wasSuccessful() else 1
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        report.update(status="fail", error=str(exc))
        (output / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"UART_SKILL_VERIFICATION=fail {exc}", file=sys.stderr)
        return 1
    finally:
        os.environ.pop("UART_RX_BEFORE_EXE", None)
        os.environ.pop("UART_RX_AFTER_EXE", None)


if __name__ == "__main__":
    raise SystemExit(main())
