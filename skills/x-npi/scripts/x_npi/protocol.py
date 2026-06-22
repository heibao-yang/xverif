"""Pure Python protocol analyzers built from sampled clock-edge rows."""

from __future__ import annotations

from collections import defaultdict, deque
from typing import Any, Deque, Dict, Iterable, List, Tuple

from .wave import active


Json = Dict[str, Any]


def _v(row: Json, name: str | None) -> Any:
    if not name:
        return None
    return row.get("values", {}).get(name)


def apb_transactions(rows: Iterable[Json], cfg: Json) -> List[Json]:
    txns: List[Json] = []
    completion_seen = False
    for row in rows:
        rst = _v(row, cfg.get("rst_n"))
        if cfg.get("rst_n") and not active(rst):
            completion_seen = False
            continue
        if not active(_v(row, cfg["psel"])) or not active(_v(row, cfg["penable"])):
            completion_seen = False
            continue
        pready = cfg.get("pready")
        if pready:
            if not active(_v(row, pready)):
                continue
            if completion_seen:
                continue
            completion_seen = True
        is_write = active(_v(row, cfg["pwrite"]))
        txns.append({
            "time": row["time"],
            "kind": "write" if is_write else "read",
            "addr": _v(row, cfg["paddr"]),
            "data": _v(row, cfg["pwdata"] if is_write else cfg["prdata"]),
            "error": active(_v(row, cfg.get("pslverr"))),
        })
    return txns


def apb_summary(rows: Iterable[Json], cfg: Json) -> Json:
    txns = apb_transactions(rows, cfg)
    return {
        "transactions": txns,
        "summary": {
            "total": len(txns),
            "writes": sum(1 for t in txns if t["kind"] == "write"),
            "reads": sum(1 for t in txns if t["kind"] == "read"),
            "errors": sum(1 for t in txns if t.get("error")),
        },
    }


def axi_transactions(rows: Iterable[Json], cfg: Json) -> Json:
    writes: List[Json] = []
    reads: List[Json] = []
    wbeats: Deque[Json] = deque()
    pending_writes: Deque[Json] = deque()
    pending_reads: Dict[str, Deque[Json]] = defaultdict(deque)
    outstanding: List[Json] = []
    read_osd: Dict[str, int] = defaultdict(int)
    write_osd: Dict[str, int] = defaultdict(int)

    def sig(row: Json, key: str) -> Any:
        return _v(row, cfg.get(key))

    def inc(table: Dict[str, int], txn_id: str) -> None:
        table[txn_id] += 1

    def dec(table: Dict[str, int], txn_id: str) -> None:
        if table.get(txn_id, 0) > 0:
            table[txn_id] -= 1
        if table.get(txn_id) == 0:
            table.pop(txn_id, None)

    for row in rows:
        if cfg.get("rst_n") and not active(sig(row, "rst_n")):
            wbeats.clear()
            pending_writes.clear()
            pending_reads.clear()
            read_osd.clear()
            write_osd.clear()
            continue

        aw = active(sig(row, "awvalid")) and active(sig(row, "awready"))
        w = active(sig(row, "wvalid")) and active(sig(row, "wready"))
        b = active(sig(row, "bvalid")) and active(sig(row, "bready"))
        ar = active(sig(row, "arvalid")) and active(sig(row, "arready"))
        r = active(sig(row, "rvalid")) and active(sig(row, "rready"))

        if w:
            wbeats.append({
                "time": row["time"],
                "data": sig(row, "wdata"),
                "strb": sig(row, "wstrb"),
                "last": active(sig(row, "wlast")) if cfg.get("wlast") else True,
            })
        if aw:
            txn = {
                "kind": "write",
                "addr_time": row["time"],
                "addr": sig(row, "awaddr"),
                "id": str(sig(row, "awid") or "0"),
                "len": sig(row, "awlen") or "0",
                "data": [],
                "wstrb": [],
            }
            inc(write_osd, txn["id"])
            pending_writes.append(txn)
        while wbeats and pending_writes:
            target = next((t for t in pending_writes if not t.get("data_complete")), None)
            if target is None:
                break
            beat = wbeats.popleft()
            target["data"].append(beat["data"])
            target["wstrb"].append(beat["strb"])
            target.setdefault("first_data_time", beat["time"])
            target["last_data_time"] = beat["time"]
            if beat["last"]:
                target["data_complete"] = True
        if b:
            bid = str(sig(row, "bid") or "0")
            for txn in list(pending_writes):
                if cfg.get("bid") and txn["id"] != bid:
                    continue
                if not txn.get("data_complete"):
                    continue
                txn["resp_time"] = row["time"]
                txn["resp"] = sig(row, "bresp")
                dec(write_osd, txn["id"])
                writes.append(txn)
                pending_writes.remove(txn)
                break
        if ar:
            txn = {
                "kind": "read",
                "addr_time": row["time"],
                "addr": sig(row, "araddr"),
                "id": str(sig(row, "arid") or "0"),
                "len": sig(row, "arlen") or "0",
                "data": [],
                "resp": [],
            }
            inc(read_osd, txn["id"])
            pending_reads[txn["id"]].append(txn)
        if r:
            rid = str(sig(row, "rid") or "0")
            queue = pending_reads.get(rid) or next((q for q in pending_reads.values() if q), deque())
            if queue:
                txn = queue[0]
                txn["data"].append(sig(row, "rdata"))
                txn["resp"].append(sig(row, "rresp"))
                txn.setdefault("first_data_time", row["time"])
                txn["last_data_time"] = row["time"]
                if active(sig(row, "rlast")) or not cfg.get("rlast"):
                    reads.append(txn)
                    dec(read_osd, txn["id"])
                    queue.popleft()
        outstanding.append({
            "time": row["time"],
            "read": sum(read_osd.values()),
            "write": sum(write_osd.values()),
            "read_by_id": dict(read_osd),
            "write_by_id": dict(write_osd),
        })

    return {"writes": writes, "reads": reads, "outstanding": outstanding}


def axi_summary(rows: Iterable[Json], cfg: Json) -> Json:
    data = axi_transactions(rows, cfg)
    all_txns = data["writes"] + data["reads"]
    latencies = [
        int(t["resp_time"] - t["addr_time"])
        for t in data["writes"]
        if "resp_time" in t and "addr_time" in t
    ] + [
        int(t["last_data_time"] - t["addr_time"])
        for t in data["reads"]
        if "last_data_time" in t and "addr_time" in t
    ]
    return {
        **data,
        "summary": {
            "writes": len(data["writes"]),
            "reads": len(data["reads"]),
            "total": len(all_txns),
            "max_latency": max(latencies) if latencies else None,
            "max_read_outstanding": max((x["read"] for x in data["outstanding"]), default=0),
            "max_write_outstanding": max((x["write"] for x in data["outstanding"]), default=0),
        },
    }


def stream_summary(rows: Iterable[Json], cfg: Json) -> Json:
    transfers: List[Json] = []
    stalls: List[Json] = []
    packets: List[Json] = []
    current_stall: Json | None = None
    current_packet: Json | None = None
    beat_index = 0

    for row in rows:
        valid = active(_v(row, cfg["valid"]))
        ready = active(_v(row, cfg["ready"])) if cfg.get("ready") else True
        if valid and not ready:
            if current_stall is None:
                current_stall = {"begin_time": row["time"], "cycles": 0}
            current_stall["cycles"] += 1
        elif current_stall is not None:
            current_stall["end_time"] = row["time"]
            stalls.append(current_stall)
            current_stall = None
        if not (valid and ready):
            continue
        fields = {name: _v(row, sig) for name, sig in cfg.get("fields", {}).items()}
        if cfg.get("data"):
            fields.setdefault("data", _v(row, cfg["data"]))
        transfer = {"time": row["time"], "beat_index": beat_index, "fields": fields}
        transfers.append(transfer)
        beat_index += 1
        sop = active(_v(row, cfg.get("sop")))
        eop = active(_v(row, cfg.get("eop")))
        if sop or (current_packet is None and cfg.get("sop")):
            current_packet = {"begin_time": row["time"], "beats": []}
        if current_packet is not None:
            current_packet["beats"].append(transfer)
        if eop and current_packet is not None:
            current_packet["end_time"] = row["time"]
            packets.append(current_packet)
            current_packet = None
    if current_stall is not None:
        stalls.append(current_stall)
    return {
        "summary": {
            "transfers": len(transfers),
            "stalls": len(stalls),
            "packets": len(packets),
        },
        "transfers": transfers,
        "stalls": stalls,
        "packets": packets,
    }
