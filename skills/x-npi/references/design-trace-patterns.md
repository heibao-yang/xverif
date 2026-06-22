# 设计追踪模式

使用 Python `pynpi.lang` 查询静态设计关系。它适合批量 dependency inventory 和 source evidence report。

## Driver trace

```python
from x_npi.runtime import pynpi_lifecycle
from x_npi.design import trace_driver

with pynpi_lifecycle([sys.argv[0], "-dbdir", "simv.daidir"], load_design=True):
    rows = trace_driver("top.u_dut.data_q")
```

每行包含：

- `use`：statement 或 use handle 文本。
- `source`：被追踪的 source handle。
- `scope`：statement 所在 scope。
- `signals`：该 statement 使用的 signal handle。
- `is_pass_through`：NPI 是否穿过了边界。

## Load trace

```python
rows = trace_load("top.u_dut.req")
```

load trace 适合 fanout summary、consumer inventory 和快速 dependency map。

## 重要规则

- 查找 dependency signal 时，优先使用 `DrvLoadStmt.get_sig_hdl_list()`，不要优先使用 `get_src_hdl()`。
- 当报告粒度是 signal-level 时，将 bit-select 和 part-select dependency 归一到 parent signal。
- 除非用户明确要求 data-only dependency，否则保持 `include_control=True`。
- 不要把这个静态 trace 当作某个波形时间点的 active-driver 证据；这种场景应使用 xdebug `trace.active_driver`。
