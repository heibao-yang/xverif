# xloc 日志位置还原

xloc 将 UVM/仿真日志中的长文件路径压缩为 `L_XXXXXXXX`，同时保留日志中的源码行号 `L_XXXXXXXX(<line>)`；它可还原文件、统计热点或给日志加注释。

## 何时使用

- 用户给了带 `L_00000001` 的压缩日志。
- 需要还原 loc_id 对应源码位置。
- 需要查看源码上下文或统计高频 loc_id。
- 需要把压缩日志 annotate 成人类可读版本。

## 入口

命令行：

```bash
xloc resolve L_00000001 --map out/sim.log.xloc.jsonl
xloc context L_00000001 --map out/sim.log.xloc.jsonl --line 42 --before 5 --after 5
xloc stats out/sim.log --top 20
xloc annotate out/sim.log --map out/sim.log.xloc.jsonl
```

## 工作流

1. 整段日志先 `stats` 找热点。
2. 对关键 loc_id 用 `resolve`。
3. 需要源码证据再用 `context`。
4. 回答引用日志中的 `loc_id(line)` 与 resolve 得到的 file。

## 排障

- map 通常是 `<log>.xloc.jsonl`。
- `resolve/context` 必须有 map。
- loc_id not found：检查是否拿错 sidecar map。
- 源码文件缺失：仍可引用 map 中 file 与日志中的 line，但不能展示上下文。
