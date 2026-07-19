from pathlib import Path

from testinfra.xverif_test.catalog import Catalog
from testinfra.xverif_test.dependencies import (
    load_default_dependency_registry,
    validate_suite_dependencies,
)


ROOT = Path(__file__).resolve().parents[2]


def test_all_declared_pytest_paths_exist() -> None:
    catalog = Catalog.load(
        ROOT / "testinfra/catalog.v1.yaml",
        ROOT / "testinfra/schemas/catalog.v1.schema.json",
    )
    missing = [
        path
        for suite in catalog.suites
        for path in suite.pytest_paths()
        if not (ROOT / path).exists()
    ]
    assert missing == []


def test_all_suite_dependencies_are_registered() -> None:
    catalog = Catalog.load(
        ROOT / "testinfra/catalog.v1.yaml",
        ROOT / "testinfra/schemas/catalog.v1.schema.json",
    )
    validate_suite_dependencies(catalog, load_default_dependency_registry(ROOT))
