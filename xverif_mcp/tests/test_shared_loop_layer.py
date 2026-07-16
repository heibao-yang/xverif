"""Tests for the SDK-free shared loop backend layer."""

from __future__ import annotations

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
