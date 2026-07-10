"""Backend lifecycle capabilities for managed loop sessions."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional


@dataclass(frozen=True)
class BackendLifecycleCapability:
    backend: str
    native_open_action: str
    native_health_action: str
    native_close_action: str
    native_kill_action: Optional[str]
    native_gc_action: Optional[str]
    backend_survives_loop: bool
    fixed_admin_path: bool
    json_request_style: str
    managed_transport: Optional[str]


CAPABILITIES = {
    "xdebug": BackendLifecycleCapability(
        backend="xdebug",
        native_open_action="session.open",
        native_health_action="session.doctor",
        native_close_action="session.close",
        native_kill_action="session.kill",
        native_gc_action="session.gc",
        backend_survives_loop=True,
        fixed_admin_path=True,
        json_request_style="loop_marker",
        managed_transport="uds",
    ),
    "xcov": BackendLifecycleCapability(
        backend="xcov",
        native_open_action="session.open",
        native_health_action="session.status",
        native_close_action="session.close",
        native_kill_action=None,
        native_gc_action=None,
        backend_survives_loop=False,
        fixed_admin_path=False,
        json_request_style="output_response_format",
        managed_transport=None,
    ),
}


def lifecycle_capability(backend: str) -> BackendLifecycleCapability:
    try:
        return CAPABILITIES[backend]
    except KeyError as exc:
        raise ValueError(f"unsupported lifecycle backend: {backend}") from exc
