"""Sequence Graph IR → Obligation Timeline IR lowering（核心）。对齐 spec 第十六~十八章。

处理：
  16.x - 基础 lowering (|->, |=>, ##N, ##[m:n], [*N], $stable, $past, $rose, $fell)
  17.x - local variable lowering (capture in antecedent, multi-var, capture in path, update)
  18.x - 高级 sequence (first_match, intersect, throughout, within, [->N], [=N])
"""

from __future__ import annotations

import copy

from xsva.ir.common import LoweringStatus, Severity, DiagnosticIR, SourceSpan
from xsva.ir.diagnostics import DiagnosticBag
from xsva.ir.expr import ExprIR, SignalRef
from xsva.ir.sequence import SeqNode, SeqNodeKind, SequenceIR, CaptureIR as SeqCaptureIR
from xsva.ir.timeline import (
    CaptureIR,
    FailureConditionIR,
    MatchPathIR,
    ObligationIR,
    ObligationKind,
    TimelineIR,
    TriggerIR,
    WindowIR,
)
from xsva.ir.surface import ClockIR


# ── helpers ──

def _impl_offset(implication: str) -> int:
    """|-> = 0, |=> = 1."""
    if implication == "|=>":
        return 1
    return 0


def _worst_status(a: str, b: str) -> str:
    order = {"exact": 0, "partial": 1, "opaque": 2, "unsupported": 3, "unsafe_to_explain": 4}
    return a if order.get(a, 0) >= order.get(b, 0) else b


def _signal_refs(expr_ir: ExprIR | None) -> list[SignalRef]:
    if expr_ir is None:
        return []
    return list(expr_ir.signals)


# ── main lowering ──

def lower_sequence_to_timeline(
    seq_ir,
    surface_ir=None,
    max_paths: int = 10,
    diag: DiagnosticBag | None = None,
) -> TimelineIR:
    if diag is None:
        diag = DiagnosticBag()

    nodes = _flatten_concat(seq_ir.nodes if hasattr(seq_ir, 'nodes') else seq_ir)
    if not nodes:
        return TimelineIR(property_name=getattr(seq_ir, 'name', ''),
                          lowering_status=LoweringStatus.OPAQUE)

    # 1. 找 implication marker
    impl_idx = -1
    for i, node in enumerate(nodes):
        if node.kind == SeqNodeKind.IMPLICATION:
            impl_idx = i
            break

    # 2. 分 ante / cons
    if impl_idx >= 0:
        trigger_nodes = nodes[:impl_idx]
        offset = 0
        obligation_nodes = nodes[impl_idx + 1:]
    else:
        trigger_nodes = nodes
        obligation_nodes = []
        offset = 0

    # 3. 路径展开
    from .path_expand import expand_paths, expand_first_match
    paths = expand_paths(obligation_nodes, max_paths=max_paths)
    paths = expand_first_match(paths)

    # 4. 提取 trigger
    trigger_expr = ""
    trigger_captures: list[CaptureIR] = []

    for node in trigger_nodes:
        if node.kind in (SeqNodeKind.EXPR, SeqNodeKind.EXPR_MATCH) and node.expr:
            if trigger_expr:
                trigger_expr += " && "
            trigger_expr += node.expr.raw
            for s in node.expr.signals:
                if s.name not in [c.var for c in trigger_captures]:
                    pass  # signals in the trigger expression
        elif node.kind in (SeqNodeKind.MATCH_ITEM,):
            if node.guard_expr and node.guard_expr.raw and node.guard_expr.raw != "1":
                if trigger_expr:
                    trigger_expr += " && "
                trigger_expr += node.guard_expr.raw
            if node.capture_var:
                trigger_captures.append(CaptureIR(
                    var=node.capture_var,
                    value_expr=node.capture_expr.raw if node.capture_expr else "",
                    relative_cycle=0,
                    meaning=f"capture {node.capture_var} at trigger cycle",
                ))
            for action in node.actions:
                trigger_captures.append(CaptureIR(
                    var=action.lhs,
                    value_expr=action.rhs,
                    relative_cycle=0,
                    meaning=f"{action.action_kind} {action.lhs} at trigger cycle",
                ))

    trigger = TriggerIR(cycle=0, expr=trigger_expr, captures=trigger_captures)

    # 5. 评估 lowering status
    overall_status = "exact"
    for node in nodes:
        status_val = node.lowering_status.value if isinstance(node.lowering_status, LoweringStatus) else node.lowering_status
        overall_status = _worst_status(overall_status, status_val)
        if node.kind in (SeqNodeKind.FIRST_MATCH, SeqNodeKind.INTERSECT,
                         SeqNodeKind.THROUGHOUT, SeqNodeKind.WITHIN):
            overall_status = _worst_status(overall_status, "partial")
            diag.warning("XSVA-W006",
                         f"{node.kind.value} lowering is conservative — marking as partial")

    # 6. 构建 obligations + paths
    all_obligations: list[ObligationIR] = []
    match_paths: list[MatchPathIR] = []
    failure_conditions: list[FailureConditionIR] = []

    for pi, path in enumerate(paths):
        path_obligations: list[ObligationIR] = []
        path_captures: list[CaptureIR] = list(trigger_captures)  # inherit trigger captures
        cycle = offset  # start from implication offset

        i = 0
        while i < len(path):
            node = path[i]

            # Range delay + single expr → windowed eventually
            if (node.kind in (SeqNodeKind.DELAY,)
                    and node.delay and node.delay.max_cycles is not None
                    and node.delay.min_cycles != node.delay.max_cycles
                    and i + 1 < len(path)
                    and path[i + 1].kind in (SeqNodeKind.EXPR, SeqNodeKind.EXPR_MATCH)
                    and path[i + 1].expr
                    and (i + 2 >= len(path) or path[i + 2].kind in (SeqNodeKind.DELAY, SeqNodeKind.IMPLICATION))):
                # Merge into single eventually obligation
                next_node = path[i + 1]
                win = WindowIR(start=cycle + node.delay.min_cycles,
                               end=cycle + node.delay.max_cycles,
                               unbounded=False)
                req = f"{next_node.expr.raw} must become true at least once between cycle +{win.start} and +{win.end}"
                fcond = f"{next_node.expr.raw} is never true from cycle +{win.start} to +{win.end}"
                obl = ObligationIR(
                    id=f"ob_{pi}_{i}", kind=ObligationKind.EVENTUALLY,
                    expr=next_node.expr.raw, expr_ir=next_node.expr,
                    has_cycle=False, has_window=True, window=win,
                    signals_to_query=_signal_refs(next_node.expr),
                    description=req, failure_condition=fcond,
                    depends_on_captures=_find_capture_deps(next_node.expr, path_captures),
                )
                path_obligations.append(obl)
                i += 2
                cycle += node.delay.max_cycles
                continue

            # Fixed delay ##N
            if node.kind in (SeqNodeKind.DELAY,) and node.delay:
                if node.delay.max_cycles is None or node.delay.min_cycles == node.delay.max_cycles:
                    cycle += node.delay.min_cycles
                    i += 1
                    continue
                # Otherwise: range delay already handled above
                cycle += node.delay.min_cycles
                i += 1
                continue

            # Expression match
            if node.kind in (SeqNodeKind.EXPR, SeqNodeKind.EXPR_MATCH) and node.expr:
                obl = _expr_to_obligation(node, cycle, pi, i, path_captures)
                if obl:
                    path_obligations.append(obl)
                i += 1
                continue

            # Match item with capture
            if node.kind in (SeqNodeKind.MATCH_ITEM,) and node.capture_var:
                cap_ir = CaptureIR(
                    var=node.capture_var,
                    value_expr=node.capture_expr.raw if node.capture_expr else "",
                    relative_cycle=cycle,
                    meaning=f"capture {node.capture_var} at cycle +{cycle}",
                )
                path_captures.append(cap_ir)
                i += 1
                continue

            if node.kind in (SeqNodeKind.MATCH_ITEM,):
                if node.guard_expr and node.guard_expr.raw and node.guard_expr.raw != "1":
                    guard_node = SeqNode.expr_node(node.guard_expr.raw, node.guard_expr)
                    obl = _expr_to_obligation(guard_node, cycle, pi, i, path_captures)
                    if obl:
                        path_obligations.append(obl)
                for action in node.actions:
                    path_captures.append(CaptureIR(
                        var=action.lhs,
                        value_expr=action.rhs,
                        relative_cycle=cycle,
                        meaning=f"{action.action_kind} {action.lhs} at cycle +{cycle}",
                    ))
                i += 1
                continue

            # Intersect — mark partial
            if node.kind in (SeqNodeKind.INTERSECT,):
                diag.warning("XSVA-W006", "intersect lowering is conservative — marking as partial")
                path_obligations.append(ObligationIR(
                    id=f"ob_{pi}_{i}", kind=ObligationKind.SEQUENCE_PATH,
                    description=f"intersect sequence (partial lowering)", has_cycle=False,
                ))
                overall_status = _worst_status(overall_status, "partial")
                i += 1
                continue

            # First_match — mark and skip (handled by path_expand)
            if node.kind in (SeqNodeKind.FIRST_MATCH,):
                path_obligations.append(ObligationIR(
                    id=f"ob_{pi}_{i}", kind=ObligationKind.SEQUENCE_PATH,
                    description="first_match (earliest satisfaction; conservative lowering)", has_cycle=False,
                ))
                overall_status = _worst_status(overall_status, "partial")
                i += 1
                continue

            # Throughout / Within — mark partial
            if node.kind in (SeqNodeKind.THROUGHOUT, SeqNodeKind.WITHIN):
                diag.warning("XSVA-W006",
                             f"{node.kind.value} lowering is conservative — marking as partial")
                overall_status = _worst_status(overall_status, "partial")
                i += 1
                continue

            # Anything else
            i += 1

        # Collect obligations
        all_obligations.extend(path_obligations)

        # Build MatchPathIR
        path_desc = f"Path {pi}: " + " → ".join(
            f"{ob.description[:50]}" for ob in path_obligations
        ) if path_obligations else f"Path {pi}: (empty)"

        match_paths.append(MatchPathIR(
            id=f"path_{pi}",
            captures=path_captures,
            obligations=tuple(path_obligations),
            pass_condition="all obligations in this path must hold",
            failure_condition="any obligation in this path fails",
            is_partial=(overall_status != "exact"),
            description=path_desc,
        ))

    # 7. Failure conditions (aggregate)
    for ob in all_obligations:
        if ob.failure_condition:
            failure_conditions.append(FailureConditionIR(
                obligation_id=ob.id, condition=ob.failure_condition,
            ))

    # 8. Clock & disable
    clock = ClockIR(edge="posedge", signal="", supported=True)
    disable_expr = ""
    if surface_ir:
        clock = surface_ir.clock or clock
        disable_expr = surface_ir.disable_expr

    # 9. Disable obligation
    disable_obl = None
    if disable_expr:
        disable_obl = ObligationIR(
            id="disable", kind=ObligationKind.POINT,
            description=f"disable iff ({disable_expr}): if true, the current attempt is immediately terminated",
            failure_condition=f"{disable_expr} becomes true during the attempt",
        )

    # 10. Vacuity checks
    vacuity: list[str] = []
    if not disable_expr:
        vacuity.append("XSVA-W001: missing disable iff")
    if trigger_expr in ("0", "1'b0"):
        vacuity.append("XSVA-W002: antecedent is constant false")

    return TimelineIR(
        schema_version="xsva.timeline_ir.v1",
        property_name=seq_ir.name if hasattr(seq_ir, 'name') else "",
        kind=surface_ir.kind if surface_ir else "assert",
        clock=clock,
        disable_expr=disable_expr,
        trigger=trigger,
        obligations=all_obligations,
        match_paths=match_paths,
        failure_conditions=failure_conditions,
        vacuity_checks=vacuity,
        lowering_status=LoweringStatus(overall_status),
        diagnostics=list(diag.diagnostics) if diag else [],
    )
    timeline._disable_obl = disable_obl


def _expr_to_obligation(
    node: SeqNode, cycle: int, pi: int, i: int, captures: list[CaptureIR],
) -> ObligationIR | None:
    """将 Expr/ExprMatch 节点转为对应的 ObligationIR。"""
    expr = node.expr
    if not expr:
        return None

    # Sampled functions
    if expr.sampled_funcs:
        func = expr.sampled_funcs[0]
        if func == "$past":
            return ObligationIR(
                id=f"ob_{pi}_{i}", kind=ObligationKind.COMPARE_PAST,
                expr=expr.raw, expr_ir=expr, has_cycle=True, cycle=cycle,
                description=f"{expr.raw} (compare with value {expr.sample_dependencies[0].depth or 1} cycles earlier)",
                depends_on_captures=_find_capture_deps(expr, captures),
                signals_to_query=_signal_refs(expr),
            )
        elif func == "$rose":
            return ObligationIR(
                id=f"ob_{pi}_{i}", kind=ObligationKind.ROSE,
                expr=expr.raw, expr_ir=expr, has_cycle=True, cycle=cycle,
                description=f"{expr.raw} (rise edge at cycle +{cycle})",
                signals_to_query=_signal_refs(expr),
            )
        elif func == "$fell":
            return ObligationIR(
                id=f"ob_{pi}_{i}", kind=ObligationKind.FELL,
                expr=expr.raw, expr_ir=expr, has_cycle=True, cycle=cycle,
                description=f"{expr.raw} (fall edge at cycle +{cycle})",
                signals_to_query=_signal_refs(expr),
            )
        elif func == "$stable":
            return ObligationIR(
                id=f"ob_{pi}_{i}", kind=ObligationKind.STABLE,
                expr=expr.raw, expr_ir=expr, has_cycle=True, cycle=cycle,
                description=f"{expr.raw} (value equals previous sample)",
                depends_on_captures=_find_capture_deps(expr, captures),
                signals_to_query=_signal_refs(expr),
            )
        elif func == "$changed":
            return ObligationIR(
                id=f"ob_{pi}_{i}", kind=ObligationKind.POINT,
                expr=expr.raw, expr_ir=expr, has_cycle=True, cycle=cycle,
                description=f"{expr.raw} (changed from previous sample)",
                signals_to_query=_signal_refs(expr),
            )

    # Capture reference in obligation
    deps = _find_capture_deps(expr, captures)

    # Hold: B[*3] pattern — handled by lowering caller or separate logic
    # Point: default
    return ObligationIR(
        id=f"ob_{pi}_{i}", kind=ObligationKind.POINT,
        expr=expr.raw, expr_ir=expr, has_cycle=True, cycle=cycle,
        description=f"{expr.raw} must be true at cycle +{cycle}",
        failure_condition=f"{expr.raw} is false at cycle +{cycle}" if cycle >= 0 else None,
        depends_on_captures=deps,
        signals_to_query=_signal_refs(expr),
    )


def _flatten_concat(nodes: list[SeqNode]) -> list[SeqNode]:
    flat: list[SeqNode] = []
    for node in nodes:
        if node.kind == SeqNodeKind.CONCAT:
            flat.extend(_flatten_concat(node.children))
        else:
            flat.append(node)
    return flat


def _find_capture_deps(expr: ExprIR | None, captures: list[CaptureIR]) -> list[str]:
    """找出表达式中引用的 capture variable。"""
    if not expr or not captures:
        return []
    deps: list[str] = []
    for c in captures:
        if c.var in expr.raw:
            deps.append(c.var)
    return deps
