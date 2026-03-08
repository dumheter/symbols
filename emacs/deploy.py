#!/usr/bin/env python3
"""Deploy emacs/symbols-server.el to the local Emacs configuration directory.

The destination is read from emacs/deploy.local (not tracked by git).
Create that file by copying emacs/deploy.local.example and editing it.
"""

import shutil
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
SOURCE = SCRIPT_DIR / "symbols-server.el"
CONFIG_FILE = SCRIPT_DIR / "deploy.local"
EXAMPLE_FILE = SCRIPT_DIR / "deploy.local.example"


def main() -> int:
    if not CONFIG_FILE.exists():
        print(f"error: {CONFIG_FILE} not found.")
        print(f"Copy {EXAMPLE_FILE} to {CONFIG_FILE} and set your emacs.d path.")
        return 1

    dest_dir = Path(CONFIG_FILE.read_text(encoding="utf-8").strip())

    if not dest_dir.is_dir():
        print(f"error: deploy target directory does not exist: {dest_dir}")
        return 1

    dest = dest_dir / SOURCE.name
    shutil.copy2(SOURCE, dest)
    print(f"deployed {SOURCE.name} -> {dest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
