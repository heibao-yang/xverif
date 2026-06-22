# list.export + xwaveform Implementation Plan

## Summary

Add an xdebug waveform export path for signal lists and a Python rendering tool.
The export path preserves full value-change data for each signal, while the
renderer produces a fixed-width 4096 pixel JPG waveform image with time
increasing from left to right.

The implementation target is:

- `xdebug` action `list.export`
- Python tool `xwaveform`
- MCP helper `xverif_waveform_render_list`

## xdebug list.export

`list.export` exports all signals from a named waveform list over a requested
time range.

Request fields:

- `args.name` or `args.list`: list name, falling back to the latest list when
  omitted, matching existing `list.*` behavior.
- `args.time_range.begin/end`, or top-level aliases `args.start/end`,
  `args.begin/end`, `args.from/to`.
- `args.output_dir`: optional output directory. If omitted, xdebug writes under
  the waveform session directory in `list_exports/`.
- `args.format`: `u64bin` by default, or `hex_tsv` for text audit output.

Behavior:

- Reject windows shorter than 256ns with a structured hint that AI callers
  should use `list.value_at` or `value.batch_at` for point reads.
- Export each signal to one file.
- Always include the value at the begin time when readable.
- Append every value-change event in the window.
- Do not truncate or sample data in the exporter.

Default binary format `u64bin.v1`:

- One binary file per signal.
- Fixed-width little-endian `uint64` rows.
- Row layout: `time_ps`, followed by value words, followed by known-mask words.
- `manifest.json` records signal path, filename, row count, word count, begin,
  end, format, and time unit metadata.
- This format is intended for fast C++ sequential writing and fast Python
  `numpy.memmap` reading.

Optional `hex_tsv` format:

- One TSV file per signal.
- Columns: `time_ps`, `value_hex`, `known_hex`.
- Intended for inspection/debugging, not the default fast path.

## Shared Export Component

Add a common waveform export component for:

- Creating output directories.
- Sanitizing and de-duplicating filenames.
- Writing manifest metadata.
- Writing binary rows.
- Writing optional hex TSV rows.

`list.export` will use the component first. Existing `axi.export` and
`stream.export` remain behavior-compatible and can migrate later.

## xwaveform

Add a new Python package and tool wrapper:

- `xwaveform/src/xwaveform/`
- `tools/xwaveform`
- `xwaveform/Makefile`
- top-level Makefile integration

Dependencies:

- Required: `numpy`, `pillow`
- Optional for user analysis/examples: `matplotlib`

CLI:

```bash
xwaveform render \
  --manifest manifest.json \
  --output wave.jpg \
  --width 4096 \
  --height-per-signal 24 \
  --cursor-count 32
```

Rendering behavior:

- Output is a single JPG.
- Width is fixed to 4096 pixels by default.
- Time maps linearly from left to right over the exported begin/end range.
- Draw 32 equally spaced vertical cursors by default, including begin and end.
- Label cursor time positions.
- Do not detect or mark multiple transitions within a single pixel column.
- The rendered image is a visual summary; full data remains in export files.
- Use JPG `quality=95` and `subsampling=0`.

Statistics:

- Rendering writes a sibling stats file, defaulting to `<output>.stats.json`.
- Per signal fields: `signal`, `row_count`, `min_hex`, `max_hex`, `mean`,
  `known_sample_count`, `unknown_sample_count`.
- Samples with unknown bits are counted as unknown and excluded from numeric
  min/max/mean.

Python API:

- `load_manifest(path)`
- `load_signal(manifest, signal_or_index)`
- returned arrays should be directly usable with NumPy and Matplotlib.

## MCP

Add MCP tool `xverif_waveform_render_list`.

Parameters:

- `session`
- `name`
- `begin`
- `end`
- `output_dir=""`
- `image_file=""`
- `width=4096`
- `height_per_signal=24`
- `cursor_count=32`
- `export_format="u64bin"`
- `image_format="jpg"`

Behavior:

- If the requested window is shorter than 256ns, do not export or render.
  Return a structured hint recommending point reads.
- Otherwise call `xverif_debug_query` with `list.export`, then call
  `tools/xwaveform render`.
- Return JSON with `ok`, `export_dir`, `manifest_file`, `image_file`,
  `stats_file`, `signal_count`, `row_count`, `width`, `cursor_count`,
  and `time_direction:"left_to_right"`.

## Validation

- `make -C xdebug schema-test`
- `make -C xdebug contract-test`
- focused C++ unit tests for the shared export component
- `make -C xwaveform test`
- non-AXI real-waveform check in `run_complex_wave.py`
- MCP registration tests in-process
- real MCP/session waveform linkage outside the sandbox, as required by the
  user's standing MCP test rule
