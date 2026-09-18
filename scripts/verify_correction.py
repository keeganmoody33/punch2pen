#!/usr/bin/env python3
"""Send one Correction over the Punch2Pen IPC socket.

Exits 1 on connection/protocol failure. Does not prove Studio Receipt Apply.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_engine import main as verify_main

if __name__ == "__main__":
    original = sys.argv[1] if len(sys.argv) > 1 else "punch 2 pen"
    corrected = sys.argv[2] if len(sys.argv) > 2 else "Punch2Pen"
    raise SystemExit(
        verify_main(["smoke", "--original", original, "--corrected", corrected])
    )
