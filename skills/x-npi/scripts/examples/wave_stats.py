#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from x_npi.jsonio import error, ok, print_json
from x_npi.runtime import pynpi_lifecycle
from x_npi.wave import close_fsdb, open_fsdb, time_in, value_statistics


def main() -> int:
    ap = argparse.ArgumentParser(description="Collect FSDB signal value statistics with pynpi.")
    ap.add_argument("--fsdb", required=True)
    ap.add_argument("--signal", action="append", required=True)
    ap.add_argument("--begin")
    ap.add_argument("--end")
    ap.add_argument("--max-changes", type=int, default=100000)
    args = ap.parse_args()

    try:
        with pynpi_lifecycle([sys.argv[0]]):
            fp = open_fsdb(args.fsdb)
            try:
                begin = time_in(fp, args.begin) if args.begin else None
                end = time_in(fp, args.end) if args.end else None
                stats = [value_statistics(fp, sig, begin, end, args.max_changes) for sig in args.signal]
            finally:
                close_fsdb(fp)
        print_json(ok("wave_stats", {"signals": stats}, {"count": len(stats)}))
        return 0
    except Exception as exc:
        print_json(error("wave_stats", "FAILED", str(exc)))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
