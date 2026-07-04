#!/usr/bin/env python3
"""Sync strict runtime request schemas for xdebug actions."""

from __future__ import annotations

import argparse
import copy
import json
import sys
from pathlib import Path
from typing import Any

from sync_action_schema_hints import PARAM_DESCRIPTIONS


XDEBUG_ROOT = Path(__file__).resolve().parents[1]
SPEC_PATH = XDEBUG_ROOT / "specs" / "actions" / "actions.yaml"
REQUEST_EXAMPLES = XDEBUG_ROOT / "examples" / "requests"


ADDITIONAL_ARG_SCHEMAS: dict[str, dict[str, Any]] = {
    "action": {"type": "string"},
    "address": {"type": "string"},
    "addr": {"type": "string"},
    "after": {"type": "string"},
    "aggregate": {"type": "object"},
    "aggregate_only": {"type": "boolean"},
    "around": {"type": "string"},
    "at": {"type": "string"},
    "auth_token": {"type": "string"},
    "before": {"type": "string"},
    "bind": {"type": "string"},
    "bind_host": {"type": "string"},
    "channel": {"type": "string"},
    "clk_period": {"type": "string"},
    "context_lines": {"type": "integer"},
    "data": {"oneOf": [{"type": "string"}, {"type": "array", "items": {"type": "string"}}]},
    "dependency_types": {"type": "array", "items": {"type": "string"}},
    "dynamic": {"type": "boolean"},
    "events": {"type": "boolean"},
    "field_scope": {"type": "string"},
    "from": {"type": "string"},
    "group_by": {"type": "array"},
    "host": {"type": "string"},
    "id": {"type": "string"},
    "include_alias_candidates": {"type": "boolean"},
    "include_compat_fields": {"type": "boolean"},
    "include_control": {"type": "boolean"},
    "include_parity": {"type": "boolean"},
    "include_preview": {"type": "boolean"},
    "include_statement_only": {"type": "boolean"},
    "last": {"type": "boolean"},
    "limits": {"type": "object"},
    "match": {"type": "object"},
    "max_depth": {"type": "integer"},
    "max_edges": {"type": "integer"},
    "max_rows": {"type": "integer"},
    "no_statement_only": {"type": "boolean"},
    "note": {"type": "string"},
    "num": {"type": "integer"},
    "origin": {"type": "string"},
    "output_dir": {"type": "string"},
    "packet_index": {"type": "integer"},
    "path": {"type": "string"},
    "posedge": {"type": "string"},
    "role": {"type": "string"},
    "rules": {"oneOf": [{"type": "array"}, {"type": "object"}]},
    "scan_limit": {"type": "integer"},
    "slice_hint": {"type": "object"},
    "source": {"type": "string"},
    "symbol": {"type": "string"},
    "time_unit": {"type": "string", "enum": ["auto", "fs", "ps", "ns", "us", "ms", "s"]},
    "to": {"type": "string"},
    "top_n": {"type": "integer"},
    "transport": {"type": "string", "enum": ["uds", "tcp", "file"]},
    "verbose": {"type": "boolean"},
}


EXTRA_ARGS_BY_ACTION: dict[str, set[str]] = {
    "apb.cursor": {"direction"},
    "apb.config.load": {"config_path"},
    "apb.query": {"direction", "address", "addr", "num", "last"},
    "apb.transfer_window": {"limit", "max_events", "time_range"},
    "axi.channel_stall": {"limit", "max_events", "time_range"},
    "axi.config.load": {"config_path"},
    "axi.cursor": {"direction"},
    "axi.export": {"begin", "end", "start", "to"},
    "axi.latency_outlier": {"limit", "max_events", "time_range"},
    "axi.outstanding_timeline": {"limit", "max_events", "time_range"},
    "axi.query": {"direction", "address", "addr", "id", "num", "last"},
    "axi.request_response_pair": {"limit", "max_events", "time_range"},
    "batch": {"mode"},
    "counter.statistics": {"limit", "max_events"},
    "detect_abnormal": {"limit", "max_events", "max_findings", "max_samples", "time_range"},
    "event.config.list": {"name"},
    "event.config.load": {"config_path"},
    "event.export": {
        "aggregate",
        "events",
        "edge",
        "group_by",
        "limit",
        "max_events",
        "mode",
        "name",
        "posedge",
        "rst_n",
        "scan_limit",
        "time_range",
    },
    "event.find": {
        "aggregate",
        "events",
        "edge",
        "group_by",
        "limit",
        "max_events",
        "mode",
        "name",
        "posedge",
        "rst_n",
        "scan_limit",
        "time_range",
    },
    "expr.eval_at": {"limit", "max_events", "time_range"},
    "expr.normalize": {"include_statement_only", "limit", "no_statement_only", "role", "signal"},
    "handshake.inspect": {"data", "limit", "max_events", "max_findings", "max_samples", "rules", "sampling", "time_range"},
    "list.delete": {"index"},
    "list.export": {"begin", "end", "limit", "output_file"},
    "list.show": {"name"},
    "sampled_pulse.inspect": {
        "limit",
        "max_events",
        "max_samples",
        "payloads",
        "time_range",
    },
    "scope.list": {"max_depth", "recursive"},
    "session.close": {"id", "name", "session_id"},
    "session.doctor": {"id", "name", "session_id"},
    "session.gc": {"id", "name", "session_id"},
    "session.kill": {"id", "name"},
    "session.list": {"id", "name", "session_id"},
    "session.open": {"bind", "bind_host", "host", "id", "port", "session_id", "transport"},
    "signal.changes": {"aggregate_only", "begin", "clock", "end", "from", "limit", "max_events", "max_samples", "mode", "sampling", "time_range", "to"},
    "signal.stability": {"around", "at", "begin", "clock", "conditions", "end", "from", "limit", "max_events", "max_samples", "mode", "sampling", "signals", "time_range", "to"},
    "signal.statistics": {"around", "at", "begin", "conditions", "end", "from", "limit", "max_events", "max_samples", "mode", "sampling", "signals", "time_range", "to"},
    "source.context": {"context_lines", "symbol"},
    "stream.export": {"begin", "channel", "limit", "name", "time_range"},
    "stream.query": {"begin", "channel", "field_scope", "limit", "match", "name", "packet_index", "time_range"},
    "stream.show": {"name"},
    "stream.validate": {"begin", "channel", "end", "limit", "name", "start"},
    "trace.active_driver": {
        "include_alias_candidates",
        "include_compat_fields",
        "include_control",
        "include_parity",
        "limits",
    },
    "trace.active_driver_chain": {"limits"},
    "trace.driver": {"include_statement_only", "limit", "no_statement_only", "role"},
    "trace.load": {"include_statement_only", "limit", "no_statement_only", "role"},
    "value.at": {"at", "slice_hint"},
    "value.batch_at": {"at"},
    "verify.conditions": {"at"},
    "window.verify": {"limit", "max_events", "sampling", "time_range"},
}


ENGINE_FORWARD_TIME_UNIT_EXCLUDE = {
    "actions",
    "batch",
    "schema",
    "session.close",
    "session.gc",
    "session.kill",
    "session.list",
    "session.open",
}


ARGS_REQUIRED_EXCEPTIONS = {
    "session.close",
    "session.kill",
}


TOP_LEVEL_PROPERTIES: dict[str, dict[str, Any]] = {
    "api_version": {"type": "string", "enum": ["xdebug.v1"]},
    "request_id": {"type": "string"},
    "action": {"type": "string"},
    "target": {"type": "object"},
    "args": {"type": "object"},
    "limits": {"type": "object"},
    "output": {"type": "object"},
    "auth_token": {"type": "string"},
}


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def dump_json(value: Any) -> str:
    return json.dumps(value, indent=2, ensure_ascii=False) + "\n"


def action_specs() -> list[dict[str, Any]]:
    return load_json(SPEC_PATH)["actions"]


def required_related_args(spec: dict[str, Any]) -> set[str]:
    keys = set(spec.get("required_args", []))
    for group in spec.get("required_arg_groups", []):
        keys.update(group)
    for conditional in spec.get("conditional_required_args", []):
        keys.update(conditional.get("when", {}).keys())
        keys.update(conditional.get("required", []))
    return keys


def example_args(action: str) -> set[str]:
    path = REQUEST_EXAMPLES / f"{action}.basic.json"
    if not path.exists():
        return set()
    data = load_json(path)
    args = data.get("args", {})
    return set(args) if isinstance(args, dict) else set()


def collect_arg_schemas(specs: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    arg_schemas: dict[str, dict[str, Any]] = {}
    for spec in specs:
        for kind in ("request",):
            path = XDEBUG_ROOT / spec["schemas"][kind]
            if not path.exists():
                continue
            schema = load_json(path)
            props = schema.get("properties", {}).get("args", {}).get("properties", {})
            if isinstance(props, dict):
                for key, value in props.items():
                    if key not in arg_schemas:
                        arg_schemas[key] = copy.deepcopy(value)
    for key, value in ADDITIONAL_ARG_SCHEMAS.items():
        arg_schemas.setdefault(key, copy.deepcopy(value))

    arg_schemas["checks"] = {
        "type": "array",
        "items": {
            "type": "object",
            "required": ["type"],
            "properties": {
                "type": {"type": "string", "enum": ["unknown_xz", "glitch", "stuck"]},
                "min_pulse_width": {"type": "string"},
                "min_duration": {"type": "string"},
            },
            "additionalProperties": False,
        },
        "description": "detect_abnormal checks. Each item must be an object with type; string shorthand is not supported.",
    }
    arg_schemas["direction"] = {"type": "string", "enum": ["wr", "rd", "all"]}
    arg_schemas["format"] = {
        "type": "string",
        "enum": [
            "h",
            "hex",
            "b",
            "bin",
            "binary",
            "d",
            "dec",
            "decimal",
            "array_indexed",
            "json",
            "tsv",
            "csv",
            "u64bin",
        ],
    }
    arg_schemas["kind"] = {"type": "string"}
    arg_schemas["mode"] = {"type": "string"}
    arg_schemas["op"] = {"type": "string", "enum": ["begin", "next", "prev", "pre", "last"]}
    arg_schemas["port"] = {"type": "integer"}
    arg_schemas["query"] = {"oneOf": [{"type": "string"}, {"type": "object"}]}
    arg_schemas["vld"] = {"oneOf": [{"type": "string"}, {"type": "object"}]}
    return arg_schemas


def allowed_args_for_spec(spec: dict[str, Any]) -> set[str]:
    action = spec["name"]
    keys = set(example_args(action))
    keys.update(required_related_args(spec))
    keys.update(EXTRA_ARGS_BY_ACTION.get(action, set()))
    if spec.get("handler_kind") == "engine_forward" and action not in ENGINE_FORWARD_TIME_UNIT_EXCLUDE:
        keys.add("time_unit")
    return keys


def sync_schema(schema: dict[str, Any], spec: dict[str, Any], arg_schemas: dict[str, dict[str, Any]]) -> dict[str, Any]:
    action = spec["name"]
    updated = copy.deepcopy(schema)
    updated["type"] = "object"
    updated["required"] = ["api_version", "action"]
    if spec.get("required_args") and action not in ARGS_REQUIRED_EXCEPTIONS:
        updated["required"].append("args")

    properties = updated.setdefault("properties", {})
    for key in list(properties):
        if key not in TOP_LEVEL_PROPERTIES:
            del properties[key]
    for key, value in TOP_LEVEL_PROPERTIES.items():
        properties.setdefault(key, copy.deepcopy(value))
    properties["action"] = {"type": "string", "enum": [action]}

    args = properties.setdefault("args", {"type": "object"})
    args["type"] = "object"
    args["required"] = list(spec.get("required_args", []))
    selected_props: dict[str, Any] = {}
    for key in sorted(allowed_args_for_spec(spec)):
        if key not in arg_schemas:
            raise ValueError(f"{action}: missing schema template for args.{key}")
        selected_props[key] = copy.deepcopy(arg_schemas[key])
        if key in PARAM_DESCRIPTIONS:
            selected_props[key].setdefault("description", PARAM_DESCRIPTIONS[key])
    args["properties"] = selected_props
    args["additionalProperties"] = False
    updated["additionalProperties"] = False
    return updated


def sync(check: bool) -> list[str]:
    specs = [spec for spec in action_specs() if spec.get("status") != "removed"]
    arg_schemas = collect_arg_schemas(specs)
    errors: list[str] = []

    for spec in specs:
        path = XDEBUG_ROOT / spec["schemas"]["request"]
        schema = load_json(path)
        try:
            updated = sync_schema(schema, spec, arg_schemas)
        except ValueError as exc:
            errors.append(str(exc))
            continue
        if schema != updated:
            if check:
                errors.append(f"{spec['schemas']['request']}: runtime request schema is not synced")
            else:
                path.write_text(dump_json(updated), encoding="utf-8")
    return errors


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="only check, do not update files")
    args = parser.parse_args(argv)

    errors = sync(check=args.check)
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print("runtime request schemas are synced")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
