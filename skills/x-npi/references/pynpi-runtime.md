# pynpi 运行时模式

## 启动

优先使用随 skill 提供的 runtime helper：

```python
from x_npi.runtime import pynpi_lifecycle

with pynpi_lifecycle([sys.argv[0]]):
    ...
```

访问 design DB 时：

```python
with pynpi_lifecycle([sys.argv[0], "-dbdir", daidir], load_design=True):
    ...
```

对于同时使用 design 和 FSDB 的脚本，只有当所用 Python API 需要 design binding 时才同时传入 `-dbdir` 和 `-ssf`。普通波形扫描只需要 `npisys.init([argv0])` 加 `waveform.open(fsdb)`。

## 环境

`VERDI_HOME` 必须指向 Synopsys Verdi 安装目录。helper 会把下面路径加入 `sys.path`：

```text
$VERDI_HOME/share/NPI/python
```

如果进程无法加载共享库，需要在启动 Python 之前设置 `LD_LIBRARY_PATH`。不要等 Python 已经启动后再尝试修复 `LD_LIBRARY_PATH`。

## 输出规范

NPI 可能向 stdout 写入信息文本。对于 JSON 工具，应在 `npisys.init`、`load_design` 和 `end` 期间把 stdout 重定向到 stderr，然后只把最终 JSON 打印到 stdout。

使用：

```python
from x_npi.jsonio import ok, error, print_json
```

来保持脚本输出稳定。

## 失败处理

在 JSON 中使用明确错误码：

```json
{"ok": false, "action": "wave_stats", "error": {"code": "FAILED", "message": "..."}}
```

常见失败：

- 缺少 `VERDI_HOME`。
- license 不可用。
- FSDB 不存在或不可读。
- 信号路径不匹配。
- `-dbdir` 不是有效的 Verdi/VCS 数据库，导致 `npisys.load_design` 失败。
