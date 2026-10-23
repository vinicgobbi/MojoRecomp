"""Detect the Crash of the Titans switch tables with XenonAnalyse.

Reads game/default.xex and writes config/CrashOfTheTitans_switch_tables.toml,
the switch-table input that tools/recompile.py consumes.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXE_SUFFIX = ".exe" if sys.platform == "win32" else ""
ANALYZE = (ROOT / "thirdparty" / "build" / "XenonRecomp" / "XenonAnalyse"
           / f"XenonAnalyse{EXE_SUFFIX}")
XEX = ROOT / "game" / "default.xex"
OUT = ROOT / "config" / "CrashOfTheTitans_switch_tables.toml"


def main() -> int:
    if not ANALYZE.exists():
        print(f"ERROR: XenonAnalyse is not built: {ANALYZE}", file=sys.stderr)
        return 1
    if not XEX.exists():
        print(f"ERROR: Game XEX is missing: {XEX}", file=sys.stderr)
        return 1

    result = subprocess.run([str(ANALYZE), str(XEX), str(OUT)], cwd=ROOT)
    if result.returncode != 0:
        print(f"ERROR: XenonAnalyse failed with exit code {result.returncode}",
              file=sys.stderr)
        return result.returncode

    lines = OUT.read_text(encoding="utf-8").splitlines()
    count = sum(1 for line in lines if line.startswith("[[switch]]"))
    print(f"Detected {count} switch tables -> {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
