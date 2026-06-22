import json
import struct
import tempfile
import unittest
from pathlib import Path

from xwaveform.loader import load_manifest, load_signal
from xwaveform.render import render_waveform


class XwaveformTest(unittest.TestCase):
    def test_load_and_render_u64bin(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            data = root / "sig.u64bin"
            rows = [
                (0, 0, 1),
                (128000, 1, 1),
                (256000, 0, 1),
            ]
            with data.open("wb") as f:
                for row in rows:
                    f.write(struct.pack("<QQQ", *row))
            manifest = {
                "version": 1,
                "format": "u64bin.v1",
                "list": "basic",
                "begin_ps": 0,
                "end_ps": 256000,
                "signals": [{
                    "index": 0,
                    "signal": "top.sig",
                    "file": "sig.u64bin",
                    "row_count": 3,
                    "width": 1,
                    "word_count": 1,
                    "columns": 3,
                }],
            }
            manifest_path = root / "manifest.json"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            loaded = load_manifest(manifest_path)
            sig = load_signal(loaded, "top.sig")
            self.assertEqual(sig.row_count, 3)
            out = root / "wave.jpg"
            result = render_waveform(manifest_path, out, width=4096)
            self.assertTrue(out.exists())
            self.assertEqual(result["width"], 4096)
            stats = json.loads((root / "wave.jpg.stats.json").read_text())
            self.assertEqual(stats["signals"][0]["min_hex"], "0x0")
            self.assertEqual(stats["signals"][0]["max_hex"], "0x1")


if __name__ == "__main__":
    unittest.main()
