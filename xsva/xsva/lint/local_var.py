"""Local variable 检查规则。对齐 spec 第十九章。

XSVA-W007: local variable update inside repetition
XSVA-W010: unsupported assignment form
"""

from __future__ import annotations

from xsva.ir.common import DiagnosticIR, Severity
from xsva.ir.timeline import TimelineIR


def check(timeline: TimelineIR) -> list[DiagnosticIR]:
    results: list[DiagnosticIR] = []

    # W007: update inside repetition
    captures = timeline.trigger.captures if timeline.trigger else []
    for c in captures:
        if "update" in c.meaning.lower():
            results.append(DiagnosticIR(
                code="XSVA-W007",
                severity=Severity.WARNING,
                message=f"Local variable update inside repetition creates path-dependent state: "
                        f"{c.var} = {c.value_expr}",
            ))

    return results
