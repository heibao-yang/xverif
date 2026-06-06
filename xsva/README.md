# xsva

SystemVerilog Assertion 语义编译工具。

把 SVA 从文本语法编译为结构化 IR（Surface → Sequence → Timeline），所有解释从 IR 生成。

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

## 测试

```bash
make test
```
