from __future__ import annotations

import json
from typing import Any

from .bitvector import BitVector
from .errors import XbitError


SCHEMA_RESULT = "xbit.result.v1"
SCHEMA_ERROR = "xbit.error.v1"


def success(op: str, *, input_value: Any = None, result: Any = None, warnings: list[str] | None = None, **extra) -> dict:
    response = {"ok": True, "schema": SCHEMA_RESULT, "op": op}
    if input_value is not None:
        response["input"] = input_value
    if result is not None:
        response["result"] = result.to_result() if isinstance(result, BitVector) else result
    response.update(extra)
    response["warnings"] = warnings or []
    return response


def failure(error: Exception) -> dict:
    if isinstance(error, XbitError):
        err = error.to_error()
    else:
        err = {"code": "INTERNAL_ERROR", "message": str(error)}
    return {"ok": False, "schema": SCHEMA_ERROR, "error": err}


def dumps(payload: dict, *, pretty: bool = False) -> str:
    return json.dumps(payload, ensure_ascii=False, indent=2 if pretty else None, sort_keys=False)


def human_result(payload: dict) -> str:
    if not payload.get("ok"):
        error = payload.get("error", {})
        return f"error {error.get('code', 'ERROR')}: {error.get('message', '')}"
    result = payload.get("result")
    if isinstance(result, dict) and "sv" in result:
        pieces = [result["sv"], f"width={result['width']}"]
        if result.get("known"):
            pieces.append(f"unsigned={result.get('unsigned')}")
            pieces.append(f"signed={result.get('signed_value')}")
        if "bool" in result:
            pieces.append(f"bool={result['bool']}")
        return " ".join(pieces)
    if result is not None:
        return str(result)
    if "matched" in payload:
        return "matched" if payload["matched"] else "not matched"
    return "ok"
