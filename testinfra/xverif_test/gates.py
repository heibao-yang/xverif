from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from .catalog import Catalog, SelectedSuite


@dataclass(frozen=True)
class ExecutionPlan:
    gate: str
    suites: tuple[SelectedSuite, ...]

    def render(self) -> str:
        lines = [f"xverif gate: {self.gate}", f"selected suites: {len(self.suites)}"]
        for selected in self.suites:
            mode = "required" if selected.required else "optional"
            suite = selected.suite
            lines.append(
                f"- {suite.id} [{mode}] level={suite.level} cost={suite.cost_class}: "
                f"{selected.reason}"
            )
        return "\n".join(lines)

    def selected_ids(self) -> set[str]:
        return {item.suite.id for item in self.suites}


def build_plan(catalog: Catalog, gate: str) -> ExecutionPlan:
    return ExecutionPlan(gate=gate, suites=catalog.select_gate(gate))


def changed_paths(repo_root: Path, base: str) -> tuple[str, ...]:
    # Implemented in the changed-only migration stage. Keeping the operation
    # explicit prevents a silent broad/narrow fallback before impact data exists.
    raise NotImplementedError(
        f"changed-only selection is not implemented yet for base {base!r} in {repo_root}"
    )
