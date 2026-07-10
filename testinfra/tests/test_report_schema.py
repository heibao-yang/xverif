import json
from pathlib import Path

import jsonschema


ROOT = Path(__file__).resolve().parents[2]


def test_minimal_execution_report_matches_schema() -> None:
    schema = json.loads(
        (ROOT / "testinfra/schemas/execution-report.v1.schema.json").read_text(
            encoding="utf-8"
        )
    )
    report = {
        "schema_version": "xverif-execution-report.v1",
        "gate": "fast",
        "catalog_version": "xverif-test-catalog.v1",
        "exitstatus": 0,
        "counts": {"passed": 1},
        "items": [
            {
                "nodeid": "testinfra/tests/test_catalog.py::test_catalog_loads",
                "suite_id": "testinfra.unit",
                "phase": "call",
                "outcome": "passed",
                "duration_sec": 0.01,
                "error_layer": None,
            }
        ],
    }
    jsonschema.Draft202012Validator(schema).validate(report)
