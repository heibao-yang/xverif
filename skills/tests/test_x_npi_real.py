from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
SKILL = ROOT / "skills" / "x-npi"
EXAMPLES = SKILL / "scripts" / "examples"
CONFIGS = ROOT / "skills" / "tests" / "data" / "x_npi"


def _run_example(name: str, *args: str) -> dict[str, Any]:
    env = dict(os.environ)
    env["PYTHONPATH"] = str(SKILL / "scripts")
    proc = subprocess.run(
        [sys.executable, str(EXAMPLES / name), *map(str, args)],
        cwd=ROOT,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=180,
        check=False,
    )
    assert proc.returncode == 0, proc.stderr[-8000:] + "\n" + proc.stdout[-4000:]
    document = json.loads(proc.stdout)
    assert document["ok"] is True
    assert document["meta"]["analysis_complete"] is True
    assert document["meta"]["time_base"] == "fsdb_tick"
    assert "NPI - Native Programming Interface" not in proc.stdout
    return document


def _run_perf_probe(fsdb: Path, mode: str, edge: str) -> dict[str, Any]:
    env = dict(os.environ)
    env["PYTHONPATH"] = str(SKILL / "scripts")
    proc = subprocess.run(
        [
            sys.executable, str(ROOT / "skills/tests/x_npi_perf_probe.py"),
            "--fsdb", str(fsdb), "--config", str(CONFIGS / "axi_vip.json"),
            "--mode", mode, "--edge", edge,
        ],
        cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        timeout=180, check=False,
    )
    assert proc.returncode == 0, proc.stderr[-8000:]
    return json.loads(proc.stdout)


def test_x_npi_axi_fixed_and_random_cached_waveforms(
    xverif_fixture: Any, tmp_path: Path
) -> None:
    resources = xverif_fixture("xdebug.axi_vip")
    fixed_report = tmp_path / "axi-fixed-transactions.json"
    fixed = _run_example(
        "axi_summary.py",
        "--fsdb", resources / "out/regression/test/axi_fixed_delay/waves.fsdb",
        "--config", CONFIGS / "axi_vip.json",
        "--detail", "transactions",
        "--output", fixed_report,
    )
    assert fixed["summary"]["writes"] == 32
    assert fixed["summary"]["reads"] == 32
    detail = json.loads(fixed_report.read_text(encoding="utf-8"))
    writes = detail["data"]["transactions"]["writes"]
    assert len(writes) == 32
    assert any(txn["phase_order"] == "w_before_aw" for txn in writes)

    random = _run_example(
        "axi_summary.py",
        "--fsdb", resources / "out/regression/test/axi_random_seed_7/waves.fsdb",
        "--config", CONFIGS / "axi_vip.json",
    )
    assert random["summary"]["writes"] > 0
    assert random["summary"]["reads"] > 0
    assert random["summary"]["final_write_outstanding"] == 0
    assert random["summary"]["final_read_outstanding"] == 0


def test_x_npi_apb_cached_waveform(xverif_fixture: Any) -> None:
    resources = xverif_fixture("xdebug.apb_vip")
    report = _run_example(
        "apb_summary.py",
        "--fsdb", resources / "out/regression/test/apb_vip_test/waves.fsdb",
        "--config", CONFIGS / "apb_vip.json",
    )
    assert report["summary"]["total"] == 10
    assert report["summary"]["writes"] == 5
    assert report["summary"]["reads"] == 5
    assert report["summary"]["errors"] == 1
    assert report["summary"]["wait_cycles"] == 23


def test_x_npi_stream_posedge_after_cached_waveform(xverif_fixture: Any) -> None:
    resources = xverif_fixture("xdebug.stream_v1")
    report = _run_example(
        "stream_summary.py",
        "--fsdb", resources / "out/waves.fsdb",
        "--config", CONFIGS / "stream_ready_packet.json",
    )
    assert report["meta"]["edge"] == "posedge"
    assert report["meta"]["sample_point"] == "after"
    assert report["summary"]["transfers"] == 20000
    assert report["summary"]["packets"] == 5000


def test_x_npi_streaming_performance_guard(xverif_fixture: Any) -> None:
    resources = xverif_fixture("xdebug.axi_vip")
    fsdb = resources / "out/regression/test/axi_fixed_delay/waves.fsdb"
    neg_legacy = _run_perf_probe(fsdb, "legacy", "negedge")
    neg_stream = _run_perf_probe(fsdb, "stream", "negedge")
    assert neg_stream["sample_count"] == neg_legacy["sample_count"]
    # Wall clock includes host scheduling delays from unrelated xdist workers.
    # CPU time keeps this a real per-process throughput guard without making
    # the host-only test depend on concurrent suite load.
    # Streaming preserves iterator semantics but performs per-edge normalization;
    # the current verified NPI baseline is within 15% of legacy CPU time.
    # Keep headroom for library/allocator variation while rejecting material
    # throughput regressions.
    assert neg_stream["cpu_sec"] <= neg_legacy["cpu_sec"] * 1.25
    # ru_maxrss is measured per fresh Python/NPI process.  A small fixed
    # allocator/runtime variance is expected on the same waveform, so retain
    # a meaningful regression guard without making the host-only suite flaky.
    assert neg_stream["max_rss_kb"] <= neg_legacy["max_rss_kb"] * 1.05

    pos_legacy = _run_perf_probe(fsdb, "legacy", "posedge")
    pos_stream = _run_perf_probe(fsdb, "stream", "posedge")
    assert pos_stream["sample_count"] == pos_legacy["sample_count"]
    assert pos_stream["cpu_sec"] <= pos_legacy["cpu_sec"] * 1.50
