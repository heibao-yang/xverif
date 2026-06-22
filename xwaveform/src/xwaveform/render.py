from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from .loader import SignalData, load_manifest, load_signal


def _low_word(data: SignalData) -> np.ndarray:
    if data.value_words.shape[1] == 0:
        return np.zeros(data.row_count, dtype=np.uint64)
    return np.asarray(data.value_words[:, 0], dtype=np.uint64)


def _known(data: SignalData) -> np.ndarray:
    if data.known_words.shape[1] == 0:
        return np.zeros(data.row_count, dtype=bool)
    full = np.ones(data.row_count, dtype=bool)
    remaining = data.width
    for idx in range(data.word_count):
        bits = min(64, max(0, remaining))
        if bits <= 0:
            break
        mask = np.uint64((1 << bits) - 1) if bits < 64 else np.uint64(0xffffffffffffffff)
        full &= (np.asarray(data.known_words[:, idx], dtype=np.uint64) & mask) == mask
        remaining -= bits
    return full


def _hex_words(words: np.ndarray) -> str:
    parts = [f"{int(v):016x}" for v in reversed(words.tolist())]
    text = "".join(parts).lstrip("0") or "0"
    return "0x" + text


def _words_to_int(words: np.ndarray) -> int:
    value = 0
    for word in reversed(words.tolist()):
        value = (value << 64) | int(word)
    return value


def _stats_for(data: SignalData) -> dict[str, Any]:
    known = _known(data)
    known_indices = np.nonzero(known)[0]
    item: dict[str, Any] = {
        "signal": data.signal,
        "row_count": data.row_count,
        "known_sample_count": int(known_indices.size),
        "unknown_sample_count": int(data.row_count - known_indices.size),
    }
    if known_indices.size:
        min_idx = max_idx = int(known_indices[0])
        min_value = max_value = _words_to_int(np.asarray(data.value_words[min_idx], dtype=np.uint64))
        total = 0
        for raw_idx in known_indices.tolist():
            idx = int(raw_idx)
            value = _words_to_int(np.asarray(data.value_words[idx], dtype=np.uint64))
            total += value
            if value < min_value:
                min_value = value
                min_idx = idx
            if value > max_value:
                max_value = value
                max_idx = idx
        mean_value = total / float(known_indices.size)
        item.update({
            "min_hex": _hex_words(np.asarray(data.value_words[min_idx], dtype=np.uint64)),
            "max_hex": _hex_words(np.asarray(data.value_words[max_idx], dtype=np.uint64)),
            "mean": mean_value,
            "mean_decimal": f"{mean_value:.6f}",
        })
    else:
        item.update({"min_hex": None, "max_hex": None, "mean": None, "mean_decimal": None})
    return item


def _format_time(ps: int) -> str:
    if ps % 1_000_000_000 == 0:
        return f"{ps // 1_000_000_000}ms"
    if ps % 1_000_000 == 0:
        return f"{ps // 1_000_000}us"
    if ps % 1000 == 0:
        return f"{ps // 1000}ns"
    return f"{ps}ps"


def _time_to_x(time_ps: np.ndarray, begin: int, end: int, left: int, plot_width: int) -> np.ndarray:
    if end <= begin:
        return np.full(time_ps.shape, left, dtype=np.int64)
    scaled = (time_ps.astype(np.float64) - float(begin)) / float(end - begin)
    x = left + np.clip(np.rint(scaled * (plot_width - 1)), 0, plot_width - 1).astype(np.int64)
    return x


def _draw_signal(draw: ImageDraw.ImageDraw, data: SignalData, begin: int, end: int,
                 left: int, top: int, plot_width: int, track_height: int) -> None:
    mid = top + track_height // 2
    high = top + 5
    low = top + track_height - 6
    draw.line((left, mid, left + plot_width - 1, mid), fill=(220, 224, 230), width=1)
    if data.row_count == 0:
        return
    xs = _time_to_x(np.asarray(data.time_ps), begin, end, left, plot_width)
    values = _low_word(data)
    known = _known(data)
    ys = np.where((values & np.uint64(1)) != 0, high, low)
    for i in range(data.row_count):
        x0 = int(xs[i])
        x1 = int(xs[i + 1]) if i + 1 < data.row_count else left + plot_width - 1
        if x1 < x0:
            x1 = x0
        color = (32, 98, 180) if bool(known[i]) else (170, 70, 170)
        draw.line((x0, int(ys[i]), x1, int(ys[i])), fill=color, width=2)
        if i + 1 < data.row_count and xs[i + 1] != xs[i]:
            draw.line((int(xs[i + 1]), int(ys[i]), int(xs[i + 1]), int(ys[i + 1])), fill=color, width=1)


def render_waveform(manifest_path: str | Path, output: str | Path,
                    width: int = 4096, height_per_signal: int = 24,
                    cursor_count: int = 32, stats_file: str | Path | None = None,
                    quality: int = 95) -> dict[str, Any]:
    manifest = load_manifest(manifest_path)
    signals = manifest.get("signals") or []
    output_path = Path(output)
    stats_path = Path(stats_file) if stats_file else output_path.with_suffix(output_path.suffix + ".stats.json")
    begin = int(manifest.get("begin_ps", 0))
    end = int(manifest.get("end_ps", begin + 1))
    left = 320
    right = 24
    top = 48
    bottom = 32
    plot_width = max(1, width - left - right)
    height = top + bottom + max(1, len(signals)) * height_per_signal
    image = Image.new("RGB", (width, height), (255, 255, 255))
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default()

    draw.text((8, 8), f"{manifest.get('list', 'list')}  {_format_time(begin)} -> {_format_time(end)}", fill=(20, 24, 31), font=font)
    if cursor_count < 2:
        cursor_count = 2
    for idx in range(cursor_count):
        ratio = idx / float(cursor_count - 1)
        x = left + int(round(ratio * (plot_width - 1)))
        t = begin + int(round(ratio * (end - begin)))
        draw.line((x, top - 12, x, height - bottom + 4), fill=(230, 200, 120), width=1)
        if idx % 4 == 0 or idx in (0, cursor_count - 1):
            draw.text((max(left, x - 18), height - bottom + 8), _format_time(t), fill=(100, 80, 30), font=font)

    stats: dict[str, Any] = {
        "manifest_file": str(Path(manifest_path)),
        "image_file": str(output_path),
        "width": width,
        "height": height,
        "cursor_count": cursor_count,
        "time_direction": "left_to_right",
        "signals": [],
    }
    total_rows = 0
    for index, entry in enumerate(signals):
        data = load_signal(manifest, index)
        y = top + index * height_per_signal
        draw.text((8, y + 5), data.signal[-48:], fill=(20, 24, 31), font=font)
        _draw_signal(draw, data, begin, end, left, y, plot_width, height_per_signal)
        item = _stats_for(data)
        stats["signals"].append(item)
        total_rows += data.row_count

    output_path.parent.mkdir(parents=True, exist_ok=True)
    image.save(output_path, format="JPEG", quality=quality, subsampling=0)
    stats_path.parent.mkdir(parents=True, exist_ok=True)
    with stats_path.open("w", encoding="utf-8") as f:
        json.dump(stats, f, indent=2, sort_keys=True)
        f.write("\n")
    return {
        "ok": True,
        "image_file": str(output_path),
        "stats_file": str(stats_path),
        "width": width,
        "height": height,
        "signal_count": len(signals),
        "row_count": total_rows,
        "cursor_count": cursor_count,
        "time_direction": "left_to_right",
    }
