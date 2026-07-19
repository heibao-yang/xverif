#!/usr/bin/env python3

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Dict, List


ROOT = Path(__file__).resolve().parents[1]
XDEBUG_ROOT = Path("/home/yian/xverif")
DEFAULT_XDEBUG = XDEBUG_ROOT / "tools" / "xdebug"


CASES = [
    {
        "name": "root_dual",
        "title": "root xif_master_slave_dual_test",
        "fsdb": ROOT / "waves" / "xif_master_slave_dual_test.fsdb",
        "top": "xif_tb_top",
        "streams": [
            {"name": "root_master", "if_path": "xif_tb_top.if_master"},
            {"name": "root_slave", "if_path": "xif_tb_top.if_slave"},
        ],
    },
    {
        "name": "mode08_observe",
        "title": "mode08 reaction observe",
        "fsdb": ROOT
        / "tests"
        / "mode08_reaction_sequences"
        / "waves"
        / "mode08_reaction_sequences_test.fsdb",
        "top": "mode08_reaction_sequences_top",
        "streams": [
            {"name": "mode08_A", "if_path": "mode08_reaction_sequences_top.if_A"},
            {"name": "mode08_B", "if_path": "mode08_reaction_sequences_top.if_B"},
        ],
    },
    {
        "name": "mode10_pair",
        "title": "mode10 master/slave pairing",
        "fsdb": ROOT
        / "tests"
        / "mode10_master_slave_agent_pairing"
        / "waves"
        / "mode10_master_slave_agent_pairing_test.fsdb",
        "top": "mode10_master_slave_agent_pairing_top",
        "streams": [
            {"name": "mode10_master", "if_path": "mode10_master_slave_agent_pairing_top.if_master"},
            {"name": "mode10_slave", "if_path": "mode10_master_slave_agent_pairing_top.if_slave"},
        ],
    },
]


def build_env(home: Path) -> Dict[str, str]:
    env = os.environ.copy()
    env["HOME"] = str(home)
    env["XVERIF_HOME"] = str(XDEBUG_ROOT)

    verdi_home = Path(env.get("VERDI_HOME", "/home/yian/Synopsys/verdi/V-2023.12-SP2"))
    npi_lib = verdi_home / "share" / "NPI" / "lib" / "linux64"
    if npi_lib.exists():
        old_ld = env.get("LD_LIBRARY_PATH", "")
        env["LD_LIBRARY_PATH"] = f"{npi_lib}:{old_ld}" if old_ld else str(npi_lib)
    return env


def extract_json(stdout: str) -> Dict[str, Any]:
    try:
        return json.loads(stdout)
    except json.JSONDecodeError:
        start = stdout.find("{")
        if start < 0:
            raise
        return json.loads(stdout[start:])


class XdebugRunner:
    def __init__(self, xdebug: Path, home: Path) -> None:
        self.xdebug = xdebug
        self.env = build_env(home)
        self.session_ids: List[str] = []

    def query(
        self,
        action: str,
        target: Dict[str, Any] = None,
        args: Dict[str, Any] = None,
        limits: Dict[str, Any] = None,
        timeout: int = 120,
    ) -> Dict[str, Any]:
        req: Dict[str, Any] = {
            "api_version": "xdebug.v1",
            "action": action,
            "args": args or {},
            "output": {"verbosity": "full"},
        }
        if target is not None:
            req["target"] = target
        if limits is not None:
            req["limits"] = limits

        proc = subprocess.run(
            [str(self.xdebug), "--json", "-"],
            cwd=str(XDEBUG_ROOT),
            env=self.env,
            input=json.dumps(req) + "\n",
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
        )
        try:
            data = extract_json(proc.stdout)
        except Exception as exc:
            raise RuntimeError(
                f"{action}: failed to parse xdebug JSON rc={proc.returncode}\n"
                f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
            ) from exc

        if proc.returncode != 0 or not data.get("ok", False):
            raise RuntimeError(
                f"{action}: xdebug query failed rc={proc.returncode}\n"
                f"request:\n{json.dumps(req, indent=2)}\n"
                f"response:\n{json.dumps(data, indent=2)}\n"
                f"stderr:\n{proc.stderr}"
            )
        return data

    def open_session(self, fsdb: Path, name: str) -> Dict[str, Any]:
        data = self.query(
            "session.open",
            target={"fsdb": str(fsdb)},
            args={"name": name},
            timeout=180,
        )
        session = data.get("session") or data.get("data", {}).get("session", {})
        sid = session["id"]
        self.session_ids.append(sid)
        return {"session_id": sid}

    def cleanup(self) -> None:
        for sid in reversed(self.session_ids):
            try:
                self.query("session.kill", args={"id": sid}, timeout=30)
            except Exception:
                pass
        try:
            self.query("session.kill", args={"id": "all"}, timeout=30)
        except Exception:
            pass


def stream_config(case: Dict[str, Any]) -> Dict[str, Any]:
    streams = []
    top = case["top"]
    for spec in case["streams"]:
        if_path = spec["if_path"]
        streams.append(
            {
                "name": spec["name"],
                "clock": f"{top}.clk",
                "clock_edge": "negedge",
                "reset": f"!{top}.rst_n",
                "vld": f"{if_path}.vld",
                "rdy": f"{if_path}.rdy",
                "data_fields": {"pd": f"{if_path}.pd"},
                "description": f"{case['title']} {spec['name']}",
            }
        )
    return {"streams": streams}


def summary_count(data: Dict[str, Any], key: str, default: int = 0) -> int:
    summary = data.get("data", {}).get("summary", data.get("summary", {}))
    return int(summary.get(key, default))


def render_report(rows: List[Dict[str, Any]], report: Path) -> None:
    lines = [
        "# xdebug stream report",
        "",
        "本报告由 `scripts/xdebug_stream_report.py` 生成，输入为仿真产出的 FSDB。",
        "",
        "| case | stream | validate | transfers | stalls | first transfer | export rows |",
        "| --- | --- | --- | ---: | ---: | --- | ---: |",
    ]
    for row in rows:
        lines.append(
            "| {case} | {stream} | {validate} | {transfers} | {stalls} | {first_time} | {export_rows} |".format(
                **row
            )
        )

    lines.extend(["", "## Artifacts", ""])
    for row in rows:
        lines.append(f"- `{row['case']}/{row['stream']}`: `{row['export_file']}`")

    report.write_text("\n".join(lines) + "\n", encoding="utf-8")


def run_case(runner: XdebugRunner, case: Dict[str, Any], out_dir: Path) -> List[Dict[str, Any]]:
    fsdb = case["fsdb"]
    if not fsdb.is_file() or fsdb.stat().st_size == 0:
        raise RuntimeError(f"missing FSDB for {case['title']}: {fsdb}")

    session_name = f"xif_{case['name']}_{os.getpid()}"
    target = runner.open_session(fsdb, session_name)
    cfg = stream_config(case)

    runner.query(
        "stream.config.load",
        target=target,
        args={"mode": "replace", "config": cfg},
    )
    runner.query("stream.config.list", target=target, args={})

    rows = []
    for stream in cfg["streams"]:
        name = stream["name"]
        validated = runner.query(
            "stream.validate",
            target=target,
            args={"stream": name, "dynamic": True, "start": "0ns", "end": "100us", "max_edges": 2048},
        )
        validate_ok = validated.get("data", {}).get("summary", {}).get("ok", False)

        summary = runner.query(
            "stream.query",
            target=target,
            args={"stream": name, "query": "summary", "start": "0ns", "end": "100us", "limit": 64},
        )
        transfers = summary_count(summary, "transfer_count")
        stalls = summary_count(summary, "stall_cycles")
        if transfers <= 0:
            raise RuntimeError(f"{case['name']}/{name}: xdebug stream found no transfers")

        first = runner.query(
            "stream.query",
            target=target,
            args={"stream": name, "query": "first_transfer", "start": "0ns", "end": "100us"},
        )
        first_row = first.get("data", {}).get("row", {})
        first_time = str(first_row.get("time", first_row.get("time_str", "unknown")))

        runner.query(
            "stream.query",
            target=target,
            args={"stream": name, "query": "transfer_window", "start": "0ns", "end": "100us", "limit": 8},
        )

        export_file = out_dir / f"{case['name']}_{name}.tsv"
        exported = runner.query(
            "stream.export",
            target=target,
            args={
                "stream": name,
                "kind": "transfer",
                "format": "tsv",
                "start": "0ns",
                "end": "100us",
                "output_file": str(export_file),
            },
            timeout=180,
        )
        export_rows = int(exported.get("data", {}).get("row_count", 0))
        if export_rows <= 0:
            raise RuntimeError(f"{case['name']}/{name}: stream.export wrote no rows")

        rows.append(
            {
                "case": case["name"],
                "stream": name,
                "validate": "ok" if validate_ok else "failed",
                "transfers": transfers,
                "stalls": stalls,
                "first_time": first_time,
                "export_rows": export_rows,
                "export_file": export_file.relative_to(ROOT),
            }
        )
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--xdebug", default=str(DEFAULT_XDEBUG))
    parser.add_argument("--report", default=str(ROOT / "reports" / "xdebug_stream_report.md"))
    parser.add_argument("--case", action="append", choices=[case["name"] for case in CASES])
    args = parser.parse_args()

    xdebug = Path(args.xdebug).expanduser().resolve()
    if not xdebug.is_file():
        raise RuntimeError(f"xdebug binary not found: {xdebug}")

    selected = [case for case in CASES if args.case is None or case["name"] in args.case]
    report = Path(args.report).expanduser().resolve()
    report.parent.mkdir(parents=True, exist_ok=True)
    out_dir = report.parent / "xdebug_stream_exports"
    out_dir.mkdir(parents=True, exist_ok=True)

    home = Path(tempfile.mkdtemp(prefix="xif_xdebug_home_"))
    runner = XdebugRunner(xdebug, home)
    rows: List[Dict[str, Any]] = []
    try:
        for case in selected:
            rows.extend(run_case(runner, case, out_dir))
        render_report(rows, report)
    finally:
        runner.cleanup()
        shutil.rmtree(home, ignore_errors=True)

    print(f"[PASS] wrote {report.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        sys.exit(1)
