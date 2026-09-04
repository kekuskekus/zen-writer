#!/usr/bin/env python3
"""Install one XDG autostart entry and retire entries from older Zen Writer installers."""

import configparser
import os
from pathlib import Path
import re
import shlex
import shutil
import stat
import tempfile
import time


LEGACY_LINE = re.compile(
    rb"^[ \t]*/usr/local/bin/zen-writer[ \t]+--fullscreen[ \t]*&[ \t]*(?:#[^\r\n]*)?(?:\r?\n)?$"
)
OWNED_COMMANDS = {
    ("/usr/local/bin/zen-writer", "--fullscreen"),
    ("/usr/local/bin/zen-writer-launch", "--fullscreen"),
    ("/usr/local/bin/zen-writer-launch", "--autostart", "--fullscreen"),
}


def backup(path):
    destination = path.with_name(f"{path.name}.zen-writer-backup-{time.time_ns()}")
    shutil.copy2(path, destination)
    print(f"Backup: {destination}")


def replace_file(path, content):
    mode = stat.S_IMODE(path.stat().st_mode) if path.exists() else 0o644
    if path.exists() and path.read_bytes() == content:
        return
    if path.exists():
        backup(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, mode)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def owned_desktop(path):
    parser = configparser.ConfigParser(interpolation=None)
    try:
        parser.read_string(path.read_text(encoding="utf-8"))
        command = tuple(shlex.split(parser.get("Desktop Entry", "Exec")))
        return command in OWNED_COMMANDS
    except (configparser.Error, UnicodeError, ValueError):
        return False


def configure():
    home = Path.home()
    config_home = Path(os.environ.get("XDG_CONFIG_HOME") or home / ".config")
    if not config_home.is_absolute():
        raise ValueError("XDG_CONFIG_HOME must be an absolute path")
    config_home = config_home.resolve()
    roots = list(dict.fromkeys((config_home, (home / ".config").resolve())))
    destination = config_home / "autostart/zen-writer.desktop"
    template = Path(__file__).resolve().parent.parent / "resources/zen-writer-autostart.desktop"
    content = template.read_bytes()

    # Do not replace a user's managed symlink with a regular file.
    for root in roots:
        for relative in ("labwc/autostart", "autostart/zen-writer.desktop"):
            path = root / relative
            if path.is_symlink():
                raise ValueError(f"Refusing to replace symlink {path}; update its target manually")
            if path.exists() and not path.is_file():
                raise ValueError(f"Expected a regular configuration file: {path}")

    # Install first, so failure cannot leave the user with no startup entry.
    replace_file(destination, content)
    print(f"XDG autostart: {destination}")

    for root in roots:
        labwc = root / "labwc/autostart"
        if labwc.exists():
            original = labwc.read_bytes()
            remaining = b"".join(line for line in original.splitlines(keepends=True)
                                 if not LEGACY_LINE.fullmatch(line))
            if remaining != original:
                if remaining.strip():
                    replace_file(labwc, remaining)
                else:
                    # An empty user script can shadow system defaults in plain labwc.
                    backup(labwc)
                    labwc.unlink()
                print(f"Retired legacy Zen Writer launch line: {labwc}")

        old_desktop = root / "autostart/zen-writer.desktop"
        if old_desktop != destination and old_desktop.exists():
            if owned_desktop(old_desktop):
                backup(old_desktop)
                old_desktop.unlink()
                print(f"Retired old XDG entry: {old_desktop}")
            else:
                print(f"Unrecognized old entry left unchanged: {old_desktop}")


if __name__ == "__main__":
    try:
        configure()
    except (OSError, ValueError) as error:
        raise SystemExit(f"Autostart setup failed: {error}") from error
