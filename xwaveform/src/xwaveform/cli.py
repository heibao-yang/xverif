from __future__ import annotations

import argparse
import json
import sys

from .render import render_waveform


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="xwaveform")
    sub = parser.add_subparsers(dest="command", required=True)

    render = sub.add_parser("render", help="render xdebug list.export data to JPG")
    render.add_argument("--manifest", required=True)
    render.add_argument("--output", required=True)
    render.add_argument("--width", type=int, default=4096)
    render.add_argument("--height-per-signal", type=int, default=24)
    render.add_argument("--cursor-count", type=int, default=32)
    render.add_argument("--stats-file", default=None)
    render.add_argument("--quality", type=int, default=95)
    render.add_argument("--json", action="store_true")

    args = parser.parse_args(argv)
    if args.command == "render":
        result = render_waveform(
            manifest_path=args.manifest,
            output=args.output,
            width=args.width,
            height_per_signal=args.height_per_signal,
            cursor_count=args.cursor_count,
            stats_file=args.stats_file,
            quality=args.quality,
        )
        if args.json:
            print(json.dumps(result, sort_keys=True))
        else:
            print(f"image_file={result['image_file']}")
            print(f"stats_file={result['stats_file']}")
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
