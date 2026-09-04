#!/usr/bin/env python3
"""Tests use only temporary profiles; no apt, sudo, system autostart or display settings."""

import configparser
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest


REPO = Path(__file__).resolve().parent.parent
HELPER = REPO / "scripts/configure-autostart.py"
TEMPLATE = REPO / "resources/zen-writer-autostart.desktop"
LEGACY_LINE = b"/usr/local/bin/zen-writer --fullscreen &\n"
LEGACY_DESKTOP = b"[Desktop Entry]\nType=Application\nName=Zen Writer\nExec=/usr/local/bin/zen-writer --fullscreen\n"


class IsolatedTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="zen-writer-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.home = self.root / "home with spaces"
        self.home.mkdir()
        self.env = dict(os.environ)
        for key in ("XDG_CONFIG_HOME", "XDG_STATE_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME",
                    "XDG_CURRENT_DESKTOP", "XDG_SESSION_TYPE", "QT_QPA_PLATFORM", "DISPLAY",
                    "WAYLAND_DISPLAY", "BASH_ENV", "ENV"):
            self.env.pop(key, None)
        self.env["HOME"] = str(self.home)
        self.config = self.home / ".config"

    def write(self, relative, content):
        path = self.home / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
        return path


class AutostartTest(IsolatedTest):
    def configure(self, expected=0):
        result = subprocess.run([sys.executable, str(HELPER)], env=self.env,
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def assert_xdg(self, config=None):
        path = (config or self.config) / "autostart/zen-writer.desktop"
        self.assertEqual(path.read_bytes(), TEMPLATE.read_bytes())
        parser = configparser.ConfigParser()
        parser.read(path)
        self.assertEqual(parser["Desktop Entry"]["Exec"],
                         "/usr/local/bin/zen-writer-launch --autostart --fullscreen")
        return path

    def test_standard_desktops_all_use_xdg(self):
        # A labwc executable being installed must not affect an X11/Wayfire session.
        binaries = self.root / "bin"
        binaries.mkdir()
        labwc = binaries / "labwc"
        labwc.write_text("#!/bin/sh\nexit 0\n")
        labwc.chmod(0o755)
        self.env["PATH"] = str(binaries) + os.pathsep + self.env["PATH"]
        for desktop, session in (("LXDE", "x11"), ("Wayfire", "wayland"),
                                 ("labwc:wlroots", "wayland"), ("", "")):
            with self.subTest(desktop=desktop):
                self.env.update(XDG_CURRENT_DESKTOP=desktop, XDG_SESSION_TYPE=session)
                self.configure()
                self.assert_xdg()
                self.assertFalse((self.config / "labwc/autostart").exists())

    def test_repeated_install_is_idempotent(self):
        self.configure()
        path = self.assert_xdg()
        timestamp = path.stat().st_mtime_ns
        self.configure()
        self.assertEqual(path.stat().st_mtime_ns, timestamp)
        self.assertEqual(list(path.parent.glob("*.zen-writer-backup-*")), [])

    def test_preserves_unrelated_labwc_commands_and_makes_backup(self):
        other = b"# User configuration\nother-panel &\n/usr/local/bin/zen-writer --windowed &\n"
        original = other + LEGACY_LINE + b"  /usr/local/bin/zen-writer --fullscreen & # legacy\r\n"
        path = self.write(".config/labwc/autostart", original)
        path.chmod(0o750)
        self.configure()
        self.assertEqual(path.read_bytes(), other)
        self.assertEqual(path.stat().st_mode & 0o777, 0o750)
        backups = list(path.parent.glob("autostart.zen-writer-backup-*"))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_bytes(), original)
        self.configure()
        self.assertEqual(len(list(path.parent.glob("autostart.zen-writer-backup-*"))), 1)

    def test_empty_legacy_labwc_file_does_not_shadow_system_defaults(self):
        path = self.write(".config/labwc/autostart", b"\n" + LEGACY_LINE)
        self.configure()
        self.assertFalse(path.exists())
        self.assertEqual(len(list(path.parent.glob("autostart.zen-writer-backup-*"))), 1)
        self.assert_xdg()

    def test_existing_xdg_entry_is_upgraded_with_backup(self):
        path = self.write(".config/autostart/zen-writer.desktop", LEGACY_DESKTOP)
        self.configure()
        self.assert_xdg()
        self.assertEqual(list(path.parent.glob("*.zen-writer-backup-*"))[0].read_bytes(), LEGACY_DESKTOP)

    def test_custom_config_migrates_both_legacy_locations(self):
        custom = self.home / "custom config"
        self.env["XDG_CONFIG_HOME"] = str(custom)
        old_desktop = self.write(".config/autostart/zen-writer.desktop", LEGACY_DESKTOP)
        old_labwc = self.write(".config/labwc/autostart", LEGACY_LINE)
        custom_labwc = self.write("custom config/labwc/autostart", LEGACY_LINE)
        self.configure()
        self.assert_xdg(custom)
        for path in (old_desktop, old_labwc, custom_labwc):
            self.assertFalse(path.exists())
            self.assertEqual(len(list(path.parent.glob(path.name + ".zen-writer-backup-*"))), 1)

    def test_custom_config_keeps_unrecognized_old_desktop(self):
        custom = self.home / "custom"
        self.env["XDG_CONFIG_HOME"] = str(custom)
        content = b"[Desktop Entry]\nExec=/home/me/custom-launcher\n"
        path = self.write(".config/autostart/zen-writer.desktop", content)
        result = self.configure()
        self.assertEqual(path.read_bytes(), content)
        self.assertIn("left unchanged", result.stdout)

    def test_config_directory_alias_cannot_remove_new_entry(self):
        self.config.mkdir()
        alias = self.home / "config-alias"
        alias.symlink_to(self.config, target_is_directory=True)
        self.env["XDG_CONFIG_HOME"] = str(alias)
        self.configure()
        self.assert_xdg()

    def test_symlink_file_is_rejected_before_changes(self):
        target = self.write("managed-config", LEGACY_LINE)
        link = self.config / "labwc/autostart"
        link.parent.mkdir(parents=True)
        link.symlink_to(target)
        self.configure(expected=1)
        self.assertTrue(link.is_symlink())
        self.assertEqual(target.read_bytes(), LEGACY_LINE)
        self.assertFalse((self.config / "autostart/zen-writer.desktop").exists())

    def test_relative_config_directory_is_rejected(self):
        self.env["XDG_CONFIG_HOME"] = "relative-config"
        self.configure(expected=1)
        self.assertFalse(self.config.exists())

    def test_xdg_to_labwc_migration_leaves_one_launch_entry(self):
        self.write(".config/autostart/zen-writer.desktop", LEGACY_DESKTOP)
        self.write(".config/labwc/autostart", LEGACY_LINE)
        self.env.update(XDG_CURRENT_DESKTOP="labwc", XDG_SESSION_TYPE="wayland")
        self.configure()
        self.assert_xdg()
        self.assertFalse((self.config / "labwc/autostart").exists())

    @unittest.skipUnless(os.geteuid() == 0, "root invocation guard is checked when running as root")
    def test_installer_rejects_root_before_privileged_operations(self):
        result = subprocess.run(["bash", str(REPO / "scripts/install-raspberry-pi.sh")],
                                env=self.env, capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 1)
        self.assertIn("without sudo", result.stderr)
        self.assertFalse(self.config.exists())


class LauncherTest(IsolatedTest):
    def setUp(self):
        super().setUp()
        self.binaries = self.root / "installed bin"
        self.binaries.mkdir()
        self.launcher = self.binaries / "zen-writer-launch"
        shutil.copyfile(REPO / "scripts/launch-linux.sh", self.launcher)
        self.launcher.chmod(0o755)
        self.binary = self.binaries / "zen-writer"
        self.binary.write_text(
            "#!/usr/bin/env python3\n"
            "import json, os, sys, time\n"
            "from pathlib import Path\n"
            "with open(os.environ['FAKE_CAPTURE'], 'a') as capture:\n"
            "    capture.write(json.dumps({'args': sys.argv[1:], 'platform': os.environ.get('QT_QPA_PLATFORM')}) + '\\n')\n"
            "print('simulated Qt diagnostic', file=sys.stderr)\n"
            "time.sleep(float(os.environ.get('FAKE_HOLD', '0')))\n"
            "sys.exit(int(os.environ.get('FAKE_EXIT', '0')))\n"
        )
        self.binary.chmod(0o755)
        self.capture = self.root / "captured.jsonl"
        self.env.update(FAKE_CAPTURE=str(self.capture), QT_QPA_PLATFORM="offscreen")

    def launch(self, *args):
        return subprocess.run([str(self.launcher), *args], env=self.env,
                              capture_output=True, text=True, timeout=5)

    def captured(self):
        return [json.loads(line) for line in self.capture.read_text().splitlines()]

    def log_path(self):
        return Path(self.env.get("XDG_STATE_HOME", self.home / ".local/state")) / "zen-writer/launch.log"

    def test_arguments_and_unicode_paths_are_preserved(self):
        result = self.launch("--windowed", "/tmp/Тест с пробелами.txt")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.captured()[0]["args"], ["--windowed", "/tmp/Тест с пробелами.txt"])
        self.assertEqual(self.captured()[0]["platform"], "offscreen")

    def test_autostart_flag_is_not_passed_to_the_application(self):
        result = self.launch("--autostart", "--fullscreen")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.captured()[0]["args"], ["--fullscreen"])

    def test_x11_platform_selection(self):
        self.env.pop("QT_QPA_PLATFORM")
        self.env["DISPLAY"] = ":99"
        self.assertEqual(self.launch().returncode, 0)
        self.assertEqual(self.captured()[0]["platform"], "xcb")

    def test_wayland_platform_with_x11_fallback(self):
        self.env.pop("QT_QPA_PLATFORM")
        self.env.update(WAYLAND_DISPLAY="wayland-0", DISPLAY=":99")
        self.assertEqual(self.launch().returncode, 0)
        self.assertEqual(self.captured()[0]["platform"], "wayland;xcb")

    def test_failure_is_logged_and_exit_status_is_preserved(self):
        self.env["FAKE_EXIT"] = "23"
        result = self.launch()
        self.assertEqual(result.returncode, 23)
        log = self.log_path().read_text()
        self.assertIn("simulated Qt diagnostic", log)
        self.assertIn("code 23", log)
        self.assertIn(str(self.log_path()), result.stderr)

    def test_no_display_has_actionable_error(self):
        self.env.pop("QT_QPA_PLATFORM")
        result = self.launch("--autostart", "--fullscreen")
        self.assertEqual(result.returncode, 1)
        self.assertIn("No graphical display", self.log_path().read_text())
        self.assertFalse(self.capture.exists())

    def test_missing_binary_is_reported(self):
        self.binary.unlink()
        self.assertEqual(self.launch().returncode, 127)
        self.assertIn("Executable not found", self.log_path().read_text())

    def test_custom_state_directory_and_private_log_permissions(self):
        self.env["XDG_STATE_HOME"] = str(self.home / "custom state")
        self.assertEqual(self.launch().returncode, 0)
        self.assertTrue(self.log_path().is_file())
        self.assertEqual(self.log_path().stat().st_mode & 0o777, 0o600)

    def test_duplicate_autostart_does_not_start_a_second_process(self):
        self.env["FAKE_HOLD"] = "1.5"
        first = subprocess.Popen([str(self.launcher), "--autostart", "--fullscreen"],
                                 env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            deadline = time.monotonic() + 3
            while not self.capture.exists() and time.monotonic() < deadline:
                time.sleep(0.02)
            self.assertTrue(self.capture.exists())
            second = self.launch("--autostart", "--fullscreen")
            self.assertEqual(second.returncode, 0, second.stderr)
            first.communicate(timeout=5)
            self.assertEqual(first.returncode, 0)
            self.assertEqual(len(self.captured()), 1)
            self.assertIn("skipping this duplicate", self.log_path().read_text())
        finally:
            if first.poll() is None:
                first.terminate()
                first.communicate(timeout=5)


if __name__ == "__main__":
    unittest.main(verbosity=2)
