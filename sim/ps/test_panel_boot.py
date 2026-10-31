"""Linux-only service lifecycle tests; all processes and paths are isolated mocks."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

SERVICE=Path(sys.argv.pop(1)).resolve()
SOURCE=SERVICE.read_text()
HERE=Path(__file__).resolve().parent

class PanelService(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory(prefix="panel-watch-build-")
        cls.binary=Path(cls.build.name)/"panel"
        cls.watch=Path(cls.build.name)/"watch"
        for source,target in (("mock_panel.c",cls.binary),("mock_watchdog.c",cls.watch)):
            subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror","-O2",str(HERE/source),"-o",str(target)],check=True)
        subprocess.run(["sh","-n",str(SERVICE)],check=True)
    @classmethod
    def tearDownClass(cls): cls.build.cleanup()
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix="panel-watch-test-")
        self.root=Path(self.temp.name)
        self.env=dict(os.environ,MOCK_PANEL_DIR=str(self.root))
        self.service=self.root/"init"
        text=SOURCE.replace("DAEMON=/usr/bin/video_status_panel",f"DAEMON={self.binary}")
        text=text.replace("WATCHDOG=/usr/bin/video_ui_watchdog",f"WATCHDOG={self.watch}")
        text=text.replace("/run/video-status-panel",str(self.root/"video-status-panel"))
        if os.environ.get("PANEL_TEST_BUSYBOX")=="1":text=text.replace("#!/bin/sh","#!/usr/bin/busybox sh",1)
        self.service.write_text(text);self.service.chmod(0o755)
    def tearDown(self):
        # Only our two test binaries, with matching unique test environment.
        for proc in Path("/proc").iterdir():
            if not proc.name.isdigit():continue
            try:
                if proc.joinpath("exe").resolve() not in (self.binary,self.watch):continue
                if (f"MOCK_PANEL_DIR={self.root}".encode()+b"\0") not in proc.joinpath("environ").read_bytes():continue
                os.kill(int(proc.name),signal.SIGKILL)
            except (OSError,RuntimeError):pass
        self.temp.cleanup()
    def call(self,*args,ok=True):
        r=subprocess.run([str(self.service),*map(str,args)],env=self.env,capture_output=True,text=True,timeout=12)
        if ok:self.assertEqual(r.returncode,0,r.stdout+r.stderr)
        else:self.assertNotEqual(r.returncode,0,r.stdout+r.stderr)
        return r
    def count(self):
        f=self.root/"count";return int(f.read_text().strip() or "0") if f.exists() else 0
    def pid(self,watch=False):return int((self.root/("video-status-panel-watchdog.pid" if watch else "video-status-panel.pid")).read_text())
    def test_boot_only_one_start_and_start_is_idempotent(self):
        self.call("start");self.assertEqual(self.count(),1)
        ui,watch=self.pid(),self.pid(True)
        self.call("start");self.call("status")
        self.assertEqual((self.pid(),self.pid(True)),(ui,watch));self.assertEqual(self.count(),1)
        self.assertFalse((self.root/"video-status-panel-boot.state").exists())
        self.call("stop");self.call("status",ok=False)
        self.assertFalse((self.root/"video-status-panel-watchdog.pid").exists())
    def test_recovery_only_cycles_ui_and_keeps_watchdog(self):
        self.call("start");ui,watch=self.pid(),self.pid(True)
        self.call("recover",ui,watch)
        self.assertEqual(self.count(),2);self.assertNotEqual(self.pid(),ui);self.assertEqual(self.pid(True),watch)
        self.call("recover",ui,watch,ok=False);self.assertEqual(self.count(),2)
        self.call("stop")
    def test_manual_stop_cannot_be_undone_by_late_recovery(self):
        self.call("start");ui,watch=self.pid(),self.pid(True)
        self.call("stop");self.call("recover",ui,watch,ok=False)
        self.assertEqual(self.count(),1);self.call("status",ok=False)
    def test_manual_restart_replaces_both_and_rejects_old_watchdog(self):
        self.call("start");ui,watch=self.pid(),self.pid(True)
        self.call("restart");self.assertEqual(self.count(),2)
        self.assertNotEqual(self.pid(True),watch)
        self.call("recover",self.pid(),watch,ok=False);self.assertEqual(self.count(),2)
        self.call("stop")
    def test_no_recovery_count_limit(self):
        self.call("start");watch=self.pid(True)
        for _ in range(5):self.call("recover",self.pid(),watch)
        self.assertEqual(self.count(),6);self.call("stop")
    def test_restore_unconfirmed_never_restarts_ui(self):
        (self.root/"mode").write_text("no-restore")
        self.call("start");self.call("recover",self.pid(),self.pid(True),ok=False)
        self.assertEqual(self.count(),1)
    def test_no_forced_kill_on_stop_timeout(self):
        (self.root/"mode").write_text("ignore-term")
        self.call("start");ui=self.pid()
        self.call("recover",ui,self.pid(True),ok=False)
        self.assertEqual(self.count(),1);self.assertEqual(self.pid(),ui)
        self.call("status")
    def test_unready_never_starts_ui_or_watch(self):
        (self.root/"mode").write_text("unready")
        self.call("start",ok=False);self.assertEqual(self.count(),0)
        self.assertFalse((self.root/"video-status-panel-watchdog.pid").exists())
    def test_recover_requires_live_matching_monitor(self):
        self.call("start");self.call("recover",self.pid(),999999,ok=False)
        self.assertEqual(self.count(),1);self.call("stop")

if __name__=="__main__":unittest.main(verbosity=2)
