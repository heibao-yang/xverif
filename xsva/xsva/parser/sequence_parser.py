"""Sequence 解析器 — 处理 SVA sequence 表达式的内部结构。

处理：##N, ##[m:n], ##[m:$], [*N], [*m:n], [=m:n], [->m:n],
      first_match(...), throughout, intersect, within,
      (v = expr) local variable assignment.
"""

from __future__ import annotations

from xsva.ir.diagnostics import DiagnosticBag
from xsva.ir.sequence import AssignActionIR, SeqNode, SeqNodeKind, DelayRange

from .scanner import Scanner, TokenKind
from .expr_parser import ExprParser


class SequenceParser:
    """解析 SVA sequence 表达式。"""

    def __init__(self, scanner: Scanner, diag: DiagnosticBag) -> None:
        self._scanner = scanner
        self._diag = diag

    def parse_sequence(self) -> list[SeqNode]:
        """解析一个 sequence 表达式，返回扁平的 SeqNode 列表。

        顶层可能是：
        - 单个信号/表达式
        - 带延迟的 sequence:  expr ##N expr
        - 带 repeat 的 sequence
        - first_match(...)
        - (v = expr) capture sequence
        """
        nodes: list[SeqNode] = []

        while True:
            tk = self._scanner.peek()

            if tk.kind == TokenKind.EOF:
                break

            # 终止条件
            if tk.kind in (TokenKind.SEMICOLON, TokenKind.RPAREN,
                           TokenKind.KW_ENDPROPERTY, TokenKind.KW_ENDSEQUENCE,
                           TokenKind.KW_ASSERT, TokenKind.KW_ASSUME, TokenKind.KW_COVER,
                           TokenKind.KW_PROPERTY, TokenKind.KW_SEQUENCE,
                           TokenKind.IMPL_OVERLAPPED, TokenKind.IMPL_NONOVERLAPPED):
                break

            # ##N 或 ##[m:n]
            if tk.kind == TokenKind.HASH_HASH:
                delay_node = self._parse_delay()
                if delay_node:
                    nodes.append(delay_node)
                continue

            # first_match(...)
            if tk.kind == TokenKind.KW_FIRST_MATCH:
                fm_node = self._parse_first_match()
                if fm_node:
                    nodes.append(fm_node)
                continue

            # throughout expr throughout seq
            # Note: "throughout" checks happen during parsing of expr nodes

            # intersect
            if tk.kind == TokenKind.KW_INTERSECT:
                # This appears between two sub-sequences; handle in lowering
                self._diag.warning("XSVA-W006",
                                   "intersect found — marking as partial, "
                                   "intersect lowering is conservative")
                self._scanner.advance()
                # Consume the right-hand sequence as raw
                right_nodes = self.parse_sequence()
                if nodes and right_nodes:
                    left = SeqNode.sequence(nodes) if len(nodes) > 1 else nodes[0]
                    right = SeqNode.sequence(right_nodes) if len(right_nodes) > 1 else right_nodes[0]
                    nodes = [SeqNode.intersect(left, right)]
                    nodes[-1].lowering_status = "partial"
                continue

            # (v = expr) local variable capture
            if tk.kind == TokenKind.LPAREN:
                match_node = self._parse_parenthesized_match_item()
                if match_node:
                    nodes.append(match_node)
                    continue

                node = self._parse_expr_node()
                if node:
                    nodes.append(node)
                continue

            # Default: expression match
            node = self._parse_expr_node()
            if node:
                nodes.append(node)
            else:
                break

        return nodes

    def _parse_parenthesized_match_item(self) -> SeqNode | None:
        """解析 `(guard, v = expr, ...)` 或 `(v = expr)` match item。"""
        saved = (self._scanner._pos, self._scanner._line, self._scanner._col)
        self._scanner.expect(TokenKind.LPAREN)

        inner_tokens = []
        depth = 1
        while depth > 0:
            tk = self._scanner.peek()
            if tk.kind == TokenKind.EOF:
                self._scanner._pos, self._scanner._line, self._scanner._col = saved
                return None
            tk = self._scanner.advance()
            if tk.kind == TokenKind.LPAREN:
                depth += 1
            elif tk.kind == TokenKind.RPAREN:
                depth -= 1
                if depth == 0:
                    break
            inner_tokens.append(tk)

        parts = self._split_top_level_commas(inner_tokens)
        actions: list[AssignActionIR] = []
        guard_parts = []

        for part in parts:
            eq_idx = self._assignment_index(part)
            if eq_idx == 1 and part[0].kind == TokenKind.IDENT:
                lhs = part[0].text
                rhs_raw = self._reconstruct_raw(part[eq_idx + 1:])
                action_kind = "update" if lhs in {tk.text for tk in part[eq_idx + 1:]
                                                   if tk.kind == TokenKind.IDENT} else "capture"
                actions.append(AssignActionIR(lhs=lhs, rhs=rhs_raw, action_kind=action_kind))
            else:
                guard_parts.extend(part)

        if not actions:
            self._scanner._pos, self._scanner._line, self._scanner._col = saved
            return None

        guard_raw = self._reconstruct_raw(guard_parts).strip()
        guard_expr = ExprParser(Scanner(guard_raw, file="<match_guard>")).parse_expr() if guard_raw else None
        if guard_expr is None:
            guard_expr = ExprParser(Scanner("1", file="<match_guard>")).parse_expr()

        return SeqNode.match_item(guard_expr, actions, raw=self._reconstruct_raw(inner_tokens))

    def _split_top_level_commas(self, tokens) -> list[list]:
        parts: list[list] = [[]]
        paren_depth = 0
        bracket_depth = 0
        for tk in tokens:
            if tk.kind == TokenKind.LPAREN:
                paren_depth += 1
            elif tk.kind == TokenKind.RPAREN and paren_depth > 0:
                paren_depth -= 1
            elif tk.kind == TokenKind.LBRACKET:
                bracket_depth += 1
            elif tk.kind == TokenKind.RBRACKET and bracket_depth > 0:
                bracket_depth -= 1
            if tk.kind == TokenKind.COMMA and paren_depth == 0 and bracket_depth == 0:
                parts.append([])
            else:
                parts[-1].append(tk)
        return [p for p in parts if p]

    def _assignment_index(self, tokens) -> int:
        paren_depth = 0
        bracket_depth = 0
        for i, tk in enumerate(tokens):
            if tk.kind == TokenKind.LPAREN:
                paren_depth += 1
            elif tk.kind == TokenKind.RPAREN and paren_depth > 0:
                paren_depth -= 1
            elif tk.kind == TokenKind.LBRACKET:
                bracket_depth += 1
            elif tk.kind == TokenKind.RBRACKET and bracket_depth > 0:
                bracket_depth -= 1
            elif tk.kind == TokenKind.EQ and paren_depth == 0 and bracket_depth == 0:
                return i
        return -1

    def _parse_delay(self) -> SeqNode | None:
        """解析 ##N 或 ##[m:n] 或 ##[m:$]"""
        self._scanner.expect(TokenKind.HASH_HASH)

        tk = self._scanner.peek()
        if tk.kind == TokenKind.NUMBER:
            # ##N
            self._scanner.advance()
            try:
                n = int(tk.text)
            except ValueError:
                self._diag.error("SVA-E003", f"invalid delay value: {tk.text}")
                return None
            return SeqNode.delay_cycles(n, None)

        if tk.kind == TokenKind.LBRACKET:
            self._scanner.advance()  # [
            # Read min
            min_tk = self._scanner.advance()
            try:
                min_c = int(min_tk.text)
            except ValueError:
                self._diag.error("SVA-E003", f"invalid delay min: {min_tk.text}")
                return None

            # Optionally : or :max or :$
            tk = self._scanner.peek()
            if tk.kind == TokenKind.COLON:
                self._scanner.advance()
                tk = self._scanner.peek()
                if tk.kind == TokenKind.DOLLAR or tk.text == "$":
                    self._scanner.advance()
                    self._scanner.expect(TokenKind.RBRACKET)
                    return SeqNode.delay_cycles(min_c, None, infinite=True)
                elif tk.kind == TokenKind.NUMBER:
                    self._scanner.advance()
                    try:
                        max_c = int(tk.text)
                    except ValueError:
                        self._diag.error("SVA-E003", f"invalid delay max: {tk.text}")
                        return None
                    self._scanner.expect(TokenKind.RBRACKET)
                    return SeqNode.delay_cycles(min_c, max_c)
                else:
                    self._diag.error("SVA-E003", f"expected number or $ after delay colon, got {tk.kind.value}")
                    return None
            elif tk.kind == TokenKind.RBRACKET:
                self._scanner.advance()
                return SeqNode.delay_cycles(min_c, min_c)  # #[N] same as ##N
            else:
                self._diag.error("SVA-E003", f"unexpected token in delay range: {tk.kind.value}")
                return None

        self._diag.error("SVA-E003", f"expected delay value after ##, got {tk.kind.value}")
        return None

    def _parse_first_match(self) -> SeqNode | None:
        """解析 first_match(seq)"""
        self._scanner.expect(TokenKind.KW_FIRST_MATCH)
        self._scanner.expect(TokenKind.LPAREN)
        inner_nodes = self.parse_sequence()
        self._scanner.expect(TokenKind.RPAREN)
        if inner_nodes:
            inner = SeqNode.sequence(inner_nodes) if len(inner_nodes) > 1 else inner_nodes[0]
            return SeqNode.first_match(inner)
        return None

    def _parse_expr_node(self) -> SeqNode | None:
        """解析一个表达式节点。"""
        expr_parser = ExprParser(self._scanner)
        expr = expr_parser.parse_expr()
        if expr.raw.strip():
            return SeqNode.signal_match(expr)

        # Couldn't parse — advance one token to avoid infinite loop
        tk = self._scanner.peek()
        if tk.kind != TokenKind.EOF:
            self._scanner.advance()
        return None

    def _reconstruct_raw(self, tokens) -> str:
        return " ".join(t.text for t in tokens)
