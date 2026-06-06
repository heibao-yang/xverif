"""Vacuity 检查规则。对齐 spec 第十九章。

XSVA-W001: missing disable iff
XSVA-W002: antecedent is constant false
XSVA-W003: antecedent is constant true
"""

from __future__ import annotations

from xsva.ir.common import DiagnosticIR, Severity
from xsva.ir.timeline import TimelineIR


def check(timeline: TimelineIR) -> list[DiagnosticIR]:
    results: list[DiagnosticIR] = []

    # W001: missing disable iff
    if not timeline.disable_expr:
        results.append(DiagnosticIR(
            code="XSVA-W001",
            severity=Severity.WARNING,
            message="Property has no disable iff. Reset behavior may be unclear.",
        ))

    # W002: antecedent constant false
    trigger_expr = timeline.trigger.expr.strip() if timeline.trigger else ""
    if trigger_expr in ("0", "1'b0", "false"):
        results.append(DiagnosticIR(
            code="XSVA-W002",
            severity=Severity.WARNING,
            message="Antecedent is constant false. Assertion may be vacuous.",
        ))

    # W003: antecedent constant true
    if trigger_expr in ("1", "1'b1", "true"):
        results.append(DiagnosticIR(
            code="XSVA-W003",
            severity=Severity.WARNING,
            message="Antecedent is constant true. Property starts a new attempt every cycle.",
        ))

    return results
