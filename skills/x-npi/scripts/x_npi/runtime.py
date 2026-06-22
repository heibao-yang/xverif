"""Runtime setup helpers for Synopsys Python NPI scripts."""

from __future__ import annotations

import contextlib
import os
import sys
from pathlib import Path
from typing import Iterator, Sequence


def verdi_home(explicit: str | None = None) -> str:
    home = explicit or os.environ.get("VERDI_HOME") or os.environ.get("XVERIF_XCOV_VERDI_HOME")
    if not home:
        raise RuntimeError("VERDI_HOME is required for pynpi")
    return home


def configure_pynpi(explicit_verdi_home: str | None = None) -> str:
    home = verdi_home(explicit_verdi_home)
    py_dir = str(Path(home) / "share" / "NPI" / "python")
    if py_dir not in sys.path:
        sys.path.insert(0, py_dir)
    return home


@contextlib.contextmanager
def redirect_stdout_to_stderr() -> Iterator[None]:
    saved = os.dup(1)
    os.dup2(2, 1)
    try:
        yield
    finally:
        os.dup2(saved, 1)
        os.close(saved)


@contextlib.contextmanager
def pynpi_lifecycle(argv: Sequence[str], load_design: bool = False, verdi_home_path: str | None = None) -> Iterator[object]:
    configure_pynpi(verdi_home_path)
    from pynpi import npisys  # type: ignore

    args = list(argv)
    with redirect_stdout_to_stderr():
        if npisys.init(args) != 1:
            raise RuntimeError("npisys.init failed")
        try:
            if load_design and npisys.load_design(args) != 1:
                raise RuntimeError("npisys.load_design failed")
            yield npisys
        finally:
            npisys.end()
