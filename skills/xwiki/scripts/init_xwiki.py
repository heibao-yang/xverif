#!/usr/bin/env python3
"""Initialize an xwiki directory skeleton."""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from dataclasses import dataclass
from datetime import date
from pathlib import Path
from typing import Iterable

ENV_NAME = "XWIKI_DIR"
@dataclass(frozen=True)
class Page:
    path: str
    object_type: str
    title: str
    description: str
    body: str
    tags: tuple[str, ...]


def _frontmatter(page: Page, today: str) -> str:
    tags = "[" + ", ".join(page.tags) + "]"
    if page.path.endswith("log.md"):
        page_type = "Verification Log"
    elif page.path.startswith("_index/"):
        page_type = "Wiki Index"
    else:
        page_type = "Verification Index"
    return "\n".join(
        [
            "---",
            f"type: {page_type}",
            f"title: {page.title}",
            f"description: {page.description}",
            f"object_type: {page.object_type}",
            f"tags: {tags}",
            f"updated_at: {today}",
            "confidence: high",
            "---",
            "",
            page.body.rstrip(),
            "",
        ]
    )


def _log_body(title: str, today: str) -> str:
    return f"""# {title}

## [{today}] init | xwiki skeleton

Action: initialized xwiki directory skeleton.

Unknowns:
- Project-specific facts have not been ingested yet.
"""


def _pages(today: str) -> list[Page]:
    return [
        Page(
            "index.md",
            "dv",
            "xwiki Root Index",
            "Root index for an xwiki verification memory.",
            """# xwiki Root Index

## Object Directories

- [DE](de/index.md)
- [DV](dv/index.md)
- [DE Issue](de_issue/index.md)
- [DV Issue](dv_issue/index.md)
""",
            ("index", "root"),
        ),
        Page(
            "de/index.md",
            "de",
            "DE Index",
            "Index for design implementation topics.",
            """# DE Index

## Scope

Use this directory for RTL, interfaces, microarchitecture, protocols, parameters and datapath topics.
""",
            ("de", "index"),
        ),
        Page("de/log.md", "de_issue", "DE Log", "Local update log for design topics.", _log_body("DE Log", today), ("de", "log")),
        Page(
            "dv/index.md",
            "dv",
            "DV Index",
            "Index for verification environment topics.",
            """# DV Index

## Scope

Use this directory for testbench, sequence, checker, scoreboard, coverage, tests, debug workflow and simulation entry topics.
""",
            ("dv", "index"),
        ),
        Page("dv/log.md", "dv_issue", "DV Log", "Local update log for verification topics.", _log_body("DV Log", today), ("dv", "log")),
        Page(
            "de_issue/index.md",
            "de_issue",
            "DE Issue Index",
            "Index for design, spec and RTL issues.",
            """# DE Issue Index

## Subdirectories

- [Spec Issues](spec/index.md)
- [RTL Issues](rtl/index.md)
""",
            ("de-issue", "index"),
        ),
        Page("de_issue/log.md", "de_issue", "DE Issue Log", "Local update log for design issue topics.", _log_body("DE Issue Log", today), ("de-issue", "log")),
        Page(
            "de_issue/spec/index.md",
            "de_issue",
            "Spec Issue Index",
            "Index for spec and documentation contract issues.",
            """# Spec Issue Index

## Scope

Use this directory for spec ambiguity, protocol definition gaps, performance requirement ambiguity or spec-versus-RTL/DV expectation conflicts.
""",
            ("de-issue", "spec", "index"),
        ),
        Page("de_issue/spec/log.md", "de_issue", "Spec Issue Log", "Local update log for spec issue topics.", _log_body("Spec Issue Log", today), ("de-issue", "spec", "log")),
        Page(
            "de_issue/rtl/index.md",
            "de_issue",
            "RTL Issue Index",
            "Index for RTL implementation issues.",
            """# RTL Issue Index

## Scope

Use this directory for DUT/RTL implementation, timing, state machine, reset/clock, backpressure, ordering, replacement or data-integrity issues.
""",
            ("de-issue", "rtl", "index"),
        ),
        Page("de_issue/rtl/log.md", "de_issue", "RTL Issue Log", "Local update log for RTL issue topics.", _log_body("RTL Issue Log", today), ("de-issue", "rtl", "log")),
        Page(
            "dv_issue/index.md",
            "dv_issue",
            "DV Issue Index",
            "Index for verification environment issues.",
            """# DV Issue Index

## Scope

Use this directory for testbench, UVM env, RM, checker, scoreboard, sequence, configuration, scripts, simulation parameters or DV assumption issues.
""",
            ("dv-issue", "index"),
        ),
        Page("dv_issue/log.md", "dv_issue", "DV Issue Log", "Local update log for DV issue topics.", _log_body("DV Issue Log", today), ("dv-issue", "log")),
        Page(
            "_index/backlinks.md",
            "dv",
            "Backlinks",
            "Optional backlink index for xwiki pages.",
            """# Backlinks

Update this page when cross-page links are added or moved.
""",
            ("index", "backlinks"),
        ),
        Page(
            "_index/tags.md",
            "dv",
            "Tags",
            "Optional tag index for xwiki pages.",
            """# Tags

Update this page when topic tags are added or changed.
""",
            ("index", "tags"),
        ),
    ]


def _resolve_wiki_dir(raw: str, root: str | None) -> Path:
    path = Path(raw).expanduser()
    if not path.is_absolute() and root:
        path = Path(root).expanduser().resolve() / path
    return path.resolve()


def _planned_writes(wiki_dir: Path, pages: Iterable[Page], force: bool) -> list[tuple[Path, str, str]]:
    today = date.today().isoformat()
    writes: list[tuple[Path, str, str]] = []
    for page in pages:
        path = wiki_dir / page.path
        content = _frontmatter(page, today)
        if path.exists() and not force:
            writes.append((path, "keep", content))
        else:
            writes.append((path, "write", content))
    return writes


def _apply_writes(writes: Iterable[tuple[Path, str, str]]) -> None:
    for path, action, content in writes:
        if action != "write":
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")


def _validate(wiki_dir: Path, quiet: bool = False) -> int:
    script = Path(__file__).resolve().with_name("validate_xwiki.py")
    result = subprocess.run(
        [sys.executable, str(script), "--wiki-dir", str(wiki_dir), "--format", "json"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.stdout and not quiet:
        print(result.stdout, end="")
    if result.stderr and not quiet:
        print(result.stderr, end="", file=sys.stderr)
    return result.returncode


def _emit_json(wiki_dir: Path, writes: list[tuple[Path, str, str]], dry_run: bool, validated: bool | None) -> None:
    payload = {
        "ok": validated is not False,
        "schema_version": "xwiki.init.v1",
        "wiki_dir": str(wiki_dir),
        "dry_run": dry_run,
        "validated": validated,
        "actions": [
            {"action": action, "path": str(path)}
            for path, action, _content in writes
        ],
    }
    print(json.dumps(payload, ensure_ascii=False, indent=2))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Initialize an xwiki directory skeleton")
    parser.add_argument("--wiki-dir", default=os.environ.get(ENV_NAME, ""), help=f"Wiki root. Defaults to ${ENV_NAME}.")
    parser.add_argument("--root", help="Project root for resolving a relative --wiki-dir.")
    parser.add_argument("--force", action="store_true", help="Overwrite scaffold files managed by this script.")
    parser.add_argument("--dry-run", action="store_true", help="Print planned actions without writing files.")
    parser.add_argument("--validate", action="store_true", help="Run validate_xwiki.py after initialization.")
    parser.add_argument("--format", choices=("text", "json"), default="text")
    args = parser.parse_args(argv)

    if not str(args.wiki_dir).strip():
        print(f"error: --wiki-dir is required when {ENV_NAME} is not set", file=sys.stderr)
        return 2

    wiki_dir = _resolve_wiki_dir(args.wiki_dir, args.root)
    pages = _pages(date.today().isoformat())
    writes = _planned_writes(wiki_dir, pages, args.force)

    if not args.dry_run:
        _apply_writes(writes)

    validated: bool | None = None
    validate_rc = 0
    if args.validate and not args.dry_run:
        validate_rc = _validate(wiki_dir, quiet=args.format == "json")
        validated = validate_rc == 0

    if args.format == "json":
        _emit_json(wiki_dir, writes, args.dry_run, validated)
    else:
        for path, action, _content in writes:
            print(f"{action}: {path}")

    return validate_rc


if __name__ == "__main__":
    raise SystemExit(main())
