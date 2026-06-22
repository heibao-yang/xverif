"""Design-side pynpi language model helpers."""

from __future__ import annotations

from typing import Any, Dict, List


Json = Dict[str, Any]


def _lang():
    from pynpi import lang  # type: ignore

    return lang


def handle_name(handle: Any) -> str:
    lang = _lang()
    try:
        return lang.get_hdl_info(handle, True, False) or ""
    except Exception:
        return ""


def statement_row(stmt: Any) -> Json:
    lang = _lang()
    use_hdl = stmt.get_use_hdl()
    sigs = stmt.get_sig_hdl_list()
    return {
        "use": lang.get_hdl_info(use_hdl, True, False),
        "is_pass_through": stmt.get_is_pass_thr(),
        "num_signal_uses": stmt.get_num_sig_use(),
        "scope": lang.get_hdl_info(stmt.get_scope_hdl(), True, False),
        "source": lang.get_hdl_info(stmt.get_src_hdl(), True, False),
        "signals": [lang.get_hdl_info(sig, True, False) for sig in sigs],
    }


def trace_driver(signal: str, include_control: bool = True) -> List[Json]:
    lang = _lang()
    opt = lang.TrcOption(report_control=include_control)
    return [statement_row(stmt) for stmt in lang.trace_driver2(signal, trc_opt=opt)]


def trace_load(signal: str, include_control: bool = True) -> List[Json]:
    lang = _lang()
    opt = lang.TrcOption(report_control=include_control)
    return [statement_row(stmt) for stmt in lang.trace_load2(signal, trc_opt=opt)]
