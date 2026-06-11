# xcov

`xcov` is an AI/MCP-oriented query engine for VCS/Verdi coverage databases.
It accepts `xcov.v1` JSON requests and returns compact `xout` by default.

## Quick Start

One-shot:

```bash
printf '%s\n' '{"api_version":"xcov.v1","action":"session.open","target":{"vdb":"fake"},"args":{"name":"cov0","fake":true}}' \
  | tools/xcov --json -
```

Loop mode for MCP:

```bash
tools/xcov --stdio-loop
```

The loop emits a JSON ready line with `protocol:"xcov-stdio-loop"` and then
JSONL envelopes containing `xout` and `json` payloads. NPI diagnostic output is
routed to stderr so stdout remains machine-readable.

## Real NPI Smoke

The current verified Python runtime is:

```text
/home/yian/miniconda3/envs/xdebug-mcp/bin/python
Python 3.11.15
```

Verified database:

```text
/home/yian/uart_example/sim/merged.vdb
```

Observed smoke results:

```text
session.open: ok, test_count=1, top_scope_count=2
tests.list: ok, matched_count=1
metrics.list: ok, matched_count=4
cov.holes: ok, matched_count=871 with max_items=5
```

Real NPI commands need access to the local Synopsys license server. In sandboxed
execution, run them outside the sandbox.

## MCP Tools

`xverif_mcp` exposes xcov as a stateful backend:

```text
xverif_cov_session_open
xverif_cov_session_list
xverif_cov_session_use
xverif_cov_session_close
xverif_cov_query
xverif_cov_raw_request
xverif_cov_list_actions
xverif_cov_get_schema
```

Use `XVERIF_MCP_ENABLE_COV=0` to hide coverage tools. `XVERIF_XCOV_BIN` and
`XVERIF_XCOV_PYTHON` override the xcov executable and Python runtime.
