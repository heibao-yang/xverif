"""Shared pytest infrastructure for xdebug tests."""

from .artifacts import ArtifactWriter
from .assertions import InvariantError, assert_invariants
from .cli import CliRunner, RunResult
from .command import CommandRunner
from .normalize import NormalizeOptions, normalize_response
from .stdio_loop import StdioLoopRunner

__all__ = [
    "ArtifactWriter",
    "CliRunner",
    "CommandRunner",
    "InvariantError",
    "NormalizeOptions",
    "RunResult",
    "StdioLoopRunner",
    "assert_invariants",
    "normalize_response",
]
