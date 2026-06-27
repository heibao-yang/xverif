#!/usr/bin/env python3
"""Replay recorded xdebug JSON requests and save request/response samples."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any


def _sample_name(action: str, used: dict[str, int]) -> str:
    base = action.replace(".", "_")
    count = used.get(base, 0) + 1
    used[base] = count
    return f"{base}.json" if count == 1 else f"{base}_{count}.json"


def _load_requests(path: Path) -> list[dict[str, Any]]:
    obj = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(obj, list):
        raise SystemExit(f"{path} must contain a list")
    requests: list[dict[str, Any]] = []
    for item in obj:
        if not isinstance(item, dict):
            continue
        request = item.get("request")
        if isinstance(request, dict) and isinstance(request.get("action"), str):
            requests.append(request)
    return requests


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--requests", type=Path, required=True)
    parser.add_argument("--out-dir", type=Path, required=True)
    parser.add_argument("--xdebug", type=Path, default=Path("tools/xdebug"))
    parser.add_argument("--timeout-sec", type=float, default=180.0)
    args = parser.parse_args()

    requests = _load_requests(args.requests)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    used: dict[str, int] = {}
    summary: list[dict[str, Any]] = []
    all_ok = True

    for request in requests:
        action = request["action"]
        name = _sample_name(action, used)
        proc = subprocess.run(
            [str(args.xdebug), "--json", "-"],
            input=json.dumps(request),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=args.timeout_sec,
        )
        try:
            response = json.loads(proc.stdout)
        except Exception:
            response = {
                "ok": False,
                "action": action,
                "error": {
                    "code": "OUTPUT_PARSE_FAILED",
                    "message": proc.stdout[-2000:],
                    "stderr": proc.stderr[-2000:],
                },
            }
        sample = {
            "request": request,
            "response": response,
            "returncode": proc.returncode,
            "stderr_tail": proc.stderr[-2000:],
        }
        (args.out_dir / name).write_text(
            json.dumps(sample, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
        ok = proc.returncode == 0 and bool(response.get("ok"))
        all_ok = all_ok and ok
        summary.append({
            "action": action,
            "ok": ok,
            "returncode": proc.returncode,
            "sample": name,
            "error": response.get("error"),
        })

    (args.out_dir / "summary.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    failed = [row for row in summary if not row["ok"]]
    print(f"replayed={len(summary)} failed={len(failed)} out_dir={args.out_dir}")
    if failed:
        for row in failed[:20]:
            print(json.dumps(row, ensure_ascii=False))
    return 0 if all_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
