from __future__ import annotations

import re
from pathlib import Path

from testinfra.xverif_test.catalog import Catalog


ROOT = Path(__file__).resolve().parents[2]
FORBIDDEN_TARGET = re.compile(
    r"^(?:test|full-test|unit-test|smoke|vim-test|pytest-[A-Za-z0-9_.-]+|mcp-[A-Za-z0-9_.-]+|[A-Za-z0-9_.-]+-test):"
)


def _catalog() -> Catalog:
    return Catalog.load(
        ROOT / "testinfra/catalog.v1.yaml",
        ROOT / "testinfra/schemas/catalog.v1.schema.json",
    )


def test_every_collected_python_test_has_catalog_owner() -> None:
    catalog = _catalog()
    leaf_paths = {
        Path(value)
        for suite in catalog.suites
        for value in suite.runner.get("leaf_paths", [])
    }
    missing: list[str] = []
    for path in sorted(ROOT.rglob("test_*.py")):
        relative = path.relative_to(ROOT)
        if any(part in {".xverif-test-cache", ".xverif-test-results"} for part in relative.parts):
            continue
        if not catalog.owners_for_path(path, ROOT) and relative not in leaf_paths:
            missing.append(relative.as_posix())
    assert missing == []


def test_legacy_pytest_configs_and_gate_scripts_are_absent() -> None:
    assert list(ROOT.rglob("pytest.ini")) == []
    assert not (ROOT / "regression/run_xdebug_regression.sh").exists()
    assert not (ROOT / "regression/run_full_regression.sh").exists()


def test_makefiles_do_not_reintroduce_public_test_targets() -> None:
    violations: list[str] = []
    for path in sorted(ROOT.rglob("Makefile*")):
        relative = path.relative_to(ROOT)
        if any(part in {".xverif-test-cache", ".xverif-test-results", "out", "csrc"} for part in relative.parts):
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            if FORBIDDEN_TARGET.match(line):
                violations.append(f"{relative}:{number}:{line}")
    assert violations == []


def test_active_trace_cases_use_the_shared_builder_profile() -> None:
    root = ROOT / "xdebug/tests/active_trace_chain"
    assert list(root.glob("p0_composability/*/Makefile")) == []
    assert list(root.glob("composite/*/Makefile")) == []
    assert list(root.glob("timing/*/Makefile")) == []
    assert list(root.glob("phase4/*/Makefile")) == []
    assert not (root / "phase5/Makefile").exists()


def test_product_test_consumers_do_not_prepare_fixtures() -> None:
    forbidden = ('["make", "clean"]', '["make", "run"]', '["make", "fixture"]', "build_p3_db")
    violations: list[str] = []
    for path in sorted((ROOT / "xdebug/tests").rglob("*")):
        if path.suffix not in {".py", ".sh"}:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for token in forbidden:
            if token in text:
                violations.append(f"{path.relative_to(ROOT)}:{token}")
    assert violations == []
