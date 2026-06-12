# xsva

SystemVerilog Assertion 语义编译工具。

把 SVA 从文本语法编译为结构化 IR（Surface → Sequence → Timeline），所有解释从 IR 生成。
高级 sequence（如 `first_match`、`throughout`、`intersect`、`within`、`[*]`、`[->]`、`[=]`）
会在 Timeline IR 的 `semantic_notes` 中生成自然语言语义摘要，避免把复杂语义误写成固定周期检查。

## 命令

```bash
xsva list    --file <file>                       # 列出所有 property/assertion
xsva scan    --file <file>                       # 语法构造分布统计
xsva explain --file <file> --property <name>      # 文本解释
xsva parse   --file <file> --property <name> --emit surface-ir|sequence-ir|timeline-ir
```

## 示例

```bash
python -m xsva list --file tests/golden_ir/simple_impl/input.sva
python -m xsva explain --file tests/golden_ir/simple_impl/input.sva --property p_test
python -m xsva parse --file tests/golden_ir/simple_impl/input.sva --property p_test --emit timeline-ir
```

高级语法示例：

```systemverilog
property p_first;
  req |-> first_match(##[1:4] ack) ##1 done;
endproperty
```

解释输出会说明：`ack` 需要在 1 到 4 个 clk 内第一次匹配到，后续 sequence 在这个第一次匹配点之后继续检查。

## 测试

```bash
make test
```
