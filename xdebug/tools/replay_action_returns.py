#!/usr/bin/env python3
"""Replay xdebug action return cases and write evidence.

The registry is intentionally data-driven: action examples stay in
``xdebug/examples/requests`` and per-action overrides live in
``xdebug/testdata/action_return_replay/cases.json``.  The runner records raw
request/response evidence under /tmp so later fixes can keep before/after
proof without checking bulky FSDB-derived output into git.
"""

from __future__ import annotations

import argparse
import copy
import importlib
import json
import os
import re
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
XDEBUG_ROOT = REPO_ROOT / "xdebug"
REQUEST_DIR = XDEBUG_ROOT / "examples" / "requests"
SCHEMA_DIR = XDEBUG_ROOT / "schemas" / "v1" / "actions"
DEFAULT_REGISTRY = XDEBUG_ROOT / "testdata" / "action_return_replay" / "cases.json"
DEFAULT_MATRIX = REPO_ROOT / "doc" / "xdebug_action_return_replay_matrix_2026-07-09.md"


def _json_load(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def _json_write(path: Path, obj: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def _text_write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _action_slug(action: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", action).replace(".", "_")


def _deep_merge(base: Any, patch: Any) -> Any:
    if isinstance(base, dict) and isinstance(patch, dict):
        out = dict(base)
        for key, value in patch.items():
            out[key] = _deep_merge(out.get(key), value)
        return out
    return copy.deepcopy(patch)


def _substitute(obj: Any, values: dict[str, str]) -> Any:
    if isinstance(obj, str):
        for key, value in values.items():
            obj = obj.replace("${" + key + "}", value)
        return obj
    if isinstance(obj, list):
        return [_substitute(item, values) for item in obj]
    if isinstance(obj, dict):
        return {key: _substitute(value, values) for key, value in obj.items()}
    return obj


def _repo_values(tmpdir: Path) -> dict[str, str]:
    return {
        "tmpdir": str(tmpdir),
        "ai_complex_fsdb": str(XDEBUG_ROOT / "testdata" / "waveform" / "ai_complex_wave" / "out" / "waves.fsdb"),
        "stream_fsdb": str(XDEBUG_ROOT / "testdata" / "waveform" / "stream_v1" / "out" / "waves.fsdb"),
        "stream_daidir": str(XDEBUG_ROOT / "testdata" / "waveform" / "stream_v1" / "out" / "simv.daidir"),
        "design_daidir": str(XDEBUG_ROOT / "testdata" / "design" / "uart" / "simv.daidir"),
        "combined_fsdb": str(XDEBUG_ROOT / "testdata" / "combined" / "active_driver" / "out" / "waves.fsdb"),
        "combined_daidir": str(XDEBUG_ROOT / "testdata" / "combined" / "active_driver" / "out" / "simv.daidir"),
        "apb_config": str(XDEBUG_ROOT / "testdata" / "waveform" / "ai_complex_wave" / "config" / "apb0.json"),
        "event_config": str(XDEBUG_ROOT / "testdata" / "waveform" / "ai_complex_wave" / "config" / "event0.json"),
        "stream_config": str(XDEBUG_ROOT / "testdata" / "waveform" / "stream_v1" / "config" / "streams.json"),
    }


@dataclass(frozen=True)
class ReplayCase:
    action: str
    family: str
    requires: str
    setup_profile: str
    example: str
    mcp_applicable: bool
    data: dict[str, Any]

    @property
    def id(self) -> str:
        return _action_slug(self.action)


def load_registry(path: Path) -> tuple[dict[str, Any], list[ReplayCase]]:
    registry = _json_load(path)
    rows = registry.get("actions")
    if not isinstance(rows, list):
        raise SystemExit(f"{path} missing actions[]")
    cases: list[ReplayCase] = []
    seen: set[str] = set()
    for row in rows:
        if not isinstance(row, dict) or not isinstance(row.get("action"), str):
            raise SystemExit(f"{path} has invalid action row: {row!r}")
        action = row["action"]
        if action in seen:
            raise SystemExit(f"duplicate action in registry: {action}")
        seen.add(action)
        cases.append(
            ReplayCase(
                action=action,
                family=str(row.get("family", "")),
                requires=str(row.get("requires", "")),
                setup_profile=str(row.get("setup_profile", "none")),
                example=str(row.get("example", f"{action}.basic.json")),
                mcp_applicable=bool(row.get("mcp_applicable", False)),
                data=row,
            )
        )
    return registry, cases


def _catalog_actions(xdebug: Path, timeout_sec: float) -> list[str]:
    req = {"api_version": "xdebug.v1", "action": "actions"}
    proc = subprocess.run(
        [str(xdebug), "--json", "-"],
        input=json.dumps(req) + "\n",
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout_sec,
        check=False,
    )
    if proc.returncode != 0:
        raise SystemExit(f"actions catalog failed: rc={proc.returncode} stderr={proc.stderr[-1000:]}")
    rsp = json.loads(proc.stdout)
    implemented = rsp.get("data", {}).get("implemented")
    if not isinstance(implemented, list):
        raise SystemExit("actions catalog response missing data.implemented[]")
    return sorted(str(item) for item in implemented)


def _static_check(cases: list[ReplayCase], xdebug: Path, timeout_sec: float) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    registry_actions = sorted(case.action for case in cases)
    catalog_actions = _catalog_actions(xdebug, timeout_sec)
    catalog_set = set(catalog_actions)
    registry_set = set(registry_actions)
    for action in sorted(registry_set | catalog_set):
        case = next((item for item in cases if item.action == action), None)
        example_ok = case is not None and (REQUEST_DIR / case.example).exists()
        request_schema_ok = (SCHEMA_DIR / f"{action}.request.schema.json").exists()
        response_schema_ok = (SCHEMA_DIR / f"{action}.response.schema.json").exists()
        rows.append(
            {
                "action": action,
                "in_registry": action in registry_set,
                "in_runtime_catalog": action in catalog_set,
                "example_ok": example_ok,
                "request_schema_ok": request_schema_ok,
                "response_schema_ok": response_schema_ok,
                "ok": (
                    action in registry_set
                    and action in catalog_set
                    and example_ok
                    and request_schema_ok
                    and response_schema_ok
                ),
            }
        )
    return rows


def _load_request(case: ReplayCase, placeholders: dict[str, str]) -> dict[str, Any]:
    request = _json_load(REQUEST_DIR / case.example)
    request = _substitute(request, placeholders)
    if isinstance(case.data.get("success"), dict):
        request = _deep_merge(request, case.data["success"])
    if request.get("action") != case.action:
        raise SystemExit(f"{case.example} action mismatch: {request.get('action')} != {case.action}")
    return request


def _run_cli(xdebug: Path, request: dict[str, Any], output_format: str, timeout_sec: float) -> tuple[Any, dict[str, Any]]:
    assert output_format in ("json", "xout")
    argv = [str(xdebug), "-"] if output_format == "xout" else [str(xdebug), "--json", "-"]
    started = time.monotonic()
    proc = subprocess.run(
        argv,
        input=json.dumps(request) + "\n",
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout_sec,
        check=False,
    )
    elapsed_ms = int((time.monotonic() - started) * 1000)
    if output_format == "json":
        try:
            response: Any = json.loads(proc.stdout)
        except Exception:
            response = {
                "ok": False,
                "action": request.get("action"),
                "error": {
                    "code": "OUTPUT_PARSE_FAILED",
                    "message": proc.stdout[-2000:],
                    "stderr": proc.stderr[-2000:],
                },
            }
    else:
        response = proc.stdout
    meta = {
        "command": argv,
        "entry": "native_cli",
        "output_format": output_format,
        "elapsed_ms": elapsed_ms,
        "returncode": proc.returncode,
        "stderr_tail": proc.stderr[-4000:],
    }
    return response, meta


def _open_native_session(
    xdebug: Path,
    name: str,
    profile: str,
    registry: dict[str, Any],
    placeholders: dict[str, str],
    timeout_sec: float,
) -> dict[str, Any] | None:
    if profile == "none":
        return None
    fixture = _substitute(registry.get("fixture_profiles", {}).get(profile, {}), placeholders)
    target = {key: value for key, value in fixture.items() if key in ("fsdb", "daidir")}
    if not target:
        return None
    request = {
        "api_version": "xdebug.v1",
        "action": "session.open",
        "target": target,
        "args": {"name": name},
    }
    response, meta = _run_cli(xdebug, request, "json", timeout_sec)
    return {"request": request, "response": response, "metadata": meta}


def _close_native_session(xdebug: Path, name: str, timeout_sec: float) -> None:
    request = {
        "api_version": "xdebug.v1",
        "action": "session.close",
        "target": {"session_id": name},
    }
    _run_cli(xdebug, request, "json", timeout_sec)


def _prepare_native_request(
    case: ReplayCase,
    request: dict[str, Any],
    session_name: str,
    profile: str,
) -> dict[str, Any]:
    request = copy.deepcopy(request)
    action = case.action
    if action == "session.open":
        request["args"] = _deep_merge(request.get("args", {}), {"name": session_name})
        return request
    if action in ("session.list", "session.gc", "actions", "schema", "batch", "expr.normalize"):
        return request
    if action in ("session.close", "session.doctor", "session.kill"):
        request["target"] = {"session_id": session_name}
        return request
    if profile != "none":
        request["target"] = {"session_id": session_name}
    return request


def _write_evidence(
    root: Path,
    case_id: str,
    request: dict[str, Any],
    response: Any,
    metadata: dict[str, Any],
) -> None:
    evidence = root / "evidence" / case_id
    _json_write(evidence / "request.json", request)
    if isinstance(response, str):
        _text_write(evidence / "response.xout", response)
    else:
        _json_write(evidence / "response.json", response)
    _json_write(evidence / "metadata.json", metadata)


def _ok_from_response(response: Any, output_format: str, returncode: int) -> bool:
    if returncode != 0:
        return False
    if output_format == "json":
        return isinstance(response, dict) and response.get("ok") is True
    return isinstance(response, str) and response.startswith("@xdebug.")


def _run_native_case(
    case: ReplayCase,
    registry: dict[str, Any],
    xdebug: Path,
    out_root: Path,
    output_format: str,
    timeout_sec: float,
) -> dict[str, Any]:
    placeholders = _repo_values(out_root)
    session_name = f"replay_{os.getpid()}_{case.id}"
    placeholders.update(
        {
            "wave_session": session_name,
            "design_session": session_name,
            "combined_session": session_name,
        }
    )
    setup = None
    if case.setup_profile != "none" and not case.action.startswith("session."):
        setup = _open_native_session(xdebug, session_name, case.setup_profile, registry, placeholders, timeout_sec)
    request = _prepare_native_request(case, _load_request(case, placeholders), session_name, case.setup_profile)
    response, meta = _run_cli(xdebug, request, output_format, timeout_sec)
    meta["fixture"] = case.setup_profile
    if setup is not None:
        meta["setup"] = setup
    _write_evidence(out_root, f"{case.id}.native.{output_format}", request, response, meta)
    if case.setup_profile != "none" and not case.action.startswith("session."):
        _close_native_session(xdebug, session_name, timeout_sec)
    ok = _ok_from_response(response, output_format, int(meta["returncode"]))
    return {
        "case_id": f"{case.id}.native.{output_format}",
        "action": case.action,
        "entry": "native_cli",
        "output_format": output_format,
        "ok": ok,
        "returncode": meta["returncode"],
        "evidence": str(out_root / "evidence" / f"{case.id}.native.{output_format}"),
    }


def render_matrix(cases: list[ReplayCase], static_rows: list[dict[str, Any]] | None = None) -> str:
    static_by_action = {row["action"]: row for row in static_rows or []}
    lines = [
        "# xdebug Action Return Replay Matrix（2026-07-09）",
        "",
        "本矩阵由 `xdebug/tools/replay_action_returns.py --write-matrix` 生成，覆盖 registry 中的 70 个 action。",
        "",
        "| # | action | family | requires | setup | native JSON | native xout | MCP JSON | MCP xout | L0 static |",
        "|---:|---|---|---|---|---|---|---|---|---|",
    ]
    for idx, case in enumerate(cases, start=1):
        mcp = "planned" if case.mcp_applicable else "n/a"
        static = static_by_action.get(case.action, {})
        l0 = "pass" if static.get("ok") else ("pending" if not static else "fail")
        lines.append(
            f"| {idx} | `{case.action}` | {case.family} | {case.requires} | {case.setup_profile} | "
            f"planned | planned | {mcp} | {mcp} | {l0} |"
        )
    lines.extend(
        [
            "",
            "## 入口说明",
            "",
            "- `native JSON`：`tools/xdebug --json -`。",
            "- `native xout`：`tools/xdebug -`。",
            "- `MCP JSON/xout`：通过 direct backend 的 `xverif_debug_query` 或专用 session/schema/list tools。",
            "- `planned` 表示 registry 已纳入矩阵，执行状态以 `/tmp/xdebug_action_return_replay_*/summary.json` 为准。",
            "",
        ]
    )
    return "\n".join(lines)


def _case_filter(cases: list[ReplayCase], actions: list[str] | None) -> list[ReplayCase]:
    if not actions:
        return cases
    wanted = set(actions)
    selected = [case for case in cases if case.action in wanted]
    missing = sorted(wanted - {case.action for case in selected})
    if missing:
        raise SystemExit(f"unknown registry actions: {', '.join(missing)}")
    return selected


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--registry", type=Path, default=DEFAULT_REGISTRY)
    parser.add_argument("--xdebug", type=Path, default=REPO_ROOT / "tools" / "xdebug")
    parser.add_argument("--out-root", type=Path)
    parser.add_argument("--timeout-sec", type=float, default=120.0)
    parser.add_argument("--layer", choices=["L0", "L1", "L2", "L3"], default="L0")
    parser.add_argument("--entry", choices=["native-json", "native-xout", "native-all"], default="native-all")
    parser.add_argument("--action", action="append", help="Limit to one action; may be repeated.")
    parser.add_argument("--write-matrix", type=Path, nargs="?", const=DEFAULT_MATRIX)
    args = parser.parse_args()

    registry, cases = load_registry(args.registry)
    out_root = args.out_root or Path("/tmp") / f"xdebug_action_return_replay_{time.strftime('%Y%m%d_%H%M%S')}"
    out_root.mkdir(parents=True, exist_ok=True)

    static_rows = _static_check(cases, args.xdebug, args.timeout_sec)
    if args.write_matrix:
        _text_write(args.write_matrix, render_matrix(cases, static_rows))
        print(f"matrix={args.write_matrix}")

    if args.layer == "L0":
        _json_write(out_root / "matrix.json", static_rows)
        summary = {
            "layer": "L0",
            "total": len(static_rows),
            "failed": [row for row in static_rows if not row["ok"]],
            "out_root": str(out_root),
        }
        _json_write(out_root / "summary.json", summary)
        print(f"L0 total={summary['total']} failed={len(summary['failed'])} out_root={out_root}")
        return 0 if not summary["failed"] else 1

    selected = _case_filter(cases, args.action)
    formats = ["json", "xout"] if args.entry == "native-all" else [args.entry.removeprefix("native-")]
    rows: list[dict[str, Any]] = []
    for case in selected:
        for output_format in formats:
            rows.append(_run_native_case(case, registry, args.xdebug, out_root, output_format, args.timeout_sec))
    summary = {
        "layer": args.layer,
        "entry": args.entry,
        "total": len(rows),
        "failed": [row for row in rows if not row["ok"]],
        "rows": rows,
        "out_root": str(out_root),
    }
    _json_write(out_root / "summary.json", summary)
    print(f"{args.layer} total={summary['total']} failed={len(summary['failed'])} out_root={out_root}")
    return 0 if not summary["failed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
