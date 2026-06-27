#!/usr/bin/env python3
"""Audit xdebug JSON responses for public contract redundancy."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


EMPTY_TOP_LEVEL_ARRAYS = {
    "warnings",
    "findings",
    "suggested_next_actions",
}

SAME_MEANING_KEYS = [
    ("event_count", "count"),
    ("signal_count", "total_signals"),
    ("sample_count", "valid_count"),
    ("statement_count", "trace_node_count"),
]


def _is_response(obj: Any) -> bool:
    return isinstance(obj, dict) and "action" in obj and "ok" in obj


def _response_from_file_root(obj: Any) -> dict[str, Any] | None:
    if _is_response(obj):
        return obj
    if isinstance(obj, dict) and _is_response(obj.get("response")):
        return obj["response"]
    return None


def _json_files(paths: list[Path]) -> list[Path]:
    files: list[Path] = []
    for path in paths:
        if path.is_file() and path.suffix == ".json":
            files.append(path)
        elif path.is_dir():
            files.extend(sorted(path.rglob("*.json")))
    return sorted(files)


def _load(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def _same_json(a: Any, b: Any) -> bool:
    return a == b


def audit_response(path: Path, response: dict[str, Any]) -> list[str]:
    errors: list[str] = []

    data = response.get("data")
    summary = response.get("summary")

    if isinstance(data, dict) and "summary" in data:
        errors.append(f"{path}: public data.summary is forbidden")

    if isinstance(summary, dict) and isinstance(data, dict):
        for key, value in summary.items():
            if key in data and _same_json(value, data[key]):
                errors.append(f"{path}: summary.{key} duplicates data.{key}")

    for key in EMPTY_TOP_LEVEL_ARRAYS:
        if key in response and response[key] == []:
            errors.append(f"{path}: top-level {key} is an empty default array")

    meta = response.get("meta")
    if isinstance(meta, dict) and meta.get("truncated") is False and len(meta) == 1:
        errors.append(f"{path}: meta.truncated=false is a default-only field")

    if isinstance(data, dict):
        for left, right in SAME_MEANING_KEYS:
            if left in data and right in data and data[left] == data[right]:
                errors.append(f"{path}: data.{left} duplicates data.{right}")

    if response.get("action") == "trace.active_driver_chain" and isinstance(data, dict):
        chain = data.get("chain")
        if isinstance(chain, dict):
            for key in [
                "evidence_source",
                "static_candidate_count",
                "active_check_count",
                "truncated",
            ]:
                if key in data and key in chain and data[key] == chain[key]:
                    errors.append(f"{path}: data.{key} duplicates data.chain.{key}")
            stats = chain.get("stats")
            if (
                isinstance(stats, dict)
                and "temporal_boundaries" in data
                and data["temporal_boundaries"] == stats.get("temporal_boundaries")
            ):
                errors.append(
                    f"{path}: data.temporal_boundaries duplicates "
                    "data.chain.stats.temporal_boundaries"
                )

    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="+", type=Path)
    args = parser.parse_args()

    errors: list[str] = []
    checked = 0
    for path in _json_files(args.paths):
        obj = _load(path)
        response = _response_from_file_root(obj)
        if response is None:
            continue
        checked += 1
        errors.extend(audit_response(path, response))

    if errors:
        print("\n".join(errors))
        print(f"checked={checked} errors={len(errors)}")
        return 1
    print(f"checked={checked} errors=0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
