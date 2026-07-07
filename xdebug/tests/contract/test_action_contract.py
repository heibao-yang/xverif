from __future__ import annotations

import json
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any, Dict

import jsonschema
import pytest

from runner import CliRunner


def _load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def _runtime_catalog(cli_runner: CliRunner) -> Dict[str, Any]:
    result = cli_runner.run(
        {"api_version": "xdebug.v1", "action": "actions"},
        output_format="json",
    )
    assert result.ok, result.stderr_raw
    return result.response


def _request_schema_args_required(schema: Dict[str, Any]) -> list[str]:
    args_schema = schema.get("properties", {}).get("args", {})
    return list(args_schema.get("required", []))


def _example_satisfies_groups(args: Dict[str, Any], groups: list[list[str]]) -> bool:
    return any(all(key in args for key in group) for group in groups)


def _required_related_args(spec: Dict[str, Any]) -> set[str]:
    keys = set(spec.get("required_args", []))
    for group in spec.get("required_arg_groups", []):
        keys.update(group)
    for conditional in spec.get("conditional_required_args", []):
        keys.update(conditional.get("when", {}).keys())
        keys.update(conditional.get("required", []))
    return keys


@pytest.mark.contract
def test_runtime_catalog_matches_specs_and_referenced_files(
    cli_runner: CliRunner, xdebug_root: Path
) -> None:
    catalog = _runtime_catalog(cli_runner)
    specs = _load_json(xdebug_root / "specs" / "actions" / "actions.yaml")[
        "actions"
    ]
    specs_by_name = {spec["name"]: spec for spec in specs}
    descriptors = {
        descriptor["name"]: descriptor
        for descriptor in catalog["data"]["actions"]
    }

    expected_implemented = {
        name for name, spec in specs_by_name.items() if spec["status"] != "removed"
    }
    expected_removed = {
        name for name, spec in specs_by_name.items() if spec["status"] == "removed"
    }
    assert set(catalog["data"]["implemented"]) == expected_implemented
    assert set(catalog["data"]["removed"]) == expected_removed
    assert set(descriptors) == expected_implemented

    for name, descriptor in descriptors.items():
        spec = specs_by_name[name]
        assert descriptor["category"] == spec["category"]
        assert descriptor["status"] == spec["status"]
        assert descriptor["requires"] == spec["requires"]
        assert descriptor["handler_kind"] == spec["handler_kind"]
        assert descriptor["request_schema"] == spec["schemas"]["request"]
        assert descriptor["response_schema"] == spec["schemas"]["response"]
        assert descriptor["request_examples"] == spec["examples"]["request"]
        assert set(spec["examples"]["response"]).issubset(
            descriptor["response_examples"]
        )
        for reference in (
            descriptor["request_schema"],
            descriptor["response_schema"],
            *descriptor["request_examples"],
            *descriptor["response_examples"],
        ):
            assert (xdebug_root / reference).is_file(), reference


@pytest.mark.contract
def test_action_required_args_match_runtime_schema_and_examples(
    cli_runner: CliRunner, xdebug_root: Path
) -> None:
    catalog = _runtime_catalog(cli_runner)
    specs = _load_json(xdebug_root / "specs" / "actions" / "actions.yaml")[
        "actions"
    ]
    descriptors = {
        descriptor["name"]: descriptor
        for descriptor in catalog["data"]["actions"]
    }

    for spec in specs:
        if spec["status"] == "removed":
            continue
        name = spec["name"]
        required_args = list(spec.get("required_args", []))
        descriptor = descriptors[name]
        assert list(descriptor.get("required_args", [])) == required_args

        request_schema = _load_json(xdebug_root / spec["schemas"]["request"])
        assert _request_schema_args_required(request_schema) == required_args

        for example_ref in spec["examples"]["request"]:
            example = _load_json(xdebug_root / example_ref)
            args = example.get("args", {})
            for key in required_args:
                assert key in args, "%s example is missing args.%s" % (name, key)
            for group in spec.get("required_arg_groups", []):
                assert _example_satisfies_groups(args, [group]) or _example_satisfies_groups(
                    args, spec["required_arg_groups"]
                ), "%s example does not satisfy any required_arg_groups" % name
            for conditional in spec.get("conditional_required_args", []):
                when = conditional.get("when", {})
                if all(args.get(key) == value for key, value in when.items()):
                    for key in conditional.get("required", []):
                        assert key in args, "%s example is missing conditional args.%s" % (
                            name,
                            key,
                        )


@pytest.mark.contract
def test_action_schema_hints_are_synced(xdebug_root: Path) -> None:
    result = subprocess.run(
        [
            sys.executable,
            str(xdebug_root / "tools" / "sync_action_schema_hints.py"),
            "--check",
        ],
        cwd=xdebug_root.parent,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.contract
def test_runtime_request_schemas_are_strict_and_synced(xdebug_root: Path) -> None:
    result = subprocess.run(
        [
            sys.executable,
            str(xdebug_root / "tools" / "sync_runtime_request_schemas.py"),
            "--check",
        ],
        cwd=xdebug_root.parent,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    assert result.returncode == 0, result.stdout + result.stderr

    specs = _load_json(xdebug_root / "specs" / "actions" / "actions.yaml")[
        "actions"
    ]
    for spec in specs:
        if spec["status"] == "removed":
            continue
        schema = _load_json(xdebug_root / spec["schemas"]["request"])
        assert schema.get("additionalProperties") is False, spec["name"]
        args_schema = schema.get("properties", {}).get("args", {})
        assert args_schema.get("additionalProperties") is False, spec["name"]


@pytest.mark.contract
def test_action_schemas_explain_purpose_and_required_args(xdebug_root: Path) -> None:
    specs = _load_json(xdebug_root / "specs" / "actions" / "actions.yaml")[
        "actions"
    ]
    for spec in specs:
        if spec["status"] == "removed":
            continue
        name = spec["name"]
        request_schema = _load_json(xdebug_root / spec["schemas"]["request"])
        response_schema = _load_json(xdebug_root / spec["schemas"]["response"])

        for key in ("description", "x-purpose", "x-how_it_works", "x-when_to_use"):
            assert request_schema.get(key), "%s request schema missing %s" % (
                name,
                key,
            )
        assert response_schema.get("description"), "%s response schema missing description" % name

        args_properties = (
            request_schema.get("properties", {})
            .get("args", {})
            .get("properties", {})
        )
        for key in _required_related_args(spec):
            assert args_properties.get(key, {}).get("description"), (
                "%s request schema missing args.%s description" % (name, key)
            )


@pytest.mark.contract
def test_runtime_modes_are_derived_from_action_categories(
    cli_runner: CliRunner,
) -> None:
    catalog = _runtime_catalog(cli_runner)
    expected = defaultdict(set)
    for descriptor in catalog["data"]["actions"]:
        expected[descriptor["category"]].add(descriptor["name"])
    actual = {
        category: set(actions)
        for category, actions in catalog["data"]["modes"].items()
    }
    for category in ("builtin", "session", "design", "waveform", "combined"):
        assert actual[category] == expected[category]


@pytest.mark.contract
def test_action_inventory_matches_specs(xdebug_root: Path) -> None:
    specs = _load_json(xdebug_root / "specs" / "actions" / "actions.yaml")[
        "actions"
    ]
    expected = {
        spec["name"]: (spec["category"], spec["status"], spec["requires"])
        for spec in specs
    }
    inventory_text = (xdebug_root / "docs" / "action-inventory.md").read_text(
        encoding="utf-8"
    )
    actual = {}
    row_pattern = re.compile(
        r"^\|\s*`([^`]+)`\s*\|\s*([^\n|]+?)\s*\|\s*([^\n|]+?)\s*\|"
        r"\s*([^\n|]+?)\s*\|",
        re.MULTILINE,
    )
    for match in row_pattern.finditer(inventory_text):
        name, category, status, requires = (
            item.strip() for item in match.groups()
        )
        if category not in {"builtin", "session", "design", "waveform", "combined"}:
            continue
        actual[name] = (category, status, requires)
    assert actual == expected


@pytest.mark.contract
def test_runtime_schema_action_returns_exact_checked_in_schema(
    cli_runner: CliRunner, xdebug_root: Path
) -> None:
    catalog = _runtime_catalog(cli_runner)
    for descriptor in catalog["data"]["actions"]:
        for kind in ("request", "response"):
            result = cli_runner.run(
                {
                    "api_version": "xdebug.v1",
                    "action": "schema",
                    "args": {"action": descriptor["name"], "kind": kind},
                },
                output_format="json",
            )
            assert result.ok, (descriptor["name"], kind, result.response)
            schema_path = xdebug_root / descriptor["%s_schema" % kind]
            assert result.response["data"]["schema_path"] == descriptor[
                "%s_schema" % kind
            ]
            assert result.response["data"]["schema"] == _load_json(schema_path)


@pytest.mark.contract
def test_all_examples_validate_against_action_schemas(xdebug_root: Path) -> None:
    for kind in ("request", "response"):
        for example_path in sorted((xdebug_root / "examples" / (kind + "s")).glob("*.json")):
            example = _load_json(example_path)
            action = example["action"]
            schema = _load_json(
                xdebug_root
                / "schemas"
                / "v1"
                / "actions"
                / ("%s.%s.schema.json" % (action, kind))
            )
            jsonschema.Draft202012Validator(schema).validate(example)


@pytest.mark.contract
def test_stream_query_match_schema_rejects_legacy_field_shorthand(
    xdebug_root: Path,
) -> None:
    schema = _load_json(
        xdebug_root / "schemas" / "v1" / "actions" / "stream.query.request.schema.json"
    )
    validator = jsonschema.Draft202012Validator(schema)
    valid = {
        "api_version": "xdebug.v1",
        "action": "stream.query",
        "args": {
            "stream": "req_stream",
            "query": "match_field",
            "match": {"field": "opcode", "op": "==", "value": "8'h5a"},
        },
    }
    validator.validate(valid)

    legacy = {
        "api_version": "xdebug.v1",
        "action": "stream.query",
        "args": {
            "stream": "req_stream",
            "query": "match_field",
            "match": {"opcode": "8'h5a"},
        },
    }
    with pytest.raises(jsonschema.ValidationError):
        validator.validate(legacy)


@pytest.mark.contract
def test_response_examples_do_not_encode_removed_redundant_payloads(
    xdebug_root: Path,
) -> None:
    response_dir = xdebug_root / "examples" / "responses"
    cases = {
        "scope.list.basic.json": ["data.signals_preview", "data.examples"],
        "event.find.basic.json": ["data.examples"],
        "event.export.basic.json": ["data.examples"],
        "verify.conditions.basic.json": ["data.results", "data.examples"],
        "list.create.basic.json": ["data.summary", "data.examples"],
        "list.add.basic.json": ["data.summary", "data.examples"],
        "list.delete.basic.json": ["data.summary", "data.examples"],
        "list.show.basic.json": ["data.count", "data.summary", "data.examples"],
        "list.value_at.basic.json": ["data.summary", "data.examples"],
        "list.validate.basic.json": ["data.summary", "data.examples"],
        "list.diff.basic.json": ["data.time", "data.summary", "data.examples"],
        "list.export.basic.json": ["data.summary", "data.examples"],
        "trace.active_driver_chain.basic.json": ["data.text", "data.chain.text"],
    }
    for filename, forbidden_paths in cases.items():
        example = _load_json(response_dir / filename)
        for dotted_path in forbidden_paths:
            current = example
            found = True
            for part in dotted_path.split("."):
                if not isinstance(current, dict) or part not in current:
                    found = False
                    break
                current = current[part]
            assert not found, "%s must not contain %s" % (filename, dotted_path)


@pytest.mark.contract
@pytest.mark.parametrize("action", ["actions", "schema", "batch"])
def test_safe_request_examples_execute_with_real_binary(
    cli_runner: CliRunner, xdebug_root: Path, action: str
) -> None:
    request = _load_json(
        xdebug_root / "examples" / "requests" / ("%s.basic.json" % action)
    )
    result = cli_runner.run(request, output_format="json")
    assert result.ok, result.response
    response_schema = _load_json(
        xdebug_root
        / "schemas"
        / "v1"
        / "actions"
        / ("%s.response.schema.json" % action)
    )
    jsonschema.Draft202012Validator(response_schema).validate(result.response)
