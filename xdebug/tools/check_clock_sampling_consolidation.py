#!/usr/bin/env python3
"""Check that clock-aware actions use waveform/common/clock_sampling.*."""

from __future__ import annotations

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
COMMON_CLOCK = Path("waveform/common/clock_sampling.cpp")

FORBIDDEN_OUTSIDE_COMMON = (
    "sample_on_clock",
    "group_start_values",
    "clk_changed",
    "old_clk_val",
    "new_clk_val",
    "clock_edge_transition_matches",
    "ClockSampleTimeResolver",
)


def main() -> int:
    failures: list[str] = []
    for path in SRC.rglob("*"):
        if path.suffix not in {".cpp", ".h"}:
            continue
        rel = path.relative_to(SRC)
        text = path.read_text(errors="ignore")
        if rel == COMMON_CLOCK or rel == Path("waveform/common/clock_sampling.h"):
            continue
        for token in FORBIDDEN_OUTSIDE_COMMON:
            if token in text:
                failures.append(f"{rel}: contains {token}")

    if failures:
        print("clock sampling consolidation check failed:", file=sys.stderr)
        for item in failures:
            print(f"  {item}", file=sys.stderr)
        return 1

    print("clock sampling consolidation check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
