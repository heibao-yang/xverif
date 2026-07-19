# xif_agent v1 设计与验证计划

## Summary
- 主工作目录固定为 `xif_agent`；`xwave` 仅作为外部波形检查工具调用，不迁移工作目录，不修改 `xwave` 代码。
- `xif_agent` 采用“UVM master 驱动 + interface 内建 slave responder”的结构；共享对象拆分为 `xif_cfg` 和 `xif_item#(PD_T)`。
- 验证顺序固定为：`VCS 编译通过 -> 仿真产出 FSDB -> xwave 检查关键时序 -> 再看 UVM log/coverage`。
- 如果在执行过程中确认 `xwave` 存在阻断性问题，立即停止当前任务并通知用户，不对 `xwave` 做任何修补。

## Key Changes
- 公共结构
  - `xif_agent_pkg` 仅包含 `class/typedef/enum`。
  - `xif_if#(type PD_T=...)` 独立实现，包含 `clk/rst/vld/rdy/bp/pd`、`drv_cb`、`mon_cb`、SVA、timeout checker、responder 线程、`OPEN/CLOSE/RLS` 强类型控制 task。
  - `xif_cfg` 至少包含：`active/passive`、`MASTER/SLAVE_RESPONDER`、`RDY/BP/NONE`、`RLS/SHORT/LONG/PULSE/RANDOM/NORMAL`、`timeout_cycles`、`short/long min/max`、`pulse_period`、`random_probability`、`normal_switch_period`、`idle_pd_policy(STABLE/X_ONLY/FULL_RANDOM)`。
  - `xif_item#(PD_T)` 仅包含事务数据与节拍控制：`pd`、`leading_cycles`、`post_cycles`。
- 行为定义
  - `passive` 只监控；`active + MASTER` 才创建 `sequencer/driver`。
  - `MASTER driver` 只驱 `vld/pd`，使用 `@drv_cb` 和非阻塞赋值；按 `leading_cycles/post_cycles` 工作。
  - `SLAVE_RESPONDER` 不创建 UVM slave driver；`xif_if` 根据 `xif_cfg` 驱动 `rdy` 或 `bp`。
  - 握手定义固定为：`RDY => vld && rdy`，`BP => vld && !bp`，`NONE => vld`。
  - responder 采用突发长度模型：`RLS` 不反压；`SHORT/LONG` 按可配区间采样长度；`PULSE` 周期性插入反压；`RANDOM` 按每拍概率；`NORMAL` 定期在前述模式间重选。
  - 空闲 `pd` 策略支持 `STABLE/X_ONLY/FULL_RANDOM`，默认 `STABLE`。
- 配置传递
  - UVM 组件通过 `uvm_config_db` 获取同一个 `xif_cfg` handle。
  - `interface` 通过虚接口连接拿到同一个 `xif_cfg` 引用，不复制配置对象。

## Validation Workflow
- 编译与仿真
  - 默认编译参数固定包含：`-full64 -sverilog -ntb_opts uvm-1.2 -timescale=1ns/1ps -debug_access+all -kdb -lca`。
  - 提供最小 smoke testbench，默认启用 FSDB dump，确保每个 smoke case 都生成波形。
  - 至少覆盖 `MASTER+RDY`、`MASTER+BP`、`MASTER+NONE`、`SLAVE_RESPONDER` 四类基础场景。
- xwave 使用方式
  - 在 `xif_agent` 内触发仿真与生成 FSDB；只在需要查询波形时调用 `/home/yian/xwave/xwave`。
  - 不修改 `xwave` 仓库，不在 `xwave` 仓库内开发，不新增补丁或脚本到 `xwave` 代码树。
  - 使用 `xwave open/value/list/list diff` 检查关键时序：握手成立拍、阻塞持续拍数、force `OPEN/CLOSE/RLS` 生效与释放、连续握手成功区间。
- 失败准则
  - 若 `xwave` 无法正确打开 FSDB、返回错误结果、或缺少完成本任务所必需的查询能力，先确认是使用方式问题。
  - 若确认问题来自 `xwave` 本身且阻塞继续验证，立即停止任务并向用户报告，不对 `xwave` 做修复性改动。

## Deliverables
- 一套可编译的 `xif_agent` v1 设计方案，覆盖参数化 `PD_T`、master 驱动、slave responder、SVA、timeout、覆盖率钩子。
- 一套基于 VCS 的 smoke 验证入口，默认产出 FSDB。
- 一套以 `xwave` 为第一验收手段的检查流程说明。
- 在任务收尾阶段补充一份简短结论：总结 `xwave` 在本任务中的可用性，以及从使用者角度是否存在改进空间；该结论只做评估，不包含对 `xwave` 的代码修改。

## Assumptions
- `xwave` 当前仅作为只读分析工具使用；`xif_agent` 是唯一允许变更的工作目录。
- `rdy` 为高有效 ready，`bp` 为高有效 backpressure，同一实例仅允许 `RDY/BP/NONE` 三选一。
- reset 期间禁止有效握手，流控输出保持阻塞态；reset 释放后恢复 responder 逻辑。
- 原计划中的 `xif_xact` 更名为 `xif_cfg`；字符串型 `set_bp` 不保留，统一改为枚举命令。
