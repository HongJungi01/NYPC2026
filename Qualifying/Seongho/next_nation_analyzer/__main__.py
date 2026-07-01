"""VS Code friendly entry point.

How to use with the VS Code Run button:
1. Put one NEXT NATION log file in next_nation_analyzer/log_data/.
2. Set LOG_FILE_NAME below to that file name.
3. Run this file.

The analyzer writes outputs to:
next_nation_analyzer/log_data/<txt file name without extension>/

Command-line use still works:
python3 -m next_nation_analyzer analyze match.txt --out out/
"""

from __future__ import annotations

import sys
from pathlib import Path

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from next_nation_analyzer.core.cli import analyze_one, main
else:
    from .core.cli import analyze_one, main


# Edit this value in VS Code, then press Run.
LOG_FILE_NAME = "65611.txt"
##################################여기 수정##################################
# Re-run even if the same input hash was analyzed before.
FORCE_REANALYZE = True


class _Args:
    def __init__(self, log_file: str, out: str, force: bool) -> None:
        self.log_file = log_file
        self.out = out
        self.force = force
        self.strict = False
        self.format = "all"
        self.rules_version = None
        self.main_factor_threshold = 0.5


def run_from_log_data() -> int:
    package_dir = Path(__file__).resolve().parent
    log_data_dir = package_dir / "log_data"
    log_data_dir.mkdir(exist_ok=True)

    log_path = log_data_dir / LOG_FILE_NAME
    if not LOG_FILE_NAME or LOG_FILE_NAME == "match_001.txt":
        print("Set LOG_FILE_NAME in next_nation_analyzer/__main__.py before running.")
        print(f"Log folder: {log_data_dir}")
        return 2
    if not log_path.exists():
        print(f"Log file not found: {log_path}")
        print("Put the .txt file in next_nation_analyzer/log_data/ and set LOG_FILE_NAME exactly.")
        return 2
    if log_path.suffix.lower() not in {".txt", ".log"}:
        print(f"Unsupported file extension: {log_path.suffix}")
        return 2

    out_dir = log_data_dir / log_path.stem
    args = _Args(str(log_path), str(out_dir), FORCE_REANALYZE)
    return analyze_one(args)

if __name__ == "__main__":
    if len(sys.argv) > 1:
        raise SystemExit(main())
    raise SystemExit(run_from_log_data())
