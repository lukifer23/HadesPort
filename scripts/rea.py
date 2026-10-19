#!/usr/bin/env python3
"""Launch the existing REA public CLI with optional project-local tool paths."""

import json
import os
from pathlib import Path
import shutil
import sys


root = Path(__file__).resolve().parents[1]
settings = root / ".local/config/tools.json"
config = json.loads(settings.read_text()) if settings.is_file() else {}
environment = os.environ.copy()
for name in ["GHIDRA_INSTALL_DIR", "JAVA_HOME"]:
    if config.get(name):
        environment.setdefault(name, config[name])
node = shutil.which("node")
launcher = Path(os.environ.get("REA_HOME") or root.parent / "rea") / "scripts/rea.mjs"
if not node or not launcher.is_file():
    sys.exit("Existing Node.js and the sibling REA public launcher are required")
os.execve(node, [node, str(launcher), *sys.argv[1:]], environment)

