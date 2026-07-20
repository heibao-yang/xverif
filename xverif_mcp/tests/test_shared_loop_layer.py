"""Tests for the SDK-free shared loop backend layer."""

from __future__ import annotations

import os
from pathlib import Path
import tomllib


def test_xverif_loop_imports_without_mcp_sdk() -> None:
    import xverif_loop
    from xverif_loop.lsf.protocol import JsonlProcess, ProtocolError
    from xverif_loop.sessions.session_manager import McpSessionManager

    assert xverif_loop is not None
    assert JsonlProcess is not None
    assert ProtocolError is not None
    assert McpSessionManager is not None


def test_xverif_loop_package_has_no_mcp_sdk_imports() -> None:
    package_root = Path(__file__).resolve().parents[1] / "src" / "xverif_loop"
    offenders: list[str] = []
    for path in package_root.rglob("*.py"):
        text = path.read_text(encoding="utf-8")
        for needle in ("from mcp", "import mcp", "xverif_mcp.server"):
            if needle in text:
                offenders.append(f"{path.relative_to(package_root)}:{needle}")
    assert offenders == []


def test_runtime_packaging_is_separate_from_testinfra() -> None:
    repo_root = Path(__file__).resolve().parents[2]
    root_project = tomllib.loads((repo_root / "pyproject.toml").read_text(encoding="utf-8"))
    runtime_project = tomllib.loads(
        (repo_root / "xverif_mcp/pyproject.toml").read_text(encoding="utf-8")
    )

    assert root_project["project"]["name"] == "xverif-testinfra"
    assert root_project["tool"]["setuptools"]["packages"]["find"]["include"] == ["testinfra*"]
    assert runtime_project["project"]["name"] == "xverif-mcp"
    assert runtime_project["project"]["scripts"] == {
        "xverif-mcp": "xverif_mcp.server:main",
        "xverif-loop-server": "xverif_loop.wrapper:server_main",
        "xverif-loop-client": "xverif_loop.wrapper:client_main",
    }


def test_runtime_defaults_use_user_xverif_home(monkeypatch, tmp_path: Path) -> None:
    from xverif_loop import logging
    from xverif_loop.wrapper import default_socket_path

    home = tmp_path / "home"
    monkeypatch.setenv("HOME", str(home))
    monkeypatch.delenv("XVERIF_TEST_TMPDIR", raising=False)
    monkeypatch.delenv("XVERIF_MCP_LOG_DIR", raising=False)
    monkeypatch.delenv("XVERIF_LOOP_SOCKET", raising=False)
    logging.configure_mcp_logging()

    assert logging.log_root() == home / ".xverif" / "mcp"
    assert default_socket_path() == str(
        home / ".xverif" / "loop-wrapper" / f"xverif-loop-{os.getuid()}.sock"
    )


def test_test_runtime_defaults_use_repository_tmp(monkeypatch, tmp_path: Path) -> None:
    from xverif_loop import logging
    from xverif_loop.wrapper import default_socket_path

    test_tmp = tmp_path / "repo" / "tmp"
    monkeypatch.setenv("XVERIF_TEST_TMPDIR", str(test_tmp))
    monkeypatch.delenv("XVERIF_MCP_LOG_DIR", raising=False)
    monkeypatch.delenv("XVERIF_LOOP_SOCKET", raising=False)
    logging.configure_mcp_logging()

    assert logging.log_root() == test_tmp / ".xverif" / "mcp"
    assert default_socket_path() == str(test_tmp / f"xverif-loop-{os.getuid()}.sock")
