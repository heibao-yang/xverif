from __future__ import annotations

import importlib.util
import sys
from pathlib import Path


def _load_replay_module(xdebug_root: Path):
    path = xdebug_root / "tools" / "replay_action_returns.py"
    spec = importlib.util.spec_from_file_location("replay_action_returns", path)
    assert spec is not None
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def test_action_return_replay_registry_covers_all_examples_and_schemas(
    xdebug_root: Path,
) -> None:
    replay = _load_replay_module(xdebug_root)
    registry_path = xdebug_root / "testdata" / "action_return_replay" / "cases.json"
    _, cases = replay.load_registry(registry_path)

    assert len(cases) == 70
    assert len({case.action for case in cases}) == len(cases)

    request_dir = xdebug_root / "examples" / "requests"
    schema_dir = xdebug_root / "schemas" / "v1" / "actions"
    for case in cases:
        assert (request_dir / case.example).exists(), case.action
        assert (schema_dir / f"{case.action}.request.schema.json").exists(), case.action
        assert (schema_dir / f"{case.action}.response.schema.json").exists(), case.action


def test_action_return_replay_matrix_lists_every_registered_action(
    xdebug_root: Path,
) -> None:
    replay = _load_replay_module(xdebug_root)
    registry_path = xdebug_root / "testdata" / "action_return_replay" / "cases.json"
    _, cases = replay.load_registry(registry_path)

    text = replay.render_matrix(cases)

    for case in cases:
        assert f"`{case.action}`" in text
    assert text.count("| `") == len(cases)
