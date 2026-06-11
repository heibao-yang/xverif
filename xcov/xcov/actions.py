from __future__ import annotations

from collections import defaultdict
from typing import Any, Dict, List

from .backend import METRICS
from .errors import XcovError, error_response
from .protocol import ok_response
from .query import (apply_output, filter_items, filters_summary, query_args,
                    sort_items, coverage_pct)
from .session import SessionManager

Json = Dict[str, Any]

P0_ACTIONS = [
    "session.open", "session.status", "session.close",
    "tests.list", "metrics.list",
    "scope.summary", "scope.children", "scope.search",
    "cov.summary", "cov.holes", "cov.object.get", "cov.object.search",
    "functional.summary", "functional.holes",
    "source.map",
    "export.summary", "export.holes", "export.scope_tree", "export.functional",
]


class Dispatcher:
    def __init__(self, sessions: SessionManager | None = None) -> None:
        self.sessions = sessions or SessionManager()

    def dispatch(self, req: Json) -> Json:
        try:
            action = req["action"]
            if action == "actions":
                return self._actions(req)
            if action == "schema":
                return self._schema(req)
            if action == "session.open":
                return self._session_open(req)
            if action == "session.status":
                return self._session_status(req)
            if action == "session.close":
                return self._session_close(req)
            sess = self._session(req)
            if action == "tests.list":
                return self._tests_list(req, sess)
            if action == "metrics.list":
                return self._metrics_list(req, sess)
            if action in ("scope.summary", "scope.children", "scope.search"):
                return self._scope(req, sess)
            if action in ("cov.summary", "cov.holes", "cov.object.search", "cov.object.get"):
                return self._cov(req, sess)
            if action in ("functional.summary", "functional.holes"):
                return self._functional(req, sess)
            if action == "source.map":
                return self._source_map(req, sess)
            if action.startswith("export."):
                return self._export(req, sess)
            raise XcovError("ACTION_NOT_FOUND", "unknown action", action=action)
        except XcovError as exc:
            return error_response(req.get("action", ""), req.get("request_id", "req-unknown"),
                                  exc.code, exc.message, **exc.detail)
        except Exception as exc:
            return error_response(req.get("action", ""), req.get("request_id", "req-unknown"),
                                  "INTERNAL_ERROR", str(exc))

    def _session(self, req: Json):
        sid = req.get("target", {}).get("session_id")
        if not sid:
            raise XcovError("SESSION_NOT_FOUND", "target.session_id is required")
        return self.sessions.get(str(sid))

    def _actions(self, req: Json) -> Json:
        rows = [{"name": a, "status": "p0", "api_version": "xcov.v1"} for a in P0_ACTIONS]
        rows += [{"name": a, "status": "p1"} for a in
                 ["index.build", "index.status", "exclude.summary", "assert.report"]]
        rows += [{"name": a, "status": "p2"} for a in
                 ["compare.tests", "compare.vdb", "exclude.load", "exclude.save"]]
        return ok_response(req, {"matched_count": len(rows), "returned": len(rows),
                                 "truncated": False, "output_path": None},
                           {"items": rows})

    def _schema(self, req: Json) -> Json:
        action = req.get("args", {}).get("action")
        if action not in P0_ACTIONS and action not in ("actions", "schema"):
            raise XcovError("ACTION_NOT_FOUND", "schema action not found", action=action)
        schema = {
            "type": "object",
            "required": ["api_version", "action"],
            "properties": {
                "api_version": {"const": "xcov.v1"},
                "action": {"const": action},
                "target": {"type": "object"},
                "args": {"type": "object"},
            },
        }
        return ok_response(req, {"matched_count": 1, "returned": 1,
                                 "truncated": False, "output_path": None},
                           {"schema": schema})

    def _session_open(self, req: Json) -> Json:
        target = req.get("target", {})
        args = req.get("args", {})
        vdb = target.get("vdb")
        if not vdb:
            raise XcovError("VDB_OPEN_FAILED", "target.vdb is required")
        sess = self.sessions.open(
            str(vdb), name=args.get("name"), fake=bool(args.get("fake")),
            reuse=bool(args.get("reuse", True)), reopen=bool(args.get("reopen", False)))
        summary = sess.public_json()
        summary.update({"matched_count": 1, "returned": 1,
                        "truncated": False, "output_path": None})
        return ok_response(req, summary, {"session": sess.public_json()})

    def _session_status(self, req: Json) -> Json:
        sess = self._session(req)
        summary = sess.public_json()
        summary.update({"cached_indexes": "lazy", "matched_count": 1, "returned": 1,
                        "truncated": False, "output_path": None})
        return ok_response(req, summary, {"session": sess.public_json()})

    def _session_close(self, req: Json) -> Json:
        sid = req.get("target", {}).get("session_id")
        sess = self.sessions.close(str(sid))
        summary = sess.public_json()
        summary.update({"matched_count": 1, "returned": 1,
                        "truncated": False, "output_path": None})
        return ok_response(req, summary, {"session": sess.public_json()})

    def _tests_list(self, req: Json, sess) -> Json:
        args = req.get("args", {})
        query = query_args(args)
        rows = filter_items(sess.backend.tests(), query)
        summary, inline, warnings = apply_output("tests.list", args, rows)
        summary["session_id"] = sess.session_id
        return ok_response(req, summary, {"filters": filters_summary(query), "items": inline}, warnings)

    def _metrics_list(self, req: Json, sess) -> Json:
        args = req.get("args", {})
        rows = sess.backend.metrics_for_scope(args.get("scope"), str(args.get("test", "merged")))
        summary, inline, warnings = apply_output("metrics.list", args, rows)
        summary.update({"session_id": sess.session_id, "scope": args.get("scope"),
                        "test": args.get("test", "merged")})
        return ok_response(req, summary, {"items": inline}, warnings)

    def _scope(self, req: Json, sess) -> Json:
        action = req["action"]
        args = req.get("args", {})
        query = query_args(args)
        scopes = sess.backend.scopes()
        if args.get("scope"):
            scope = str(args["scope"])
            scopes = [s for s in scopes if str(s.get("full_name", "")).startswith(scope)]
        rows = filter_items(scopes, query)
        if action in ("scope.summary", "scope.children"):
            metrics = args.get("metrics") or METRICS
            enriched = []
            for row in rows:
                full = row.get("full_name")
                mrows = sess.backend.metrics_for_scope(full, str(args.get("test", "merged")))
                sums = _summary_from_items([m for m in mrows if m.get("metric") in metrics], "metric")
                total_coverable = sum(int(m.get("coverable") or 0) for m in mrows)
                total_covered = sum(int(m.get("covered") or 0) for m in mrows)
                enriched.append({**row, "covered": total_covered,
                                 "coverable": total_coverable,
                                 "missing": total_coverable - total_covered,
                                 "coverage_pct": coverage_pct(total_covered, total_coverable),
                                 "metrics": sums})
            rows = enriched
        rows = sort_items(rows, args.get("sort"))
        summary, inline, warnings = apply_output(action, args, rows)
        summary.update({"session_id": sess.session_id, "scope": args.get("scope"),
                        "test": args.get("test", "merged")})
        return ok_response(req, summary, {"filters": filters_summary(query), "items": inline}, warnings)

    def _cov(self, req: Json, sess) -> Json:
        action = req["action"]
        args = req.get("args", {})
        query = query_args(args)
        metrics = args.get("metrics")
        if action == "cov.object.get":
            name = args.get("name")
            rows = [r for r in sess.backend.items(scope=args.get("scope"),
                                                  test=str(args.get("test", "merged")))
                    if r.get("full_name") == name or r.get("name") == name]
            if not rows:
                raise XcovError("OBJECT_NOT_FOUND", "coverage object not found", name=name)
        else:
            rows = sess.backend.items(metrics=metrics, scope=args.get("scope"),
                                      test=str(args.get("test", "merged")))
            if action == "cov.holes":
                rows = [r for r in rows if int(r.get("missing") or 0) > 0]
            if action == "cov.summary":
                rows = _summary_from_items(rows, str(args.get("group_by", "metric")))
            rows = filter_items(rows, query)
        rows = sort_items(rows, args.get("sort"))
        summary, inline, warnings = apply_output(action, args, rows)
        summary.update({"session_id": sess.session_id, "scope": args.get("scope"),
                        "test": args.get("test", "merged"), "metrics": metrics or METRICS})
        return ok_response(req, summary, {"filters": filters_summary(query), "items": inline}, warnings)

    def _functional(self, req: Json, sess) -> Json:
        action = req["action"]
        args = req.get("args", {})
        query = query_args(args)
        rows = sess.backend.items(metrics=["functional"], scope=args.get("scope"),
                                  test=str(args.get("test", "merged")),
                                  functional_only=True)
        if action == "functional.holes":
            rows = [r for r in rows if int(r.get("missing") or 0) > 0]
        else:
            rows = _summary_from_items(rows, str(args.get("group_by", "covergroup")))
        rows = filter_items(rows, query)
        rows = sort_items(rows, args.get("sort"))
        summary, inline, warnings = apply_output(action, args, rows)
        summary.update({"session_id": sess.session_id, "test": args.get("test", "merged")})
        return ok_response(req, summary, {"filters": filters_summary(query), "items": inline}, warnings)

    def _source_map(self, req: Json, sess) -> Json:
        args = req.get("args", {})
        query = query_args(args)
        file_name = args.get("file")
        line = args.get("line")
        window = int(args.get("window", 0))
        if file_name is None or line is None:
            raise XcovError("SCHEMA_INVALID", "source.map requires file and line")
        metrics = args.get("metrics")
        lo, hi = int(line) - window, int(line) + window
        rows = []
        for item in sess.backend.items(metrics=metrics, test=str(args.get("test", "merged"))):
            ev = item.get("evidence") if isinstance(item.get("evidence"), dict) else {}
            if str(ev.get("file", "")).endswith(str(file_name)) and ev.get("line") is not None:
                try:
                    if lo <= int(ev["line"]) <= hi:
                        rows.append(item)
                except Exception:
                    pass
        rows = filter_items(rows, query)
        summary, inline, warnings = apply_output("source.map", args, rows)
        summary.update({"session_id": sess.session_id, "file": file_name, "line": line,
                        "window": window})
        return ok_response(req, summary, {"filters": filters_summary(query), "items": inline}, warnings)

    def _export(self, req: Json, sess) -> Json:
        action = req["action"]
        args = dict(req.get("args", {}))
        output = dict(args.get("output") or {})
        output.setdefault("mode", "file")
        args["output"] = output
        if action == "export.summary":
            rows = _summary_from_items(sess.backend.items(metrics=args.get("metrics"),
                                                          scope=args.get("scope"),
                                                          test=str(args.get("test", "merged"))),
                                       str(args.get("group_by", "scope")))
        elif action == "export.holes":
            rows = [r for r in sess.backend.items(metrics=args.get("metrics"),
                                                  scope=args.get("scope"),
                                                  test=str(args.get("test", "merged")))
                    if int(r.get("missing") or 0) > 0]
        elif action == "export.scope_tree":
            rows = sess.backend.scopes()
        elif action == "export.functional":
            rows = sess.backend.items(metrics=["functional"], scope=args.get("scope"),
                                      test=str(args.get("test", "merged")),
                                      functional_only=True)
            if args.get("mode") == "holes":
                rows = [r for r in rows if int(r.get("missing") or 0) > 0]
        else:
            raise XcovError("ACTION_NOT_FOUND", "unknown export action", action=action)
        query = query_args(args)
        rows = filter_items(rows, query)
        rows = sort_items(rows, args.get("sort"))
        summary, inline, warnings = apply_output(action, args, rows, default_mode="file")
        summary["session_id"] = sess.session_id
        return ok_response(req, summary, {"filters": filters_summary(query), "items": inline}, warnings)


def _summary_from_items(items: List[Json], group_by: str) -> List[Json]:
    groups: Dict[str, List[Json]] = defaultdict(list)
    for item in items:
        if group_by == "source_file":
            ev = item.get("evidence") if isinstance(item.get("evidence"), dict) else {}
            key = str(ev.get("file") or "<unknown>")
        else:
            key = str(item.get(group_by) or item.get("metric") or "<unknown>")
        groups[key].append(item)
    rows: List[Json] = []
    for key, subset in groups.items():
        coverable = sum(int(i.get("coverable") or 0) for i in subset)
        covered = sum(int(i.get("covered") or 0) for i in subset)
        rows.append({group_by: key, "covered": covered, "coverable": coverable,
                     "missing": coverable - covered,
                     "coverage_pct": coverage_pct(covered, coverable),
                     "metric": key if group_by == "metric" else "summary",
                     "name": key, "full_name": key})
    return rows
