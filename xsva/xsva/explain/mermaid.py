"""Mermaid 可视化生成器。对齐 spec 第二十一章。

从 TimelineIR 生成 Mermaid flowchart。
"""

from __future__ import annotations

from xsva.ir.timeline import TimelineIR


def render_timeline_mermaid(timeline: TimelineIR) -> str:
    """从 TimelineIR 生成 Mermaid flowchart。"""
    lines: list[str] = []
    lines.append("```mermaid")
    lines.append("flowchart LR")

    # Trigger node
    trigger_label = f"cycle 0: {timeline.trigger.expr}" if timeline.trigger else "trigger"
    lines.append(f'  T["{_escape(trigger_label)}"]')

    if len(timeline.match_paths) == 1 and timeline.match_paths[0].obligations:
        _render_single_path(lines, timeline)
    elif len(timeline.match_paths) > 1:
        _render_multi_path(lines, timeline)
    elif timeline.semantic_notes:
        for i, note in enumerate(timeline.semantic_notes):
            prev = "T" if i == 0 else f"N{i - 1}"
            lines.append(f'  {prev} --> N{i}["{_escape(note.text)}"]')
    else:
        lines.append(f'  T --> N["(no obligations)"]')

    lines.append("```")
    return "\n".join(lines)


def _render_single_path(lines: list[str], timeline: TimelineIR) -> None:
    """渲染单路径 obligation(s)。"""
    path = timeline.match_paths[0]
    prev = "T"
    for i, ob in enumerate(path.obligations):
        node_id = f"O{i}"
        label = ob.description or f"[{ob.kind.value}]"
        if ob.window and not ob.window.unbounded:
            label += f" (+{ob.window.start}..+{ob.window.end})"
        lines.append(f'  {prev} --> {node_id}["{_escape(label)}"]')
        prev = node_id

    if path.pass_condition:
        lines.append(f'  {prev} --> PASS["{_escape(path.pass_condition)}"]')


def _render_multi_path(lines: list[str], timeline: TimelineIR) -> None:
    """渲染多路径。"""
    for pi, path in enumerate(timeline.match_paths):
        prev = "T"
        for j, ob in enumerate(path.obligations):
            node_id = f"P{pi}O{j}"
            label = ob.description or f"[{ob.kind.value}]"
            if ob.window and not ob.window.unbounded:
                label += f" (+{ob.window.start}..+{ob.window.end})"
            lines.append(f'  {prev} --> {node_id}["{_escape(label)}"]')
            prev = node_id

    # 汇总 PASS 节点
    last_nodes = [f"P{p}O{len(path.obligations) - 1}" for p, path in enumerate(timeline.match_paths) if path.obligations]
    if last_nodes:
        lines.append(f'  PASS["PASS if any path matches"]')
        for n in last_nodes:
            lines.append(f'  {n} --> PASS')


def _escape(text: str) -> str:
    """转义 Mermaid 特殊字符。"""
    return text.replace('"', '\\"')
