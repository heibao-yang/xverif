#!/usr/bin/env python3
"""Compare xdebug JSON sample facts before and after response cleanup."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


IGNORED_PATH_PARTS = {
    "warnings",
    "findings",
    "suggested_next_actions",
    "meta",
}


def _response(obj: Any) -> dict[str, Any] | None:
    if isinstance(obj, dict) and "action" in obj and "ok" in obj:
        return obj
    if isinstance(obj, dict) and isinstance(obj.get("response"), dict):
        resp = obj["response"]
        if "action" in resp and "ok" in resp:
            return resp
    return None


def _request_key(obj: Any, response: dict[str, Any]) -> str:
    request = obj.get("request") if isinstance(obj, dict) else None
    if not isinstance(request, dict):
        request = {"action": response.get("action")}
    return json.dumps(request, sort_keys=True, ensure_ascii=False)


def _load_responses(directory: Path) -> dict[tuple[str, str], dict[str, Any]]:
    responses: dict[tuple[str, str], dict[str, Any]] = {}
    for path in sorted(directory.glob("*.json")):
        obj = json.loads(path.read_text(encoding="utf-8"))
        resp = _response(obj)
        if resp is None or resp.get("ok") is not True:
            continue
        action = resp["action"]
        responses.setdefault((action, _request_key(obj, resp)), resp)
    return responses


def _leaf_values(value: Any, path: tuple[str, ...] = ()) -> list[tuple[tuple[str, ...], Any]]:
    if any(part in IGNORED_PATH_PARTS for part in path):
        return []
    if isinstance(value, dict):
        out: list[tuple[tuple[str, ...], Any]] = []
        for key, child in value.items():
            if key == "tool":
                continue
            out.extend(_leaf_values(child, path + (key,)))
        return out
    if isinstance(value, list):
        out = []
        for index, child in enumerate(value):
            out.extend(_leaf_values(child, path + (str(index),)))
        return out
    return [(path, value)]


def _semantic_keys(response: dict[str, Any]) -> set[str]:
    keys = set()
    for path, _value in _leaf_values(response):
        if not path:
            continue
        if path[-1].isdigit() and len(path) > 1:
            keys.add(path[-2])
        else:
            keys.add(path[-1])
    return keys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    args = parser.parse_args()

    before = _load_responses(args.before)
    after = _load_responses(args.after)
    before_actions = {action for action, _request in before}
    after_actions = {action for action, _request in after}
    missing_actions = sorted(before_actions - after_actions)
    if missing_actions:
        print("missing actions: " + ", ".join(missing_actions))
        return 1

    errors: list[str] = []
    compared = 0
    for key, before_response in sorted(before.items()):
        action, _request = key
        if key not in after:
            continue
        compared += 1
        before_keys = _semantic_keys(before_response)
        after_keys = _semantic_keys(after[key])
        missing_keys = sorted(before_keys - after_keys)
        if missing_keys:
            errors.append(f"{action}: missing semantic keys: {', '.join(missing_keys)}")

    if errors:
        print("\n".join(errors))
        return 1
    print(f"compared={compared} missing_semantic_keys=0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
