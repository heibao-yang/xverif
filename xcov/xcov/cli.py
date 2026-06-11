from __future__ import annotations

import argparse
import json
import os
import sys
from typing import Any, Dict

from .actions import Dispatcher
from .errors import XcovError, error_response
from .protocol import json_dumps, parse_request, render_xout, response_format

Json = Dict[str, Any]

_PROTOCOL_OUT = None


def _setup_protocol_stdout() -> None:
    global _PROTOCOL_OUT
    if _PROTOCOL_OUT is not None:
        return
    saved = os.dup(1)
    _PROTOCOL_OUT = os.fdopen(saved, "w", buffering=1, encoding="utf-8")
    os.dup2(2, 1)


def _protocol_write(text: str) -> None:
    out = _PROTOCOL_OUT or sys.stdout
    out.write(text)
    out.flush()


def _emit(req: Json, rsp: Json) -> None:
    if response_format(req) == "json":
        _protocol_write(json_dumps(rsp) + "\n")
    else:
        _protocol_write(render_xout(rsp))


def run_once(text: str, dispatcher: Dispatcher) -> int:
    try:
        req = parse_request(text)
    except XcovError as exc:
        req = {"request_id": "req-unknown", "action": ""}
        rsp = error_response("", "req-unknown", exc.code, exc.message, **exc.detail)
        _emit(req, rsp)
        return 1
    rsp = dispatcher.dispatch(req)
    _emit(req, rsp)
    return 0 if rsp.get("ok") else 1


def stdio_loop(dispatcher: Dispatcher) -> int:
    ready = {"type": "ready", "protocol": "xcov-stdio-loop", "version": 1,
             "pid": os.getpid()}
    _protocol_write(json.dumps(ready, separators=(",", ":")) + "\n")
    seq = 0
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        seq += 1
        try:
            req = parse_request(line)
            rid = req.get("request_id") or req.get("id") or f"req-{seq}"
            req["request_id"] = rid
            if req.get("action") == "stdio.quit":
                _protocol_write(json.dumps({"id": rid, "ok": True, "payload_format": "json",
                                            "json": {"ok": True, "action": "stdio.quit"}}) + "\n")
                return 0
            rsp = dispatcher.dispatch(req)
        except XcovError as exc:
            rid = f"req-{seq}"
            req = {"request_id": rid, "action": ""}
            rsp = error_response("", rid, exc.code, exc.message, **exc.detail)
        xout = render_xout(rsp)
        envelope = {"id": req.get("request_id", f"req-{seq}"),
                    "ok": bool(rsp.get("ok")),
                    "payload_format": "json" if response_format(req) == "json" else "xout",
                    "json": rsp,
                    "xout": xout}
        _protocol_write(json.dumps(envelope, ensure_ascii=False, separators=(",", ":")) + "\n")
    return 0


def main(argv: list[str] | None = None) -> int:
    _setup_protocol_stdout()
    parser = argparse.ArgumentParser(prog="xcov")
    parser.add_argument("--stdio-loop", action="store_true")
    parser.add_argument("--once", action="store_true")
    parser.add_argument("--request")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("file", nargs="?")
    ns = parser.parse_args(argv)
    dispatcher = Dispatcher()
    if ns.stdio_loop:
        return stdio_loop(dispatcher)
    if ns.request:
        with open(ns.request, "r", encoding="utf-8") as fh:
            text = fh.read()
    elif ns.file and ns.file != "-":
        with open(ns.file, "r", encoding="utf-8") as fh:
            text = fh.read()
    else:
        text = sys.stdin.read()
    if ns.json:
        try:
            obj = json.loads(text)
            obj.setdefault("output", {})["format"] = "json"
            text = json.dumps(obj)
        except Exception:
            pass
    return run_once(text, dispatcher)


if __name__ == "__main__":
    raise SystemExit(main())
