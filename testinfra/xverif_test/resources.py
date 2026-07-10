from __future__ import annotations

import pytest

from .catalog import Suite


def apply_xdist_resource_group(item: pytest.Item, suite: Suite) -> None:
    tokens = sorted(str(value) for value in suite.resources.get("tokens", []))
    if not tokens:
        return
    # loadgroup keeps all suites that claim the same normalized token set on one
    # worker, providing deterministic serialization without replacing xdist.
    group = "xverif-resource-" + "-".join(tokens)
    item.add_marker(pytest.mark.xdist_group(name=group))
