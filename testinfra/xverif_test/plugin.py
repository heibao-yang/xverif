from __future__ import annotations

from pathlib import Path
from typing import Any

import pytest

from .catalog import Catalog, CatalogError
from .gates import ExecutionPlan, build_plan


DEFAULT_CATALOG = Path("testinfra/catalog.v1.yaml")
DEFAULT_SCHEMA = Path("testinfra/schemas/catalog.v1.schema.json")


def pytest_addoption(parser: pytest.Parser) -> None:
    group = parser.getgroup("xverif", "catalog-driven xverif test orchestration")
    group.addoption("--xverif-gate", choices=("fast", "regression", "nightly"))
    group.addoption("--xverif-plan", action="store_true", default=False)
    group.addoption("--xverif-catalog", default=str(DEFAULT_CATALOG))
    group.addoption("--xverif-prepare", action="append", default=[])
    group.addoption("--xverif-fixture-validation", action="store_true", default=False)
    group.addoption("--xverif-all-fixtures", action="store_true", default=False)
    group.addoption("--xverif-changed")
    group.addoption("--rerun-failed", dest="xverif_rerun_failed")
    group.addoption("--xverif-fixture-clean", action="store_true", default=False)
    group.addoption("--xverif-results-clean", action="store_true", default=False)


def _repo_root(config: pytest.Config) -> Path:
    return Path(str(config.rootpath)).resolve()


def _load_catalog(config: pytest.Config) -> Catalog:
    root = _repo_root(config)
    configured = Path(config.getoption("--xverif-catalog"))
    catalog_path = configured if configured.is_absolute() else root / configured
    schema_path = root / DEFAULT_SCHEMA
    return Catalog.load(catalog_path, schema_path)


def _requested_operation(config: pytest.Config) -> str | None:
    operations: list[str] = []
    if config.getoption("--xverif-gate"):
        operations.append("gate")
    if config.getoption("--xverif-prepare"):
        operations.append("prepare")
    if config.getoption("--xverif-fixture-validation"):
        operations.append("fixture-validation")
    if config.getoption("xverif_rerun_failed"):
        operations.append("rerun-failed")
    if config.getoption("--xverif-fixture-clean"):
        operations.append("fixture-clean")
    if config.getoption("--xverif-results-clean"):
        operations.append("results-clean")
    if len(operations) > 1:
        raise pytest.UsageError(
            "xverif operations are mutually exclusive: " + ", ".join(operations)
        )
    return operations[0] if operations else None


def _ensure_xverif_state(config: pytest.Config) -> str | None:
    existing = getattr(config, "_xverif_operation", None)
    if existing is not None:
        return existing
    operation = _requested_operation(config)
    if operation is None:
        return None
    try:
        catalog = _load_catalog(config)
    except CatalogError as exc:
        raise pytest.UsageError(str(exc)) from exc
    config._xverif_catalog = catalog  # type: ignore[attr-defined]
    if operation == "gate":
        gate = config.getoption("--xverif-gate")
        config._xverif_plan = build_plan(catalog, gate)  # type: ignore[attr-defined]
    elif config.getoption("--xverif-plan"):
        raise pytest.UsageError("--xverif-plan requires --xverif-gate")
    config._xverif_operation = operation  # type: ignore[attr-defined]
    return operation


def pytest_configure(config: pytest.Config) -> None:
    operation = _ensure_xverif_state(config)
    if operation is None:
        raise pytest.UsageError(
            "bare pytest is not an xverif test operation; use --xverif-gate "
            "fast|regression|nightly, --xverif-plan with a gate, or an explicit "
            "fixture/results operation"
        )


@pytest.hookimpl(tryfirst=True)
def pytest_cmdline_main(config: pytest.Config) -> int | None:
    operation = _ensure_xverif_state(config)
    if operation == "gate" and config.getoption("--xverif-plan"):
        plan: ExecutionPlan = config._xverif_plan  # type: ignore[attr-defined]
        print(plan.render())
        return pytest.ExitCode.OK
    if operation is None:
        return None
    if operation != "gate":
        raise pytest.UsageError(
            f"xverif operation {operation!r} is declared but not implemented in this stage"
        )
    return None


def pytest_collection_modifyitems(
    config: pytest.Config, items: list[pytest.Item]
) -> None:
    catalog: Catalog = config._xverif_catalog  # type: ignore[attr-defined]
    plan: ExecutionPlan = config._xverif_plan  # type: ignore[attr-defined]
    selected_ids = plan.selected_ids()
    repo_root = _repo_root(config)
    kept: list[pytest.Item] = []
    deselected: list[pytest.Item] = []
    unmapped: list[str] = []
    for item in items:
        suite = catalog.owner_for_path(Path(str(item.path)), repo_root)
        if suite is None:
            unmapped.append(item.nodeid)
            continue
        item.user_properties.append(("xverif_suite", suite.id))
        setattr(item, "_xverif_suite_id", suite.id)
        if suite.id in selected_ids:
            kept.append(item)
        else:
            deselected.append(item)
    if unmapped:
        raise pytest.UsageError(
            "collected pytest items are missing catalog ownership:\n  "
            + "\n  ".join(sorted(unmapped))
        )
    items[:] = kept
    if deselected:
        config.hook.pytest_deselected(items=deselected)
