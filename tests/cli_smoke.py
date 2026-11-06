# SPDX-License-Identifier: GPL-3.0-only
"""Check the embedded helper without touching Waywallen or real app settings."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="selector-cli-test-") as directory:
    root = Path(directory)
    preferences = root / ".config/local.desktop/WaywallenSelector.conf"
    preferences.parent.mkdir(parents=True)
    preferences.write_text("[appearance]\nautomaticAppColors=true\n")
    env = os.environ | {
        "HOME": str(root), "XDG_CONFIG_HOME": str(root / ".config"),
        "XDG_STATE_HOME": str(root / ".local/state"),
        "QT_QPA_PLATFORM": "offscreen", "QT_QUICK_BACKEND": "software",
        "DBUS_SESSION_BUS_ADDRESS": "unix:path=/nonexistent",
    }
    result = subprocess.run([sys.argv[1], "--restore-colors"], env=env,
                            capture_output=True, text=True, timeout=15)
    assert result.returncode == 0, result.stdout + result.stderr
    assert json.loads(result.stdout)["files"] == []
    assert "automaticAppColors=false" in preferences.read_text()
    print("Embedded color helper and restore CLI passed (isolated settings).")
