#!/usr/bin/env python3

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import List


ROOT = Path(__file__).resolve().parents[1]
HOME = Path.home()


TESTS = {
    "xif_master_rdy_test": {
        "fsdb": ROOT / "waves" / "xif_master_rdy_test.fsdb",
        "list_windows": [("65ns", "165ns", "65ns")],
        "value_checks": [
            ("xif_tb_top.if0.vld", "85ns", "'b1"),
            ("xif_tb_top.if0.rdy", "85ns", "'b1"),
            ("xif_tb_top.if0.pd", "85ns", "'b{00010000,0001000000000000}"),
            ("xif_tb_top.if0.vld", "125ns", "'b1"),
            ("xif_tb_top.if0.rdy", "125ns", "'b1"),
            ("xif_tb_top.if0.pd", "125ns", "'b{00010001,0001000000000001}"),
            ("xif_tb_top.if0.vld", "165ns", "'b1"),
            ("xif_tb_top.if0.rdy", "165ns", "'b1"),
            ("xif_tb_top.if0.pd", "165ns", "'b{00010010,0001000000000010}"),
        ],
    },
    "xif_master_bp_test": {
        "fsdb": ROOT / "waves" / "xif_master_bp_test.fsdb",
        "list_windows": [("75ns", "175ns", "75ns")],
        "value_checks": [
            ("xif_tb_top.if0.vld", "95ns", "'b1"),
            ("xif_tb_top.if0.bp", "95ns", "'b0"),
            ("xif_tb_top.if0.pd", "95ns", "'b{00010000,0001000000000000}"),
            ("xif_tb_top.if0.vld", "135ns", "'b1"),
            ("xif_tb_top.if0.bp", "135ns", "'b0"),
            ("xif_tb_top.if0.pd", "135ns", "'b{00010001,0001000000000001}"),
            ("xif_tb_top.if0.vld", "175ns", "'b1"),
            ("xif_tb_top.if0.bp", "175ns", "'b0"),
            ("xif_tb_top.if0.pd", "175ns", "'b{00010010,0001000000000010}"),
        ],
    },
    "xif_master_none_test": {
        "fsdb": ROOT / "waves" / "xif_master_none_test.fsdb",
        "list_windows": [("55ns", "145ns", "55ns")],
        "value_checks": [
            ("xif_tb_top.if0.vld", "65ns", "'b1"),
            ("xif_tb_top.if0.pd", "65ns", "'b{00010000,0001000000000000}"),
            ("xif_tb_top.if0.vld", "105ns", "'b1"),
            ("xif_tb_top.if0.pd", "105ns", "'b{00010001,0001000000000001}"),
            ("xif_tb_top.if0.vld", "145ns", "'b1"),
            ("xif_tb_top.if0.pd", "145ns", "'b{00010010,0001000000000010}"),
        ],
    },
    "xif_slave_responder_test": {
        "fsdb": ROOT / "waves" / "xif_slave_responder_test.fsdb",
        "list_windows": [("85ns", "595ns", "85ns")],
        "value_checks": [
            ("xif_tb_top.if0.vld", "85ns", "'b1"),
            ("xif_tb_top.if0.bp", "85ns", "'b0"),
            ("xif_tb_top.if0.pd", "85ns", "'b{10000000,0010000000000000}"),
            ("xif_tb_top.if0.vld", "155ns", "'b1"),
            ("xif_tb_top.if0.bp", "155ns", "'b0"),
            ("xif_tb_top.if0.pd", "155ns", "'b{10000001,0010000000000001}"),
            ("xif_tb_top.if0.dbg_force_state", "105ns", "'d2"),
            ("xif_tb_top.if0.dbg_force_state", "125ns", "'d1"),
            ("xif_tb_top.if0.dbg_force_state", "145ns", "'d0"),
        ],
    },
    "xif_passive_monitor_test": {
        "fsdb": ROOT / "waves" / "xif_passive_monitor_test.fsdb",
        "list_windows": [("65ns", "115ns", "65ns")],
        "value_checks": [
            ("xif_tb_top.if0.vld", "75ns", "'b1"),
            ("xif_tb_top.if0.rdy", "75ns", "'b1"),
            ("xif_tb_top.if0.pd", "75ns", "'b{11000000,0011000000000000}"),
            ("xif_tb_top.if0.vld", "95ns", "'b1"),
            ("xif_tb_top.if0.rdy", "95ns", "'b1"),
            ("xif_tb_top.if0.pd", "95ns", "'b{11000001,0011000000000001}"),
            ("xif_tb_top.if0.vld", "115ns", "'b1"),
            ("xif_tb_top.if0.rdy", "115ns", "'b1"),
            ("xif_tb_top.if0.pd", "115ns", "'b{11000010,0011000000000010}"),
        ],
    },
    "xif_master_duplicate_pd_test": {
        "fsdb": ROOT / "waves" / "xif_master_duplicate_pd_test.fsdb",
        "list_windows": [("55ns", "95ns", "55ns")],
        "value_checks": [
            ("xif_tb_top.if0.vld", "65ns", "'b1"),
            ("xif_tb_top.if0.rdy", "65ns", "'b1"),
            ("xif_tb_top.if0.pd", "65ns", "'b{01011010,1010010101011010}"),
            ("xif_tb_top.if0.vld", "95ns", "'b1"),
            ("xif_tb_top.if0.rdy", "95ns", "'b1"),
            ("xif_tb_top.if0.pd", "95ns", "'b{01011010,1010010101011010}"),
        ],
    },
    "xif_back_to_back_sequence_test": {
        "fsdb": ROOT / "waves" / "xif_back_to_back_sequence_test.fsdb",
        "value_checks": [
            ("xif_tb_top.if0.vld", "65ns", "'b1"),
            ("xif_tb_top.if0.rdy", "65ns", "'b1"),
            ("xif_tb_top.if0.pd", "65ns", "'b{00100000,0100000000000000}"),
            ("xif_tb_top.if0.pd", "85ns", "'b{00100001,0100000000000001}"),
            ("xif_tb_top.if0.pd", "105ns", "'b{00100010,0100000000000010}"),
            ("xif_tb_top.if0.pd", "125ns", "'b{00100011,0100000000000011}"),
        ],
    },
    "xif_burst_sequence_test": {
        "fsdb": ROOT / "waves" / "xif_burst_sequence_test.fsdb",
        "value_checks": [
            ("xif_tb_top.if0.pd", "65ns", "'b{00110000,0101000000000000}"),
            ("xif_tb_top.if0.pd", "85ns", "'b{00110000,0101000000000001}"),
            ("xif_tb_top.if0.pd", "105ns", "'b{00110000,0101000000000010}"),
            ("xif_tb_top.if0.vld", "125ns", "'b0"),
            ("xif_tb_top.if0.pd", "145ns", "'b{00110001,0101000000010000}"),
            ("xif_tb_top.if0.pd", "185ns", "'b{00110001,0101000000010010}"),
        ],
    },
    "xif_pulse_sequence_test": {
        "fsdb": ROOT / "waves" / "xif_pulse_sequence_test.fsdb",
        "value_checks": [
            ("xif_tb_top.if0.pd", "65ns", "'b{01000000,0110000000000000}"),
            ("xif_tb_top.if0.pd", "85ns", "'b{01000000,0110000000000001}"),
            ("xif_tb_top.if0.vld", "105ns", "'b0"),
            ("xif_tb_top.if0.pd", "135ns", "'b{01000001,0110000000010000}"),
            ("xif_tb_top.if0.pd", "205ns", "'b{01000010,0110000000100000}"),
        ],
    },
    "xif_multi_channel_outstanding_test": {
        "fsdb": ROOT / "waves" / "xif_multi_channel_outstanding_test.fsdb",
        "value_checks": [
            ("xif_tb_top.if0.pd", "65ns", "'b{00000000,0111000000000000}"),
            ("xif_tb_top.if0.pd", "85ns", "'b{01000000,0111000000010000}"),
            ("xif_tb_top.if0.pd", "105ns", "'b{10000000,0111000000100000}"),
            ("xif_tb_top.if0.pd", "125ns", "'b{11000000,0111000000110000}"),
            ("xif_tb_top.if0.pd", "155ns", "'b{00000001,0111000000000001}"),
            ("xif_tb_top.if0.pd", "215ns", "'b{11000001,0111000000110001}"),
        ],
    },
    "xif_multi_channel_weighted_arb_test": {
        "fsdb": ROOT / "waves" / "xif_multi_channel_weighted_arb_test.fsdb",
        "value_checks": [
            ("xif_tb_top.if0.pd", "65ns", "'b{00000000,0111000000000000}"),
            ("xif_tb_top.if0.pd", "85ns", "'b{00000001,0111000000000000}"),
            ("xif_tb_top.if0.pd", "105ns", "'b{00000010,0111000000000000}"),
            ("xif_tb_top.if0.pd", "125ns", "'b{01000000,0111000000010000}"),
            ("xif_tb_top.if0.pd", "145ns", "'b{01000001,0111000000010000}"),
            ("xif_tb_top.if0.pd", "165ns", "'b{01000010,0111000000010000}"),
        ],
    },
    "xif_driver_mailbox_prefetch_test": {
        "fsdb": ROOT / "waves" / "xif_driver_mailbox_prefetch_test.fsdb",
        "value_checks": [
            ("xif_tb_top.if0.vld", "65ns", "'b1"),
            ("xif_tb_top.if0.rdy", "65ns", "'b0"),
            ("xif_tb_top.if0.pd", "65ns", "'b{00100000,0100000000000000}"),
            ("xif_tb_top.if0.rdy", "105ns", "'b1"),
            ("xif_tb_top.if0.pd", "105ns", "'b{00100000,0100000000000000}"),
            ("xif_tb_top.if0.pd", "125ns", "'b{00100001,0100000000000001}"),
        ],
    },
    "xif_reaction_sequence_test": {
        "fsdb": ROOT / "waves" / "xif_reaction_sequence_test.fsdb",
        "value_checks": [
            ("xif_tb_top.if0.pd", "65ns", "'b{01010000,1000000000000000}"),
            ("xif_tb_top.if0.pd", "85ns", "'b{01010001,1000000100000000}"),
            ("xif_tb_top.if0.pd", "105ns", "'b{01010010,1000000100000001}"),
            ("xif_tb_top.if0.pd", "125ns", "'b{01010011,1000000100000010}"),
        ],
    },
    "xif_complex_integrated_test": {
        "fsdb": ROOT / "waves" / "xif_complex_integrated_test.fsdb",
        "value_checks": [
            ("xif_tb_top.if0.pd", "65ns", "'b{01100000,1001000000000000}"),
            ("xif_tb_top.if0.pd", "125ns", "'b{00110000,0101000000000000}"),
            ("xif_tb_top.if0.pd", "215ns", "'b{01000000,0110000000000000}"),
            ("xif_tb_top.if0.pd", "315ns", "'b{00000000,0111000000000000}"),
            ("xif_tb_top.if0.pd", "495ns", "'b{01010000,1000000000000000}"),
            ("xif_tb_top.if0.pd", "535ns", "'b{01010010,1000000100000001}"),
        ],
    },
    "xif_master_slave_dual_test": {
        "fsdb": ROOT / "waves" / "xif_master_slave_dual_test.fsdb",
        "value_checks": [
            ("xif_tb_top.if_slave.dbg_effective_mode", "65ns", "'d3"),
            ("xif_tb_top.if_master.rdy", "65ns", "'b1"),
            ("xif_tb_top.if_master.pd", "65ns", "'b{00000000,0111000000000000}"),
            ("xif_tb_top.if_master.pd", "125ns", "'b{11000000,0111000000110000}"),
            ("xif_tb_top.if_master.rdy", "85ns", "'b0"),
            ("xif_tb_top.if_slave.pd", "225ns", "'b{11000001,0111000000110001}"),
        ],
    },
}


def run_cmd(cmd: List[str], check: bool = True) -> str:
    env = os.environ.copy()
    verdi_home = env.get("VERDI_HOME", "/home/yian/Synopsys/verdi/V-2023.12-SP2")
    npi_lib = Path(verdi_home) / "share" / "NPI" / "lib" / "linux64"
    if npi_lib.exists():
        current_ld_path = env.get("LD_LIBRARY_PATH", "")
        env["LD_LIBRARY_PATH"] = f"{npi_lib}:{current_ld_path}" if current_ld_path else str(npi_lib)

    proc = subprocess.run(
        cmd,
        cwd=str(ROOT),
        env=env,
        universal_newlines=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if check and proc.returncode != 0:
        raise RuntimeError(
            f"command failed: {' '.join(cmd)}\nstdout:\n{proc.stdout}\nstderr:\n{proc.stderr}"
        )
    return proc.stdout.strip()


def cleanup_xwave(xwave_bin: str) -> None:
    run_cmd([xwave_bin, "session", "kill", "all"], check=False)
    for pattern in (".xwave.registry", ".xwave.registry.lock", ".xwave.lists"):
        path = HOME / pattern
        if path.exists():
            path.unlink()
    for sock in HOME.glob(".xwave.*.sock"):
        sock.unlink()


def open_session(xwave_bin: str, fsdb: Path) -> int:
    out = run_cmd([xwave_bin, "open", str(fsdb)])
    match = re.search(r"\[Session\s+(\d+)\]", out)
    if not match:
        raise RuntimeError(f"failed to parse session id from:\n{out}")
    return int(match.group(1))


def get_value(xwave_bin: str, sid: int, signal: str, time_str: str, fmt: str = "-b") -> str:
    return run_cmd([xwave_bin, "value", signal, time_str, fmt, "-s", str(sid)]).splitlines()[-1].strip()


def build_list(xwave_bin: str, sid: int, list_name: str) -> None:
    run_cmd([xwave_bin, "list", "new", list_name, "-s", str(sid)])
    for sig in ("xif_tb_top.if0.vld", "xif_tb_top.if0.rdy", "xif_tb_top.if0.bp", "xif_tb_top.if0.pd"):
        run_cmd([xwave_bin, "list", "add", "-s", str(sid), "-l", list_name, sig])


def get_diff(xwave_bin: str, sid: int, list_name: str, begin: str, end: str) -> str:
    return run_cmd([xwave_bin, "list", "diff", "-l", list_name, "-b", begin, "-e", end, "-s", str(sid)]).splitlines()[-1].strip()


def check_test(xwave_bin: str, test_name: str) -> None:
    spec = TESTS[test_name]
    fsdb = spec["fsdb"]
    if not fsdb.exists():
        raise RuntimeError(f"missing FSDB: {fsdb}")

    cleanup_xwave(xwave_bin)
    sid = open_session(xwave_bin, fsdb)

    if "list_windows" in spec:
        list_name = f"chk_{test_name}"
        build_list(xwave_bin, sid, list_name)
        for begin, end, expected in spec["list_windows"]:
            actual = get_diff(xwave_bin, sid, list_name, begin, end)
            if actual != expected:
                raise RuntimeError(
                    f"{test_name}: diff mismatch for [{begin}, {end}], expected {expected}, got {actual}"
                )

    for signal, time_str, expected in spec["value_checks"]:
        fmt = "-d" if expected.startswith("'d") else "-b"
        actual = get_value(xwave_bin, sid, signal, time_str, fmt=fmt)
        if actual != expected:
            raise RuntimeError(
                f"{test_name}: value mismatch for {signal} @ {time_str}, expected {expected}, got {actual}"
            )

    print(f"[PASS] {test_name}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--test", default="xif_master_rdy_test")
    parser.add_argument("--xwave", default="/home/yian/xwave/xwave")
    args = parser.parse_args()

    tests = list(TESTS.keys()) if args.test == "all" else [args.test]
    for test_name in tests:
        if test_name not in TESTS:
            raise RuntimeError(f"unknown test: {test_name}")
        check_test(args.xwave, test_name)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        sys.exit(1)
