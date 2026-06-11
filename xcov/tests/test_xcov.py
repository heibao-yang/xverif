from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
XCOV = ROOT / "tools" / "xcov"


def _run(req: dict) -> dict:
    req.setdefault("output", {})["format"] = "json"
    proc = subprocess.run([str(XCOV), "--json", "-"], input=json.dumps(req),
                          text=True, capture_output=True, check=False,
                          cwd=str(ROOT))
    assert proc.returncode == 0, proc.stderr + proc.stdout
    return json.loads(proc.stdout)


def test_session_open_fake_json():
    rsp = _run({
        "api_version": "xcov.v1",
        "request_id": "open",
        "action": "session.open",
        "target": {"vdb": "fake"},
        "args": {"name": "cov0", "fake": True},
    })
    assert rsp["ok"] is True
    assert rsp["summary"]["session_id"] == "cov0"
    assert rsp["summary"]["worker"] == "fake"


def test_stdio_loop_fake_holes():
    lines = [
        {"api_version": "xcov.v1", "request_id": "open",
         "action": "session.open", "target": {"vdb": "fake"},
         "args": {"name": "cov0", "fake": True}},
        {"api_version": "xcov.v1", "request_id": "holes",
         "action": "cov.holes", "target": {"session_id": "cov0"},
         "args": {"metrics": ["toggle", "branch"]}},
    ]
    proc = subprocess.run([str(XCOV), "--stdio-loop"],
                          input="\n".join(json.dumps(x) for x in lines) + "\n",
                          text=True, capture_output=True, check=False,
                          cwd=str(ROOT))
    assert proc.returncode == 0, proc.stderr
    out = [json.loads(line) for line in proc.stdout.splitlines()]
    assert out[0]["protocol"] == "xcov-stdio-loop"
    assert out[2]["id"] == "holes"
    assert out[2]["ok"] is True
    assert "@xcov.v1 ok action=cov.holes" in out[2]["xout"]
    assert out[2]["json"]["summary"]["matched_count"] == 2


def test_regex_rejected():
    reqs = [
        {"api_version": "xcov.v1", "request_id": "open",
         "action": "session.open", "target": {"vdb": "fake"},
         "args": {"name": "cov0", "fake": True}},
        {"api_version": "xcov.v1", "request_id": "bad",
         "action": "cov.holes", "target": {"session_id": "cov0"},
         "args": {"query": {"include_patterns": ["^top.*"]}}},
    ]
    proc = subprocess.run([str(XCOV), "--stdio-loop"],
                          input="\n".join(json.dumps(x) for x in reqs) + "\n",
                          text=True, capture_output=True, check=False,
                          cwd=str(ROOT))
    out = [json.loads(line) for line in proc.stdout.splitlines()]
    assert out[2]["ok"] is False
    assert out[2]["json"]["error"]["code"] == "REGEX_NOT_SUPPORTED"


def test_export_writes_file(tmp_path):
    path = tmp_path / "holes.ndjson"
    reqs = [
        {"api_version": "xcov.v1", "request_id": "open",
         "action": "session.open", "target": {"vdb": "fake"},
         "args": {"name": "cov0", "fake": True}},
        {"api_version": "xcov.v1", "request_id": "export",
         "action": "export.holes", "target": {"session_id": "cov0"},
         "args": {"output": {"mode": "file", "artifact_format": "ndjson",
                              "path": str(path)}}},
    ]
    proc = subprocess.run([str(XCOV), "--stdio-loop"],
                          input="\n".join(json.dumps(x) for x in reqs) + "\n",
                          text=True, capture_output=True, check=False,
                          cwd=str(ROOT))
    assert proc.returncode == 0
    assert path.exists()
    assert "npiCovToggleBin" in path.read_text()
