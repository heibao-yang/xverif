"""FastMCP registration tests for xverif-mcp."""

from __future__ import annotations

import importlib
import json
import os
import sys
from pathlib import Path

import anyio
import pytest

XDEBUG_DIR = Path(__file__).resolve().parents[2] / "xdebug"
sys.path = [
    path for path in sys.path
    if Path(path or os.getcwd()).resolve() != XDEBUG_DIR
]

sys.modules.pop("mcp", None)
pytest.importorskip("mcp")


POLICY_ENV = [
    "XVERIF_MCP_ENABLE_COMMON",
    "XVERIF_MCP_ENABLE_DEBUG",
    "XVERIF_MCP_ENABLE_COV",
    "XVERIF_MCP_ENABLE_BIT",
    "XVERIF_MCP_ENABLE_ENTRY",
    "XVERIF_MCP_ENABLE_LOC",
    "XVERIF_MCP_ENABLE_CONTEXT",
    "XVERIF_MCP_ENABLE_CONTEXT_WRITE",
    "XVERIF_MCP_ENABLE_SVA",
    "XVERIF_MCP_ENABLE_WRITE",
]


def _server(monkeypatch: pytest.MonkeyPatch, overrides: dict[str, str] | None = None):
    for name in POLICY_ENV:
        monkeypatch.delenv(name, raising=False)
    for name, value in (overrides or {}).items():
        monkeypatch.setenv(name, value)
    if "xverif_mcp.server" in sys.modules:
        return importlib.reload(sys.modules["xverif_mcp.server"])
    return importlib.import_module("xverif_mcp.server")


def _tool_names(monkeypatch: pytest.MonkeyPatch, overrides: dict[str, str] | None = None) -> set[str]:
    server = _server(monkeypatch, overrides)

    async def _run() -> set[str]:
        tools = await server.mcp.list_tools()
        return {tool.name for tool in tools}

    return anyio.run(_run)


def _call_tool(monkeypatch: pytest.MonkeyPatch, name: str, args: dict | None = None,
               overrides: dict[str, str] | None = None):
    server = _server(monkeypatch, overrides)

    async def _run():
        result = await server.mcp.call_tool(name, args or {})
        if isinstance(result, tuple):
            return result
        return result, None

    return anyio.run(_run)


def test_mcp_server_initialize(monkeypatch: pytest.MonkeyPatch):
    server = _server(monkeypatch)
    assert server.mcp.name == "xverif"


def test_mcp_tools_list(monkeypatch: pytest.MonkeyPatch):
    """tools/list must include all expected read-only tool names by default."""
    names = _tool_names(monkeypatch)
    assert "xverif_ping" in names
    assert "xverif_debug_query" in names
    assert "xverif_debug_session_open" in names
    assert "xverif_cov_session_open" in names
    assert "xverif_cov_query" in names
    assert "xverif_debug_list_actions" in names
    assert "xverif_debug_get_schema" in names
    assert "xverif_debug_session_list" in names
    assert "xverif_debug_session_use" in names
    assert "xverif_debug_session_close" in names
    assert "xverif_session_open" not in names
    assert "xverif_session_list" not in names
    assert "xverif_session_use" not in names
    assert "xverif_session_close" not in names
    assert "xverif_debug_raw_request" in names
    assert "xverif_tools" in names
    assert "xverif_bit_eval" in names
    assert "xverif_entry_decode" in names
    assert "xverif_loc_resolve" in names
    assert "xverif_context_status" in names
    assert "xverif_sva_explain_property" in names
    assert "xverif_context_init_config" not in names


def test_mcp_ping_call(monkeypatch: pytest.MonkeyPatch):
    """Calling xverif_ping should return a string containing 'pong'."""
    content, structured = _call_tool(monkeypatch, "xverif_ping")
    assert "pong" in content[0].text.lower()
    assert structured["result"] == "pong"


def test_tool_group_disable_sva(monkeypatch: pytest.MonkeyPatch):
    env = {"XVERIF_MCP_ENABLE_SVA": "0"}
    names = _tool_names(monkeypatch, env)
    assert "xverif_sva_explain_property" not in names
    assert "xverif_debug_query" in names

    content, _ = _call_tool(monkeypatch, "xverif_tools", {}, env)
    payload = json.loads(content[0].text)
    catalog = {tool["name"] for tool in payload["tools"]}
    assert "xverif_sva_explain_property" not in catalog


def test_tool_group_disable_debug(monkeypatch: pytest.MonkeyPatch):
    names = _tool_names(monkeypatch, {"XVERIF_MCP_ENABLE_DEBUG": "0"})
    assert "xverif_debug_query" not in names
    assert "xverif_debug_session_open" not in names
    assert "xverif_wave_value_at" not in names
    assert "xverif_cov_query" in names
    assert "xverif_bit_eval" in names


def test_tool_group_disable_cov(monkeypatch: pytest.MonkeyPatch):
    names = _tool_names(monkeypatch, {"XVERIF_MCP_ENABLE_COV": "0"})
    assert "xverif_cov_query" not in names
    assert "xverif_cov_session_open" not in names
    assert "xverif_debug_query" in names


def test_cov_session_fake_lifecycle(monkeypatch: pytest.MonkeyPatch):
    overrides = {
        "XVERIF_HOME": str(Path(__file__).resolve().parents[2]),
        "XVERIF_MCP_BACKEND": "direct",
    }
    server = _server(monkeypatch, overrides)

    async def _run():
        opened = await server.mcp.call_tool(
            "xverif_cov_session_open",
            {"name": "cov_fake", "vdb": "fake"},
        )
        queried = await server.mcp.call_tool(
            "xverif_cov_query",
            {"session": "cov_fake", "action": "cov.holes",
             "args": {"metrics": ["toggle", "branch"]},
             "limits": {"max_items": 1},
             "output_format": "json"},
        )
        closed = await server.mcp.call_tool(
            "xverif_cov_session_close",
            {"name": "cov_fake"},
        )
        return opened, queried, closed

    opened, queried, _ = anyio.run(_run)
    opened_payload = json.loads(opened[0].text)
    queried_payload = json.loads(queried[0].text)
    assert opened_payload["ok"] is True
    assert queried_payload["summary"]["matched_count"] == 2
    assert queried_payload["summary"]["returned"] == 1


@pytest.mark.parametrize(
    ("env_name", "missing", "present"),
    [
        ("XVERIF_MCP_ENABLE_BIT", "xverif_bit_eval", "xverif_entry_decode"),
        ("XVERIF_MCP_ENABLE_ENTRY", "xverif_entry_decode", "xverif_bit_eval"),
        ("XVERIF_MCP_ENABLE_LOC", "xverif_loc_resolve", "xverif_bit_eval"),
        ("XVERIF_MCP_ENABLE_CONTEXT", "xverif_context_status", "xverif_bit_eval"),
    ],
)
def test_tool_group_disable_stateless_groups(
    monkeypatch: pytest.MonkeyPatch,
    env_name: str,
    missing: str,
    present: str,
):
    names = _tool_names(monkeypatch, {env_name: "0"})
    assert missing not in names
    assert present in names


def test_tool_group_disable_common(monkeypatch: pytest.MonkeyPatch):
    names = _tool_names(monkeypatch, {"XVERIF_MCP_ENABLE_COMMON": "0"})
    assert "xverif_ping" not in names
    assert "xverif_tools" not in names
    assert "xverif_tool_help" not in names
    assert "xverif_debug_query" in names


def test_context_write_requires_both_switches(monkeypatch: pytest.MonkeyPatch):
    assert "xverif_context_init_config" not in _tool_names(monkeypatch)
    assert "xverif_context_init_config" not in _tool_names(
        monkeypatch,
        {"XVERIF_MCP_ENABLE_CONTEXT_WRITE": "1"},
    )
    enabled = {
        "XVERIF_MCP_ENABLE_CONTEXT": "1",
        "XVERIF_MCP_ENABLE_CONTEXT_WRITE": "1",
        "XVERIF_MCP_ENABLE_WRITE": "1",
    }
    names = _tool_names(monkeypatch, enabled)
    assert "xverif_context_init_config" in names

    content, _ = _call_tool(monkeypatch, "xverif_tools", {}, enabled)
    payload = json.loads(content[0].text)
    catalog = {tool["name"] for tool in payload["tools"]}
    assert "xverif_context_init_config" in catalog


def test_invalid_bool_policy_warning(monkeypatch: pytest.MonkeyPatch):
    content, _ = _call_tool(
        monkeypatch,
        "xverif_tools",
        {},
        {"XVERIF_MCP_ENABLE_SVA": "maybe"},
    )
    payload = json.loads(content[0].text)
    assert "xverif_sva_explain_property" in {tool["name"] for tool in payload["tools"]}
    assert payload["policy"]["warnings"]
    assert "XVERIF_MCP_ENABLE_SVA" in payload["policy"]["warnings"][0]


def test_tool_help_disabled_tool_is_hidden(monkeypatch: pytest.MonkeyPatch):
    content, _ = _call_tool(
        monkeypatch,
        "xverif_tool_help",
        {"name": "xverif_sva_explain_property"},
        {"XVERIF_MCP_ENABLE_SVA": "0"},
    )
    payload = json.loads(content[0].text)
    assert payload["ok"] is False
    assert payload["error"]["code"] == "TOOL_NOT_ENABLED"
