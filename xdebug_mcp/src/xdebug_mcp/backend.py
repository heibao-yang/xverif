"""Backends for xdebug MCP tools."""

from __future__ import annotations

import json
import os
import subprocess
import time
from typing import Any, Dict, List, Optional

from .session_manager import McpSessionManager


Json = Dict[str, Any]


def error_payload(code: str, message: str, **extra: Any) -> Json:
    payload: Json = {"ok": False, "error": {"code": code, "message": message}}
    if extra:
        payload["error"].update(extra)
    return payload


def repo_root() -> str:
    return os.environ.get("XVERIF_HOME") or os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))


def default_xdebug_cmd_json() -> List[str]:
    """xdebug command that forces JSON output."""
    return [os.path.join(repo_root(), "tools", "xdebug"), "--json", "-"]


def default_xdebug_cmd_xout() -> List[str]:
    """xdebug command that uses default (XOUT) output."""
    return [os.path.join(repo_root(), "tools", "xdebug"), "-"]


def _bkill_job(job_name: Optional[str]) -> None:
    """Clean up an LSF job by name using bkill -J."""
    if not job_name:
        return
    bkill_cmd = os.environ.get("XDEBUG_LSF_BKILL", "bkill")
    try:
        subprocess.run([bkill_cmd, "-J", job_name], timeout=10, check=False)
    except Exception:
        pass


def _bkill_by_id(job_id: str) -> None:
    """Clean up an LSF job by ID using bkill <id>."""
    if not job_id:
        return
    bkill_cmd = os.environ.get("XDEBUG_LSF_BKILL", "bkill")
    try:
        subprocess.run([bkill_cmd, job_id], timeout=10, check=False)
    except Exception:
        pass


def _extract_session_id(response: Json, fallback: str) -> str:
    """Extract session_id from xdebug session.open response."""
    for key in ("summary", "data", "session"):
        value = response.get(key)
        if isinstance(value, dict):
            sid = value.get("session_id") or value.get("id")
            if isinstance(sid, str) and sid:
                return sid
    return fallback


# ---------------------------------------------------------------------------
# XdebugRunner
# ---------------------------------------------------------------------------


class XdebugRunner:
    """Runs the ``tools/xdebug`` binary as a subprocess."""

    def __init__(
        self,
        cmd_json: Optional[List[str]] = None,
        cmd_xout: Optional[List[str]] = None,
        timeout_sec: Optional[float] = None,
    ) -> None:
        self.cmd_json = cmd_json or default_xdebug_cmd_json()
        self.cmd_xout = cmd_xout or default_xdebug_cmd_xout()
        self.timeout_sec = timeout_sec or float(os.environ.get("XDEBUG_MCP_TIMEOUT_SEC", "120"))

    def _run_raw(self, request: Json, output_format: str = "json") -> dict:
        """Execute xdebug and return raw {exit_code, stdout, stderr}."""
        cmd = self.cmd_json if output_format in ("json", "envelope") else self.cmd_xout
        try:
            proc = subprocess.run(
                cmd,
                input=json.dumps(request, ensure_ascii=False),
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=self.timeout_sec,
                check=False,
            )
        except subprocess.TimeoutExpired:
            return {"exit_code": -1, "stdout": "", "stderr": f"timed out after {self.timeout_sec:g}s"}
        except OSError as exc:
            return {"exit_code": -1, "stdout": "", "stderr": str(exc)}
        return {"exit_code": proc.returncode, "stdout": proc.stdout, "stderr": proc.stderr}

    def request(self, request: Json, output_format: str = "json") -> Any:
        """Run xdebug and return parsed result based on output_format.

        The output_format parameter is optional (default "json") for backward
        compatibility with callers that only pass the request dict.

        - xout  → str (raw xdebug text output)
        - json  → dict (parsed JSON)
        - envelope → dict {exit_code, stdout, stderr, payload_format}
        """
        raw = self._run_raw(request, output_format)

        if output_format == "xout":
            return raw["stdout"]

        if output_format == "json":
            if raw["exit_code"] != 0 and not raw["stdout"].strip():
                return error_payload(
                    "XDEBUG_MCP_EXEC_FAILED",
                    raw["stderr"][-4096:] or f"exit {raw['exit_code']}",
                    exit_code=raw["exit_code"],
                )
            try:
                payload = json.loads(raw["stdout"])
            except Exception as exc:  # noqa: BLE001
                return error_payload(
                    "XDEBUG_MCP_BAD_RESPONSE",
                    f"xdebug did not return JSON: {exc}",
                    exit_code=raw["exit_code"],
                    stdout=raw["stdout"][-4096:],
                    stderr=raw["stderr"][-4096:],
                )
            if isinstance(payload, dict):
                return payload
            return error_payload("XDEBUG_MCP_BAD_RESPONSE", "xdebug JSON response was not an object")

        # envelope
        return {
            "ok": raw["exit_code"] == 0,
            "exit_code": raw["exit_code"],
            "stdout": raw["stdout"],
            "stderr": raw["stderr"],
            "payload_format": "json" if "--json" in self.cmd_json else "xout",
        }


# ---------------------------------------------------------------------------
# DirectBackend (one-shot xdebug runner, legacy compatibility)
# ---------------------------------------------------------------------------


class DirectBackend:
    """Minimal one-shot xdebug runner. Session management is handled by McpSessionManager."""

    def __init__(self, runner: Optional[XdebugRunner] = None) -> None:
        self.runner = runner or XdebugRunner()

    def ping(self) -> str:
        return "pong"

    def query(self, action: str, args: Optional[Json] = None, **kwargs: Any) -> Any:
        """Thin wrapper — delegates to request(). Kept for test compatibility."""
        req: Json = {"api_version": "xdebug.v1", "action": action}
        if args:
            req["args"] = args
        if kwargs.get("target"):
            req["target"] = kwargs["target"]
        return self.request(req, kwargs.get("output_format", "xout"))

    def request(self, request: Json, output_format: str = "json") -> Any:
        """Run a raw xdebug request."""
        request = dict(request)
        request.setdefault("api_version", "xdebug.v1")
        request.setdefault("output", {})
        if isinstance(request["output"], dict):
            if output_format in ("json", "envelope"):
                request["output"].setdefault("format", "json")
            else:
                request["output"].pop("format", None)
        return self.runner.request(request, output_format)


# ---------------------------------------------------------------------------
# XDebugMcpBackend (unified dispatch via McpSessionManager)
# ---------------------------------------------------------------------------


class XDebugMcpBackend:
    """Unified backend using McpSessionManager for loop sessions.

    - session_open / query / close  → McpSessionManager (direct or LSF via launcher)
    - request (xdebug_direct_request) → one-shot XdebugRunner for backward compat
    """

    def __init__(
        self,
        mode: Optional[str] = None,
        runner: Optional[XdebugRunner] = None,
    ) -> None:
        self.mode = mode or os.environ.get("XDEBUG_MCP_BACKEND", "direct")
        self._runner = runner or XdebugRunner()
        self._sessions = McpSessionManager(mode=self.mode)
        # Backward compat: old server_legacy checks self.lsf
        self.lsf = self._sessions if self.mode == "lsf" else None
        self.direct = DirectBackend(self._runner)

    def ping(self) -> str:
        return "pong"

    def request(self, request: Json, output_format: str = "json") -> Any:
        """Raw one-shot xdebug request (legacy compatibility)."""
        return DirectBackend(self._runner).request(request, output_format)

    def session_open(self, name: str, **kwargs: Any) -> Json:
        return self._sessions.open_session(name=name, **kwargs)

    def session_list(self, **kwargs: Any) -> Json:
        return self._sessions.list_sessions()

    def session_use(self, key: str) -> Json:
        return self._sessions.use_session(key)

    def session_close(self, key: str) -> Json:
        return self._sessions.close_session(key)

    def query(self, **kwargs: Any) -> Any:
        return self._sessions.query(**kwargs)
