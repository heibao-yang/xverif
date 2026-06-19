# AXI SVT VIP real-wave fixture

本 fixture 用真实 Synopsys SVT AXI VIP、真实 VCS 仿真和真实 FSDB/daidir
验证 xdebug 的 AXI 查询链路。环境代码来自 `AXI_REFERENCE_ROOT` 指向的
AXI 参考环境；编译拆分、宏、UVM、KDB 和 FSDB 选项参考 xring 的
`dv/cfg/Makefile`。

## 依赖

- `AXI_REFERENCE_ROOT`：包含 `tb/` 和 `tests/` 的 AXI 环境根目录。
- `SVT_VIP_INCDIR`：包含 `svt_axi_if.svi`、`svt_axi.uvm.pkg`。
- `SVT_VIP_SRCDIR`：SVT VIP 的 VCS SystemVerilog source overlay。
- VCS、Verdi/FSDB PLI 和 AXI VIP 运行环境。

仓库不依赖 svtref skill 或其 Python 依赖包；fixture 直接编译并运行真实
VIP 环境。

## 执行

```bash
make -C xdebug pytest-axi-vip
```

也可以只构建波形：

```bash
make -C xdebug/testdata/waveform/axi_vip_real run
```

默认固定 seed 7，生成 2 个 ID、每个 ID 6 笔读和 6 笔写，允许每个
ID/方向 4 笔 outstanding，并注入 1–8 cycle 的 slave response delay。
这组参数已在当前 SVT VIP 上验证。参考环境的 delay sequence 与旧版 VIP
内部约束存在共同合法域；例如把最大 delay 提高到 20 会导致 constraint
inconsistency，因此压力参数调整必须先通过独立仿真门禁。

输出位置由 `manifest.json` 定义，`out/` 不进入版本库。pytest 会检查：

- 仿真无 UVM error/fatal，scoreboard 通过；
- FSDB 和 daidir 均真实生成；
- `axi.config.load/list`、`axi.query/cursor/analysis`；
- request/response pairing、latency outlier、outstanding timeline；
- channel stall 采样；
- session 打开、复用和清理。
