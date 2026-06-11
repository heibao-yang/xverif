from __future__ import annotations

import os
from dataclasses import dataclass
from typing import Any, Dict, List, Optional

from .backend import CoverageBackend, FakeCoverageBackend, NpiCoverageBackend
from .errors import XcovError

Json = Dict[str, Any]


@dataclass
class XcovSession:
    session_id: str
    vdb: str
    backend: CoverageBackend
    worker: str
    state: str = "alive"

    def close(self) -> None:
        self.backend.close()
        self.state = "closed"

    def public_json(self) -> Json:
        summary = self.backend.summary()
        return {
            "session_id": self.session_id,
            "state": self.state,
            "vdb": self.vdb,
            "test_count": summary.get("test_count", 0),
            "top_scope_count": summary.get("top_scope_count", 0),
            "worker": self.worker,
        }


class SessionManager:
    def __init__(self) -> None:
        self.sessions: Dict[str, XcovSession] = {}

    def open(self, vdb: str, name: Optional[str] = None, fake: bool = False,
             reuse: bool = True, reopen: bool = False) -> XcovSession:
        sid = name or "cov0"
        if sid in self.sessions and self.sessions[sid].state == "alive":
            if reopen:
                self.sessions[sid].close()
            elif reuse:
                return self.sessions[sid]
            else:
                raise XcovError("SESSION_EXISTS", "session already exists", session_id=sid)
        if fake or vdb == "fake":
            backend: CoverageBackend = FakeCoverageBackend(vdb)
            worker = "fake"
        else:
            backend = NpiCoverageBackend(vdb)
            worker = "npi_python"
        sess = XcovSession(session_id=sid, vdb=vdb, backend=backend, worker=worker)
        self.sessions[sid] = sess
        return sess

    def get(self, session_id: str) -> XcovSession:
        sess = self.sessions.get(session_id)
        if not sess or sess.state != "alive":
            raise XcovError("SESSION_NOT_FOUND", "coverage session not found",
                            session_id=session_id)
        return sess

    def close(self, session_id: str) -> XcovSession:
        sess = self.get(session_id)
        sess.close()
        self.sessions.pop(session_id, None)
        return sess
