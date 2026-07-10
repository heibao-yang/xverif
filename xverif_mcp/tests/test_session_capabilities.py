from __future__ import annotations

import pytest

from xverif_loop.sessions.capabilities import lifecycle_capability


def test_xdebug_and_xcov_expose_symmetric_managed_lifecycle_with_different_native_capabilities() -> None:
    debug = lifecycle_capability("xdebug")
    cov = lifecycle_capability("xcov")

    assert debug.native_open_action == cov.native_open_action == "session.open"
    assert debug.native_close_action == cov.native_close_action == "session.close"
    assert debug.native_kill_action == "session.kill"
    assert debug.native_gc_action == "session.gc"
    assert debug.backend_survives_loop is True
    assert debug.fixed_admin_path is True
    assert debug.json_request_style == "loop_marker"
    assert debug.managed_transport == "uds"
    assert cov.native_kill_action is None
    assert cov.native_gc_action is None
    assert cov.backend_survives_loop is False
    assert cov.fixed_admin_path is False
    assert cov.json_request_style == "output_response_format"
    assert cov.managed_transport is None


def test_unknown_backend_has_no_implicit_capability_fallback() -> None:
    with pytest.raises(ValueError, match="unsupported lifecycle backend"):
        lifecycle_capability("unknown")
