"""SVG 语义时序图生成器。对齐 spec 第二十二章。

从 TimelineIR 生成 standalone SVG string（零第三方依赖）。
布局：trigger row → capture row → obligation rows → failure row
"""

from __future__ import annotations

from xsva.ir.timeline import TimelineIR

# Layout constants
ROW_HEIGHT = 40
CYCLE_WIDTH = 80
LEFT_MARGIN = 160
TOP_MARGIN = 40
FONT_SIZE = 12
RECT_HEIGHT = 24


def render_timeline_svg(timeline: TimelineIR) -> str:
    """从 TimelineIR 生成 SVG 语义时序图。"""
    # 计算尺寸
    max_cycle = _max_cycle(timeline)
    num_cycles = max_cycle + 2  # +1 for trigger, +1 for padding
    num_rows = 2 + len(timeline.match_paths) + len(timeline.semantic_notes)

    width = LEFT_MARGIN + num_cycles * CYCLE_WIDTH + 20
    height = TOP_MARGIN + num_rows * ROW_HEIGHT + 40

    parts: list[str] = []
    parts.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}">')
    parts.append(f'<style>text {{ font-family: monospace; font-size: {FONT_SIZE}px; }} '
                 f'.label {{ font-weight: bold; fill: #333; }} '
                 f'.cycle-header {{ fill: #666; font-size: 10px; }} '
                 f'.trigger {{ fill: #0066cc; }} '
                 f'.obligation {{ fill: #cc6600; }} '
                 f'.failure {{ fill: #cc0000; }} '
                 f'.window {{ fill: #f0f0f0; stroke: #ccc; }}</style>')

    # Title
    parts.append(f'<text x="{LEFT_MARGIN}" y="20" class="label" font-size="14">'
                 f'{_esc(timeline.property_name)} ({timeline.kind})</text>')

    # Clock row
    parts.append(f'<text x="{LEFT_MARGIN}" y="50" class="label">'
                 f'Clock: @({timeline.clock.edge} {timeline.clock.signal})</text>')

    # Cycle headers
    for c in range(num_cycles):
        x = LEFT_MARGIN + c * CYCLE_WIDTH
        parts.append(f'<text x="{x}" y="{TOP_MARGIN + ROW_HEIGHT - 28}" class="cycle-header">'
                     f'+{c}</text>')

    row_y = TOP_MARGIN + ROW_HEIGHT

    # Trigger row
    trigger_expr = timeline.trigger.expr if timeline.trigger else ""
    parts.append(f'<text x="{LEFT_MARGIN + 10}" y="{row_y + 20}" class="trigger">'
                 f'Trigger: {_esc(trigger_expr)}</text>')
    _draw_circle(parts, LEFT_MARGIN + 0 * CYCLE_WIDTH + CYCLE_WIDTH // 2, row_y + RECT_HEIGHT // 2,
                 "#0066cc", "trigger")

    row_y += ROW_HEIGHT

    for note in timeline.semantic_notes:
        parts.append(f'<text x="{LEFT_MARGIN + 10}" y="{row_y + 20}" class="obligation" font-size="10">'
                     f'Note: {_esc(note.text[:100])}</text>')
        row_y += ROW_HEIGHT

    # Obligation rows (one per path)
    for pi, path in enumerate(timeline.match_paths):
        for ob in path.obligations:
            if ob.window and not ob.window.unbounded:
                # Draw window rect
                x1 = LEFT_MARGIN + ob.window.start * CYCLE_WIDTH + CYCLE_WIDTH // 2
                x2 = LEFT_MARGIN + ob.window.end * CYCLE_WIDTH + CYCLE_WIDTH // 2
                parts.append(f'<rect x="{x1}" y="{row_y + 4}" width="{x2 - x1}" '
                             f'height="{RECT_HEIGHT}" class="window" rx="4"/>')

            # Draw point/circle
            cycle = ob.cycle or (ob.window.start if ob.window else 0)
            _draw_circle(parts,
                         LEFT_MARGIN + cycle * CYCLE_WIDTH + CYCLE_WIDTH // 2,
                         row_y + RECT_HEIGHT // 2,
                         "#cc6600", f"ob_{ob.id}")

            parts.append(f'<text x="{LEFT_MARGIN + 10}" y="{row_y + 20}" class="obligation" font-size="10">'
                         f'[{ob.kind.value}] {_esc(ob.description[:60])}</text>')

        row_y += ROW_HEIGHT

    # Failure row
    if timeline.failure_conditions:
        parts.append(f'<text x="{LEFT_MARGIN + 10}" y="{row_y + 20}" class="failure">'
                     f'Failure: {_esc(str(timeline.failure_conditions[0].condition)[:80])}</text>')

    parts.append('</svg>')
    return "\n".join(parts)


def _draw_circle(parts: list[str], cx: int, cy: int, color: str, label: str) -> None:
    parts.append(f'<circle cx="{cx}" cy="{cy}" r="6" fill="{color}" opacity="0.8">'
                 f'<title>{_esc(label)}</title></circle>')


def _max_cycle(timeline: TimelineIR) -> int:
    """计算最大 cycle 数。"""
    mc = 2
    for path in timeline.match_paths:
        for ob in path.obligations:
            if ob.window and ob.window.end > mc:
                mc = ob.window.end
            if ob.cycle > mc:
                mc = ob.cycle
    return mc


def _esc(text: str) -> str:
    """XML 转义。"""
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")
