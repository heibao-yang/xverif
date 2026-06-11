from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

from xcov.actions import Dispatcher
from xcov.backend import CoverageBackend
from xcov.session import XcovSession

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
                              "path": str(path), "allow_absolute_path": True}}},
    ]
    proc = subprocess.run([str(XCOV), "--stdio-loop"],
                          input="\n".join(json.dumps(x) for x in reqs) + "\n",
                          text=True, capture_output=True, check=False,
                          cwd=str(ROOT))
    assert proc.returncode == 0
    assert path.exists()
    assert "npiCovToggleBin" in path.read_text()


def _dispatch_opened() -> Dispatcher:
    dispatcher = Dispatcher()
    rsp = dispatcher.dispatch({
        "api_version": "xcov.v1", "request_id": "open",
        "action": "session.open", "target": {"vdb": "fake"},
        "args": {"name": "cov0", "fake": True},
        "output": {"format": "json"},
    })
    assert rsp["ok"] is True
    return dispatcher


def test_top_level_limits_output_are_merged_for_mcp_queries():
    dispatcher = _dispatch_opened()
    rsp = dispatcher.dispatch({
        "api_version": "xcov.v1", "request_id": "holes",
        "action": "cov.holes", "target": {"session_id": "cov0"},
        "args": {"metrics": ["toggle", "branch"]},
        "limits": {"max_items": 1},
        "output": {"format": "json"},
    })
    assert rsp["ok"] is True
    assert rsp["summary"]["matched_count"] == 2
    assert rsp["summary"]["returned"] == 1


def test_args_limits_take_precedence_over_top_level_limits():
    dispatcher = _dispatch_opened()
    rsp = dispatcher.dispatch({
        "api_version": "xcov.v1", "request_id": "holes",
        "action": "cov.holes", "target": {"session_id": "cov0"},
        "args": {"metrics": ["toggle", "branch"], "limits": {"max_items": 2}},
        "limits": {"max_items": 1},
        "output": {"format": "json"},
    })
    assert rsp["ok"] is True
    assert rsp["summary"]["returned"] == 2


def test_scope_summary_returns_one_requested_scope():
    dispatcher = _dispatch_opened()
    rsp = dispatcher.dispatch({
        "api_version": "xcov.v1", "request_id": "scope",
        "action": "scope.summary", "target": {"session_id": "cov0"},
        "args": {"scope": "top.u_dut"},
        "output": {"format": "json"},
    })
    assert rsp["ok"] is True
    assert rsp["summary"]["matched_count"] == 1
    item = rsp["data"]["items"][0]
    assert item["full_name"] == "top.u_dut"
    assert item["coverable"] == 4


def test_scope_children_direct_vs_recursive():
    dispatcher = _dispatch_opened()
    direct = dispatcher.dispatch({
        "api_version": "xcov.v1", "request_id": "children",
        "action": "scope.children", "target": {"session_id": "cov0"},
        "args": {"scope": "top.u_dut"},
        "output": {"format": "json"},
    })
    recursive = dispatcher.dispatch({
        "api_version": "xcov.v1", "request_id": "children-rec",
        "action": "scope.children", "target": {"session_id": "cov0"},
        "args": {"scope": "top", "recursive": True},
        "output": {"format": "json"},
    })
    assert {i["full_name"] for i in direct["data"]["items"]} == {
        "top.u_dut.u_ctrl", "top.u_dut.u_fifo"
    }
    assert "top.u_dut.u_fifo" in {i["full_name"] for i in recursive["data"]["items"]}


def test_scope_search_does_not_enrich_coverage():
    dispatcher = _dispatch_opened()
    rsp = dispatcher.dispatch({
        "api_version": "xcov.v1", "request_id": "search",
        "action": "scope.search", "target": {"session_id": "cov0"},
        "args": {"query": {"include_patterns": ["*u_fifo"], "match_fields": ["full_name"]}},
        "output": {"format": "json"},
    })
    assert rsp["ok"] is True
    assert rsp["data"]["items"][0]["full_name"] == "top.u_dut.u_fifo"
    assert "coverage_pct" not in rsp["data"]["items"][0]


def test_export_scope_tree_contains_coverage_tree(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    dispatcher = _dispatch_opened()
    rsp = dispatcher.dispatch({
        "api_version": "xcov.v1", "request_id": "tree",
        "action": "export.scope_tree", "target": {"session_id": "cov0"},
        "args": {"scope": "top.u_dut", "output": {"mode": "both", "path": "tree.json"}},
        "output": {"format": "json"},
    })
    assert rsp["ok"] is True
    assert rsp["summary"]["output_path"] == ".xverif/xcov_exports/tree.json"
    item = next(i for i in rsp["data"]["items"] if i["full_name"] == "top.u_dut")
    assert item["coverable"] == 4
    assert item["metrics"]
    assert (tmp_path / ".xverif/xcov_exports/tree.json").exists()


def test_functional_levels_filter():
    dispatcher = _dispatch_opened()
    bins = dispatcher.dispatch({
        "api_version": "xcov.v1", "request_id": "func-bin",
        "action": "functional.holes", "target": {"session_id": "cov0"},
        "args": {"levels": ["bin"]},
        "output": {"format": "json"},
    })
    cps = dispatcher.dispatch({
        "api_version": "xcov.v1", "request_id": "func-cp",
        "action": "functional.holes", "target": {"session_id": "cov0"},
        "args": {"levels": ["coverpoint"]},
        "output": {"format": "json"},
    })
    assert bins["summary"]["matched_count"] == 1
    assert cps["summary"]["matched_count"] == 0


def test_test_each_is_explicitly_unsupported():
    dispatcher = _dispatch_opened()
    rsp = dispatcher.dispatch({
        "api_version": "xcov.v1", "request_id": "each",
        "action": "cov.holes", "target": {"session_id": "cov0"},
        "args": {"test": "each"},
        "output": {"format": "json"},
    })
    assert rsp["ok"] is False
    assert rsp["error"]["code"] == "TEST_MODE_NOT_SUPPORTED"


def test_export_path_safety(tmp_path, monkeypatch):
    monkeypatch.chdir(tmp_path)
    dispatcher = _dispatch_opened()
    bad_parent = dispatcher.dispatch({
        "api_version": "xcov.v1", "request_id": "bad-parent",
        "action": "export.holes", "target": {"session_id": "cov0"},
        "args": {"output": {"mode": "file", "path": "../holes.json"}},
        "output": {"format": "json"},
    })
    bad_abs = dispatcher.dispatch({
        "api_version": "xcov.v1", "request_id": "bad-abs",
        "action": "export.holes", "target": {"session_id": "cov0"},
        "args": {"output": {"mode": "file", "path": str(tmp_path / "holes.json")}},
        "output": {"format": "json"},
    })
    ok = dispatcher.dispatch({
        "api_version": "xcov.v1", "request_id": "ok-rel",
        "action": "export.holes", "target": {"session_id": "cov0"},
        "args": {"output": {"mode": "file", "path": "holes.json"}},
        "output": {"format": "json"},
    })
    assert bad_parent["error"]["code"] == "OUTPUT_PATH_UNSAFE"
    assert bad_abs["error"]["code"] == "OUTPUT_PATH_UNSAFE"
    assert ok["ok"] is True
    assert (tmp_path / ".xverif/xcov_exports/holes.json").exists()


class CountingBackend(CoverageBackend):
    def __init__(self) -> None:
        self.scopes_called = 0

    def tests(self):
        return [{"name": "t0"}]

    def summary(self):
        return {"test_count": 1, "top_scope_count": 1}

    def scopes(self):
        self.scopes_called += 1
        raise AssertionError("session public_json must not scan scopes")

    def metrics_for_scope(self, scope, test):
        return []

    def items(self, metrics=None, scope=None, test="merged", functional_only=False):
        return []


def test_session_public_json_does_not_scan_scopes():
    backend = CountingBackend()
    session = XcovSession("cov0", "fake", backend, "fake")
    assert session.public_json()["top_scope_count"] == 1
    assert backend.scopes_called == 0
