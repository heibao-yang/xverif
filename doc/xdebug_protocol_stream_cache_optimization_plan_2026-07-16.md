# xdebug APB / AXI / Stream 缓存优化计划

日期：2026-07-16
状态：决策已确认，尚未实施
范围：`xdebug` engine 内 APB、AXI、stream 的波形分析缓存、内存布局、查询索引与失效合同。

## 1. 目标与非目标

### 目标

1. 保持 APB 和 AXI “每个 session/config 首次完整扫描一次，后续 action 复用结果”的行为，同时降低常驻内存。
2. 让 `stream.query` 在相同 session/config/分析范围的重复调用中复用基础分析结果，而不是每次重新扫描 FSDB。
3. 对常见 selector（地址、ID、时间窗、握手时间、channel）建立可度量、可按需创建的索引，避免把未使用的索引常驻在内存中。
4. 为所有缓存条目建立明确的 key、失效、容量、命中率和扫描次数诊断；不把缓存命中与协议语义混在 handler 中。
5. 不改变既有 JSON/XOUT action 输出，除非后续确认需要公开 cache diagnostics 或淘汰策略。

### 非目标

1. 不实现一个把 APB、AXI、stream 原始 FSDB sample 强行合并的“全局 sample tape”。三者的信号集合、值格式和解析状态机不同；它的内存成本和同时间戳语义风险均高于当前收益。
2. 不改变 `ClockSampleScanner` / `ClockExpressionSampleScanner` 的 edge、before/after 或 raw-value 语义。本计划在其上缓存领域分析结果。
3. 第一阶段不把缓存持久化到磁盘，也不跨 engine/session 复用；FSDB handle 与 session 生命周期仍是天然边界。

## 2. 已核对的现状

| 领域 | 当前扫描和缓存 | 当前查询代价 | 主要内存热点 / 风险 |
|---|---|---|---|
| APB | `ApbAnalyzer::results_` 以 config `name` 缓存 `ApbResult`；首次扫 FSDB 全时域，后续命中直接返回。 | `all` 为时间序指针视图，time-range 已可 `lower_bound`；按地址仍线性扫描并重复解析 hex。 | transaction 中的 `addr/data` string；缺少按地址索引、缓存预算和显式失效。 |
| AXI | `AxiAnalyzer::results_` 以 config `name` 缓存 `AxiResult`；首次完整扫描，query/analysis/cursor/export 共用。 | 地址/ID selector 大多线性扫描；握手索引惰性建立。 | `all` 深拷贝 `writes+reads`；transaction payload 多 vector/string；每个采样点复制 outstanding 的 per-ID map。 |
| stream | `stream.query` 每次新建局部 `StreamAnalyzer`/`StreamAnalysis`；未缓存。未给 `time_range` 时范围为 FSDB min..max。 | 每次都做时钟边沿扫描和表达式求值；`line_limit` 只限制响应，不限制扫描。 | 直接缓存现有 `StreamAnalysis` 会保存嵌套 map、packet-beat、副本字段和 filter view，长波形下会过大。 |

额外发现：APB/AXI 的 config manager 拒绝同名 `config.load`，因而当前 name-key 在公开路径下不会被同名覆盖；stream 的 `mode:replace` 可以覆盖同名 config。因此 stream cache 必须在 config 更新时精确失效，不能只以 name 作为长期正确性键。

## 3. 总体架构

新增内部组件 `waveform/cache/analysis_cache.*`，仅负责条目生命周期，不理解 APB/AXI/stream 业务对象。

### 3.1 核心对象

```text
AnalysisCache<Key, Entry>
  key              = protocol + session identity + FSDB fingerprint + config fingerprint
  entry            = immutable canonical analysis result + lazy indexes + byte accounting
  state            = building | ready | failed | evicted
  metadata         = created_at, last_access, scan_count, hit_count, estimated_bytes
  policy           = max_bytes, LRU, pin count
```

Key 必须至少含：

- 协议/stream 类型；
- 当前 engine 的 session identity；
- FSDB identity（已在 engine context 维护的 dev/inode/size/mtime，必要时加入内容 fingerprint）；
- 所有会影响分析语义的规范化 config fingerprint：信号路径、reset、clock edge/sample point，以及 stream expression/字段/packet 设置；
- 对 stream，基础缓存固定为完整 FSDB 范围的无 filter 解析结果；请求的 `time_range`、`line_limit`、filter、channel 不进入基础 key，而在 result 上派生。

`Entry` 完成构造后不可修改 canonical 数据。懒索引允许单调增加，但必须单独计入字节数、可独立淘汰，并且不得改变 action 返回语义。

### 3.2 生命周期和失效

1. engine/session 退出即释放全部缓存；不持久化。
2. FSDB identity 变化时，清空该 engine 的所有 protocol/stream 条目并记录 `fsdb_changed` 原因。
3. `stream.config.load` 成功后，对所有被 replace 的 stream name 使旧 entry 失效；append 的新名称不影响既有 entry。
4. 后续若 APB/AXI 增加 replace/update config action，必须在 config 成功落盘的同一事务中失效该 config 的条目；不能依赖下次 query 猜测。
5. 不允许删除正在被 cursor 使用的 canonical entry；cursor 持有 pin，关闭 session 或 cursor 结束后才允许淘汰。

### 3.3 容量和可观测性

- 默认采用 engine-local byte budget 与 LRU，不依据 entry 数量。
- 每个 canonical result 和每个 lazy index 必须实现 `estimated_bytes()`；先允许保守估算，后续用 allocator/profile 校准。
- 内部 log 记录 `hit/miss/build/evict/invalidate`、key 的非敏感摘要、字节数、扫描时长和原因。
- 第一阶段不公开新增 action 参数。若后续需要用户可见控制，单独设计 `cache.status` / `cache.clear` action 与 schema，而不将隐式 cache policy 塞进 APB/AXI/stream action。

## 4. AXI 计划

### 4.1 Phase A：消除 canonical transaction 的深拷贝

当前 `AxiTransactionTracker::finish()` 把 `writes` 和 `reads` 中完整的 `AxiTransaction` 再逐条拷入 `all`，而 `AxiTransaction` 包含 payload vector 和多个 string。这是最直接、低语义风险的内存浪费。

改为单一 transaction arena：

```text
AxiResult
  transactions: vector<AxiTransaction>       // 唯一实体，完成后不再搬移
  write_ids: vector<uint32_t>                // 视图，按既有顺序
  read_ids: vector<uint32_t>
  all_ids_by_addr_time: vector<uint32_t>
  all_ids_by_resp_time: vector<uint32_t>
  pending_ids: ...                           // 仅在最终 diagnostics 需要时
```

- 所有 cursor、selector、export 和 analysis helper 改为从 id 解引用返回 `const AxiTransaction*`，保留现有对 handler 的接口形状。
- `all_ids_by_addr_time` 与 `all_ids_by_resp_time` 是 32-bit index view，不复制 payload。
- 构建完成后 reserve/冻结 `transactions`，保证指针稳定。
- 为 32-bit id 设上限检查；超过上限时明确失败，不发生截断。

预期收益：完成 transaction 至少消除一份完整 `all` 拷贝；payload 越大，收益越接近删除约三分之一的已完成 transaction 存储。

### 4.2 Phase B：lazy selector indexes

保留既有 handshake index 的“首次使用才建”模式，并扩展为：

- `address -> vector<txn_id>`：按读/写分桶；地址解析在 scan/build 阶段只做一次，避免每次 selector 重复 `strtoull`。
- `id -> vector<txn_id>`：用于同 ID 重复查询、outstanding 过滤和定位。
- 时间索引继续采用排序 id vector + `lower_bound`，不复制 transaction。

索引只在对应 selector 首次被使用时创建。独立 LRU 可在内存紧张时先淘汰索引，canonical result 仍可服务 O(N) 查询。

### 4.3 Phase C：outstanding 压缩

当前每个采样时刻存储 `AxiOutstandingSample`，其中包含两个 `map<string,int>`。改为：

- 全局 read/write outstanding：只保存值变化的 change point；
- per-ID outstanding：保存 `{time_delta, id_id, signed_delta}` 事件；ID 使用 arena 内 intern table；
- 每 N 个 change point（初始建议 256，作为内部常量并以测试/benchmark 调整）保存 checkpoint，窗口 query 从最近 checkpoint 重建。

这样将“每周期复制整张 map”改为“仅变更时记录一个小 delta”。`axi.outstanding_timeline`、statistics 和 outlier 的结果必须与现有 oracle 完全一致。

### 4.4 Phase D：payload sidecar（仅在 profile 证明确有必要时）

将每 beat 的 `data/wstrb/data_resp/data_handshake_times/data_last` 移入 transaction payload arena，由 transaction 保存 offset/count。默认仍完整缓存，确保 `output.include_data:true` 不改变行为；仅当 budget policy 已确认后再允许 sidecar 独立淘汰。

不在本计划默认实现 payload 淘汰，因为被淘汰后 `include_data:true` 的重建或错误合同属于公开语义决策。

### 4.5 详细 canonical 数据模型

`AxiResult` 在 build 完成后必须是不可变的 canonical result；它不再以 `writes`、`reads`、`all` 三份完整 transaction 实体表达同一事实。第一版使用单一 transaction arena 和轻量 id view：

```text
AxiResult
  identity
    cache key、config fingerprint、FSDB fingerprint、scan_begin/end
    scan_count、build_duration、estimated_bytes、analysis_complete

  transactions: vector<AxiTransaction>       // 唯一已完成 transaction 所有者
  pending_transactions: vector<AxiTransaction>

  write_ids: vector<uint32_t>                // 已完成 write 的既有排序语义
  read_ids: vector<uint32_t>                 // 已完成 read 的既有排序语义
  all_ids_by_addr_time: vector<uint32_t>     // 替代当前深拷贝 all
  all_ids_by_resp_time: vector<uint32_t>     // 替代 all_by_resp_time 的二层索引

  outstanding: OutstandingTimeline
  diagnostics: AxiDiagnostics

  optional lazy indexes
    address -> vector<uint32_t>
    id -> vector<uint32_t>
    handshake channel -> vector<HandshakeIndexEntry>
```

`uint32_t` id 在写入前必须检查容量上限；超过可表达范围时，分析明确失败而不是截断。所有 id view 只保存 transaction id，不复制 `data/wstrb/data_resp/data_handshake_times/data_last` 或 string 字段。

构建阶段可使用 tracker 内部的 deque/map 处理未完成 AW/W/B/AR/R 配对；transaction 只有完成或在扫描结束被归类为 pending 后才 move 到最终 arena。发布 ready entry 前 reserve/freeze 最终 vector，之后不得 append 或重排 `transactions`，以保证 action 临时得到的 `const AxiTransaction*` 稳定。

`pending_transactions` 必须保留既有 incomplete/reset/orphan diagnostics 所需的信息，但不进入 completed transaction 的 write/read/all views；其 public summary、export 可见性必须先用现有 response 基线逐项确认。

### 4.6 tracker、action 和 cursor 的迁移方式

#### Tracker 收敛

1. `AxiTransactionTracker::consume()` 保持 AW/W/B/AR/R 配对、out-of-order、burst、reset 和 valid-begin-time 计算不变。
2. `complete_write()` / `complete_read()` 不再分别永久拥有最终 transaction；它们调用 `append_completed(transaction, direction)`，返回 stable id，并把 id 追加到 `write_ids` 或 `read_ids`。
3. `finish()` 生成 `all_ids_by_addr_time`，排序的是 id view；再从该 view 构造 `all_ids_by_resp_time`。禁止任何 `AxiTransaction` 复制。
4. `AxiTransactionTracker` 不负责 LRU、cache key 或 session 失效；它只产出一个完整 immutable `AxiResult`。

#### Handler 与 helper 收敛

所有 `axi.query`、`axi.cursor`、`axi.analysis`、`axi.statistics`、`axi.export`、`axi.request_response_pair`、`axi.latency_outlier`、`axi.outstanding_timeline`、`axi.channel_stall` 必须通过同一 `ensure_axi_analyzed` / cache lookup。禁止某 action 因需要特殊视图而重新扫描或维护旁路 result。

| 使用方 | 迁移后读取方式 | 保持的合同 |
|---|---|---|
| `axi.query` | direction/address/ID selector 返回 id，再解引用 canonical transaction | selector、index/last、`output.include_data`、handshake query JSON 不变。 |
| `axi.cursor` | cursor 保存对应 id view 的位置；`begin/next/prev/last` 从 view 解引用 | 方向、1-based cursor state、transaction 顺序不变。 |
| `axi.analysis` / statistics | 遍历 write/read/all id view；latency/outstanding 引用同一 transaction | max/min/percentile、diagnostics、完整性字段不变。 |
| window/pair/outlier/export | 用 addr-time 或 resp-time id view 二分/筛选 | time range、match time、truncation、payload 输出不变。 |
| handshake lookup | `HandshakeIndexEntry` 保存 transaction id + beat index，按 channel/time 排序 | 现有精确 handshake anchor 匹配不变。 |

任何从 id 转成 `const AxiTransaction*` 的指针只可在当前 handler/query view 生命周期内使用；不可存进跨 entry 的全局容器。cache entry 被 cursor 使用时加 pin，释放 cursor/session 后解除，避免 LRU 造成悬垂引用。

### 4.7 懒索引的详细设计

索引不是 canonical transaction 的替代物，而是可独立创建、统计字节数和淘汰的加速层。

1. **地址索引**：scan/build 期将可知地址解析为数值 metadata，首次 address selector 生成 `unordered_map<uint64_t, AddressBucket>`。bucket 内保持原 action 期望的顺序，并按 direction 分开或以 id view 过滤。`last` 使用 bucket 尾部；`index` 直接定位。
2. **ID 索引**：将重复 ID 以 interned id 表示；首次 ID selector 生成 `id -> vector<txn_id>`。不能改变“同 ID 最老 AW/AR 配对”的 tracker 规则，该规则仍发生在索引建立之前。
3. **握手索引**：保留惰性建立原则，但条目从裸 transaction pointer 改为 transaction id；channel 内按 handshake time 和 beat index 保持稳定排序。
4. **时间索引**：completed transaction 的 addr-time/resp-time id view 在 build 时必须存在，因为 range action 多、且仅为 4-byte id；按时间 action 使用 `lower_bound`，不复制实体。
5. **淘汰顺序**：预算不足时先淘汰 Address/ID/Handshake lazy index，再淘汰未 pin 的完整 AXI entry。索引被淘汰后，后续 selector 可以从 canonical result 重新建索引，不触发 FSDB scan。

### 4.8 outstanding timeline 的详细设计

当前 `snapshot()` 在每个 sample 保存全局 outstanding 和两份 `map<string,int>`。新结构必须表达完全相同的时间序状态，但避免无变化周期重复复制 map：

```text
OutstandingTimeline
  global_change_points:
    {time, read_count, write_count}

  id_deltas:
    {time_delta, interned_id, read_delta, write_delta}

  checkpoints:
    每 256 个 change point：
      {point_offset, time, read_count, write_count, per_id_state}
```

构建规则：

1. 只要全局 read/write 或任一 per-ID outstanding 变化，才追加 change event；纯 sample 且状态相同不存储。
2. 同一 timestamp 的多项变化先按当前 tracker 的 consume 顺序聚合，再生成稳定 event，防止 query 重建时出现瞬态顺序差异。
3. checkpoint 间隔初始为 256 change point，仅作为内部实现常量；不得出现在公共 schema。profile 后可调，但必须保持 JSON 等价。
4. 查询时间窗时二分到 begin 前最近 checkpoint，回放 delta 至 begin，再遍历窗口内 change point；peak、first_nonzero、final count、per-ID 过滤都基于重建状态计算。
5. 如果 action 需要完整每-cycle timeline 而非变化点，必须先核对当前 schema/response 是否真的承诺该粒度；不能为了压缩静默丢弃一个公共时间点。

### 4.9 AXI 缓存生命周期、预算和错误路径

1. cache key 必须包含 engine/session、FSDB identity 和完整规范化 `AxiConfig` fingerprint；当前 name 仅是用户可读 lookup 名，不是正确性 key。
2. 当前 `axi.config.load` 拒绝同名 config；若未来支持 update/replace，则 config 成功保存时同步 invalidate 对应 key、cursor 和 lazy indexes。
3. FSDB identity 变化、session.close、engine 退出、engine crash 都释放 AXI entry；不能跨 session 复用 NPI handle 或 transaction pointer。
4. LRU 淘汰必须跳过 pin entry。未 pin entry 被淘汰后，下次 action 自动完整重扫并返回正常结果，内部记录 `cache_miss_after_evict`。
5. scan/build 失败不发布 ready entry；错误沿用现有 `protocol_analyze_error` 路径。默认不做 failure cache，以免掩盖暂态 NPI/FSDB 问题。
6. payload sidecar 独立淘汰不在第一版实施；完整 entry 淘汰后自动重扫，确保 `output.include_data:true` 仍有确定语义。

### 4.10 AXI 新增测试要求

除现有 `xdebug.axi_vip`、`xdebug.contract` 和相关 protocol suite 外，新增下列测试。真实 FSDB/NPI/VCS/VIP 测试必须在沙箱外，从正式 catalog gate/suite 入口运行。

#### 单元测试（不需要真实 FSDB）

1. **arena/id view**：构造 read/write/pending transaction（含多 beat payload），断言 `transactions` 是唯一实体，write/read/all/resp-time view 覆盖正确 id、排序等价，且没有 payload 深拷贝。
2. **指针稳定与 pin**：ready 后禁止 append/reorder；id 解引用地址稳定；cursor pin 阻止 eviction，unpin 后可被 LRU 淘汰；不产生悬垂 pointer。
3. **selector index**：地址、地址+ID、ID、first/index/last、无命中、重复地址、重复 ID；索引前后返回同一 transaction，索引独立淘汰后可重建。
4. **时间/握手 index**：addr-time/resp-time 边界、相同 timestamp 的稳定排序、多 channel、多 beat handshake、找不到 handshake；id-based entry 与旧 pointer-based oracle 等价。
5. **outstanding delta**：无变化 sample 不产生事件；同 timestamp 多变更稳定聚合；checkpoint 前后任意窗口重建的全局/per-ID count、peak、first_nonzero、final 值均与朴素每 sample oracle 相同。
6. **cache key/lifecycle**：任一 AXI signal、reset polarity、edge/sample point、FSDB fingerprint 改变必须 miss；同 config 的不同 action 必须 hit；build failure 不发布；byte accounting 覆盖 canonical data、payload、timeline 和 lazy index。

#### 真实 AXI waveform / VIP 集成测试

1. **热命中覆盖**：同一 session/config 顺序运行 `axi.query`、`axi.cursor`、`axi.analysis`、`axi.statistics`、`axi.export`、`axi.request_response_pair`、`axi.latency_outlier`、`axi.outstanding_timeline`、`axi.channel_stall`；断言第一次后不再扫描，`full_scan_count=1`，各 JSON 与关闭新缓存布局的基线逐字段一致。
2. **事务语义**：AW-before-W、W-before-AW、同周期 AW/W、burst、多 beat、out-of-order read、同 ID 多 outstanding、BID/RID oldest matching、incomplete transaction、reset 清理、orphan B/R/W，分别比较独立 VIP oracle。
3. **selector 与 payload**：address、address+ID、index、last、direction、handshake_time/channel、`output.include_data:false/true`；验证 transaction、beat 顺序、payload、match time、truncation 不变。
4. **时间和统计**：不同 begin/end 边界、resp time 与 addr time 排序、latency percentile、outlier、pair、outstanding peak/final/per-ID、timeline change point 输出，与朴素结果或独立 VIP oracle 等价。
5. **timeline 压缩回归**：稀疏变化、长时间无变化、频繁 ID 变化、checkpoint 跨越窗口、同 timestamp 变化；验证压缩前后的 action JSON 一致，并记录 timeline bytes 下降。
6. **预算与自动重建**：以不超过 128 MiB 的 `XDEBUG_ANALYSIS_CACHE_MAX_BYTES` 创建多个 AXI config/entry 触发 LRU；验证未 pin 的最旧 entry 被淘汰、下一 action 自动完整重扫、`full_scan_count` 重新从本 entry 的首次扫描开始、JSON 与基线一致。验证带 active cursor 的 pin entry 不被淘汰。
7. **内存/性能**：记录 cold scan、hot query、selector-index 首次/再次查询、timeline window query 的 wall time、entry bytes 与 peak RSS；要求 transaction arena 布局低于现有 `writes + reads + all` 深拷贝基线，热 query 不触发 scanner。

## 5. APB 计划

### 5.1 保留现有紧凑 ownership，补齐 metadata 与索引

APB 的 `writes/reads` 为唯一 transaction 实体，`all` 已是 `const ApbTransaction*` 时间序视图，因此不要为了统一而改成复杂 arena。

实施：

1. 把 `ApbResult` 纳入公共 cache entry，加入 config/FSDB fingerprint、bytes、hits、last access 和失效 reason。
2. scan 时把可解析地址同时保存为 `uint64_t address_value` + `has_known_address`，避免后续 address selector 重复解析 string；原 `addr` string 继续用于既有输出。
3. 首次地址 selector 时构建 `unordered_map<uint64_t, vector<uint32_t>>` 的 lazy index。read/write/all 由 index view 派生，不复制 transaction。
4. 保持 `all` 时间序指针 view 与 `lower_bound` window 查询；如需统一接口，可转为 id view，但这不是内存热点，优先稳定性。

预期收益：内存小幅增加（可选 index），但地址型高频查询由 O(N) 降至 O(1)+O(K)，同时消除重复 literal 解析。APB 条目应是 LRU 中最晚淘汰的候选，因为其 canonical result 相对轻量。

### 5.2 详细 canonical 数据模型

APB 不采用 AXI 的 transaction arena 重构。当前 `writes`、`reads` 已是唯一 transaction 所有者，`all` 只是时间序指针 view；强行合并为一份 arena 会扩大改动面但不能显著减少 payload 内存。优化目标是用紧凑 ref 替换 pointer view，并把可查询 metadata 在 build 时一次性写入。

```text
ApbTransaction
  time: npiFsdbTime
  addr: string                         // 保持既有 JSON 输出
  data: string                         // 保持既有 JSON 输出
  is_write: bool
  has_error: bool
  address_value: uint64_t              // scan 时解析，不在每次 query 重复 strtoull
  has_known_address: bool

ApbRef: uint32_t
  bit 31: direction (0=read, 1=write)
  bit 0..30: index in reads/writes

ApbResult
  writes: vector<ApbTransaction>       // 唯一 write 实体，按既有时间顺序
  reads: vector<ApbTransaction>        // 唯一 read 实体，按既有时间顺序
  all_by_time: vector<ApbRef>          // 替代 vector<const ApbTransaction*>
  diagnostics: ApbDiagnostics

  optional lazy indexes
    address -> AddressBucket
      all_refs: vector<ApbRef>
      read_refs: vector<ApbRef>
      write_refs: vector<ApbRef>
```

`ApbRef` 编码/解码必须集中在 `apb_analyzer` 内，禁止 handler 自行位运算。构建时检查 read/write 任一 vector 不超过 `2^31-1`；超出范围时明确报告分析失败，绝不能截断 index。分析完成并发布 cache entry 后，`reads/writes/all_by_time` 不得 append、move 或排序，从而保证 ref 和短生命周期内的解引用指针稳定。

从指针 view 改为 32-bit ref view 会将 `all` 的每项由平台相关的 pointer（通常 8 bytes）降为 4 bytes。它不是 AXI 级别的大收益，但对百万级 APB transfer 可稳定节约约 4 MiB 的时间序视图空间，也使 lazy index 与 cursor 使用同一引用类型。

### 5.3 scan、query、cursor 的迁移方式

#### scan/build

1. `ApbAnalyzer::analyze()` 继续以完整 FSDB 时间范围和现有 `ClockSampleScanner` 语义扫描；reset、`psel/penable/pready`、`completion_seen`、read/write、`pslverr` 判定不得改变。
2. 每次创建 `ApbTransaction` 时，尝试解析 `addr` 写入 `address_value/has_known_address`。解析失败不是分析失败，保持 raw string 输出；该 transaction 仅不进入 numeric address index。
3. writes/reads 排序后，merge 生成 `all_by_time` 的 `ApbRef`，不再存 transaction pointer。
4. 写入 scan count、sample count、scan range、analysis completeness 和 estimated bytes；构建失败时不发布 ready entry。

#### action/helper

所有 `apb.query`、`apb.cursor`、`apb.statistics`、`apb.transfer_window` 必须经过同一个 `ensure_apb_analyzed` / cache lookup 入口。对 ref 的 `resolve(ref)` 统一返回当前 ready entry 内的 `const ApbTransaction*`，handler 不得自己访问 reads/writes 或缓存 ref。

| 使用方 | 缓存后读取方式 | 保持的合同 |
|---|---|---|
| `apb.query` | 无 address 时从 read/write/all view 取 count/index/last；有 address 时使用 bucket。 | direction、first/index/last、line limit、transaction JSON 不变。 |
| `apb.cursor` | cursor 保存 read/write/all view 的 position；all view 通过 ref resolve。 | begin/next/prev/last、1-based state、时间顺序不变。 |
| `apb.transfer_window` | 对 `all_by_time` 用 `lower_bound`，解 ref 后按 direction 与 line limit 过滤。 | begin/end 包含边界、truncation、read/write/all 顺序不变。 |
| `apb.statistics` | 遍历对应 id/ref view 或累计计数，避免额外 scan。 | count/error/read/write 等已发布统计不变。 |

任何返回的 `const ApbTransaction*` 只在当前 action 或被 pin 的 cursor entry 生命周期内有效；不能进入全局静态表或跨 session 保存。

### 5.4 地址懒索引和复杂度保证

当前 address selector 会反复线性扫描，并在 `query.line_limit` 场景中循环调用第 N 次匹配查询，最坏接近 O(N × line_limit)。新路径为：

1. 第一次请求 numeric `args.address` 时，建立一次 `address -> AddressBucket`。遍历 `all_by_time`，将已知地址的 ref 依时间序加入 all/read/write bucket。
2. `first` 返回 bucket 第一个 ref；`index=N` 直接取 bucket 的第 N 个 ref；`last` 直接取尾 ref；`line_limit=L` 仅切片前 L 个 ref。
3. 同一 address 的 read/write direction 在 bucket 内保持现有全局时间序；direction 过滤不能重新排序。
4. 首次索引代价 O(N)，后续 address query 为 O(1)+O(K)，其中 K 是实际输出行数；不再重复解析 `addr` string。
5. Address index 作为独立 lazy cache 记录 bytes/hits，可在预算压力下先于 canonical APB entry 淘汰。索引 miss 后仅从 canonical result 重建，不重扫 FSDB。

若 profile 显示 address cardinality 极高且单次 selector 占多数，则允许把策略改为“同一 entry 第二次 address query 才构建完整 index，第一次线性扫描”；这是内部性能策略，必须先以测试确认首次/热查询的输出完全一致，且不得暴露为公共 fallback 参数。

### 5.5 缓存生命周期、预算与错误路径

1. APB cache key 必须包括 protocol、engine/session、FSDB identity 和完整规范化 `ApbConfig` fingerprint；config name 只是用户 lookup 名，不是正确性键。
2. 当前 `apb.config.load` 拒绝同名 config。若未来引入 replace/update，config 成功落盘时必须同步 invalidate canonical entry、address index 和 cursor state。
3. FSDB identity 改变、session.close、engine 退出或 crash 时释放 APB entry；不得跨 session 复用 transaction pointer/ref。
4. APB canonical entry 较轻，但仍参加 1 GiB engine-local LRU。预算紧张时优先淘汰它的 address index，再考虑淘汰未 pin canonical entry。
5. canonical entry 淘汰后，下次 APB action 自动完整扫描并正常返回，内部记录 `cache_miss_after_evict`；pin 中 cursor entry 不得淘汰。
6. NPI/FSDB build 失败不发布 ready entry，也不默认缓存 failure；继续沿用现有 protocol analysis error 行为。

### 5.6 APB 新增测试要求

除现有 `xdebug.apb_vip`、`xdebug.contract` 和相关 protocol suite 外，新增以下测试。真实 FSDB/NPI/VIP 测试必须在沙箱外，从正式 catalog gate/suite 入口运行。

#### 单元测试（不需要真实 FSDB）

1. **ApbRef**：read/write 编码解码、最大合法 index、溢出拒绝、all-by-time merge、同 timestamp 的稳定顺序，以及 resolve 后 transaction 与原视图等价。
2. **地址 metadata**：known hex、零地址、大地址、解析失败/X/Z/空字符串；原始 `addr` 输出不变，未知地址不进入 numeric index。
3. **地址索引**：同地址多笔 read/write、不同地址、first/index/last、line limit、direction filter；索引前后的结果和顺序完全一致。
4. **复杂度回归**：为 `line_limit` 的 address query 注入可计数 matcher，断言首次完整 index 构建最多遍历 canonical view 一次，热查询不再按每个返回行重扫 N 次。
5. **时间视图/cursor**：begin/end 在 transaction 前、上、后、空窗口、相同 timestamp；begin/next/prev/last、cursor state 与 ref view 一致。
6. **cache lifecycle**：key 覆盖 reset、clock edge/sample point、任一 APB signal、FSDB fingerprint；build failure 不发布、address index 可独立淘汰、pin 阻止 entry eviction、unpin 后允许 LRU。

#### 真实 APB waveform / VIP 集成测试

1. **热命中覆盖**：同一 session/config 连续执行 `apb.query`、`apb.cursor`、`apb.statistics`、`apb.transfer_window`，断言首次后不再扫描 FSDB，各 JSON 与关闭新缓存布局的基线逐字段一致。
2. **协议语义**：read/write、`psel/penable` setup/access、`pready` wait-state、连续 select、reset、`pslverr`、连续同一 transfer 的 `completion_seen` 去重，使用独立 APB VIP/pin-level oracle 比较。
3. **selector 覆盖**：address、direction、index、last、line_limit、无命中、多笔同地址、read/write 混合顺序；验证 index 构建前后 transaction 与 count/summary 完全相同。
4. **窗口和 cursor**：不同 begin/end 边界、同 timestamp 多 transaction、truncation、cursor 全部操作和 1-based state；验证 `all_by_time` ref layout 不改变可见顺序。
5. **预算与自动重建**：以不超过 128 MiB 的 `XDEBUG_ANALYSIS_CACHE_MAX_BYTES` 创建多个 APB config/entry 或与其他 protocol entry 竞争，触发 LRU；验证未 pin entry 被淘汰、下一 APB action 自动完整重扫且 JSON 与基线一致，address index 淘汰只重建索引不重扫 FSDB。
6. **内存/性能**：记录 cold scan、热 count/time-window、首次/再次 address selector 的 wall time、entry bytes 和 peak RSS；要求热 address query 不重复解析每笔地址、不触发 scanner，且 `all_by_time` ref view 小于旧 pointer view 的字节数。

## 6. Stream 计划

### 6.1 先拆分“基础解析”与“请求派生”

不能直接把当前 `StreamAnalysis` 原样塞进 cache：它同时含 summary、所有 transfer/stall/packet、packet 内 beat、副本 field map 以及 query/filter 的临时证据。

引入：

```text
StreamBaseAnalysis (immutable, unfiltered, full range)
  sampled_time/cycle/control columns
  transfer_index, stall ranges, packet ranges
  field registry: alias -> field_id
  value store: packed/interned logic values
  packet metadata and beat span indexes

StreamQueryView (transient)
  requested time_range/channel/filter/query/line_limit
  range-selection and filter result ids
  JSON materialization only for returned rows/packets
```

基础分析继续用现有 `ClockExpressionSampleScanner` 做一次完整扫描，并维持当前 same-timestamp、expression、reset 和 X/Z 语义。query 只负责：

- 用时间列二分定位窗口；
- 用 transfer/stall/packet id view 选取；
- 在必要边界上计算 partial packet 与 SOP/EOP filter 的 unresolved 语义；
- 仅为将返回的 `line_limit` 项生成现有 JSON。

### 6.2 紧凑布局

1. 固定控制字段采用 bitset/连续数组，不再在每个 `StreamRow` 放多个 bool 和重复的 `std::map` 节点。
2. 配置定义的 field 名只保存一次，row/beat 保存 field id；逻辑值优先保存 width + packed 4-state bits，重复的短值可 intern。
3. packet 保存 beat `[begin,end)` 索引，而不是 `vector<StreamBeat>` 的独立深层副本；first/last field 由边界 beat 读取。
4. `first_filter_fields` / `last_filter_fields` 变为派生 view，不在 canonical cache 中重复保存。
5. 对 packet stable mismatch 保存事件列表和 beat id，而不是复制完整字段 map。

### 6.3 查询速度

- 时间窗：`lower_bound` 到开始/结束，O(log N + K)。
- `packet_at`：packet id -> offset 的 O(1) 索引。
- channel：第一次使用时建 `channel -> packet/transfer ids` lazy index；未使用时不占索引内存。
- filter：仍需检查候选 transfer/packet 的字段；先按 time/channel 缩小候选，不能为了缓存命中而改变 filter 的 X/Z 或 partial-boundary 判定。

### 6.4 分阶段落地

先实现功能正确但内存受控的 `StreamBaseAnalysis`（可使用 vector/id view，暂不做 packed 4-state arena），以真实 profile 给出每 transfer/beat 的 byte 数。只有 profile 表明确实需要时，再做 value packing/intern；避免在没有样本时一次性实现复杂的自定义编码器。

### 6.5 详细数据模型

`StreamBaseAnalysis` 必须是“完整 FSDB 范围、无 request filter 的不可变事实”。它不保存某次 `summary`、`line_limit`、filter 命中数或 JSON 对象；这些均属于临时 `StreamQueryView`。

第一版采用以下结构，优先消除 object graph 重复而非过早压缩 bit：

```text
StreamBaseAnalysis
  identity
    cache key、config fingerprint、FSDB fingerprint、scanned_begin/end
    scan_count、build_duration、estimated_bytes、analysis_complete

  samples                         // 一行对应一个被采样的 clock edge
    times: vector<npiFsdbTime>
    cycles: vector<uint32_t>
    control: packed bit columns
      reset, vld, rdy, bp, sop, eop, transfer, stall
    control_xz_count: vector<uint16_t>
    data_xz_count: vector<uint16_t>
    stall_reason_id: vector<uint8_t>
    channel_value_ref: vector<ValueRef>

  field_schema
    fields: vector<FieldDescriptor>       // field_id、名称、宽度、beat/stable 类型
    aliases: config 所有 alias/path 的只读表

  field_columns
    vector<FieldColumn>                   // field_id 对应一条按 sample 对齐的值列

  transfer_ids: vector<uint32_t>          // sample id，按时间升序
  stalls: vector<StallRange>              // start/end sample id、reason、cycles
  packets: vector<PacketRecord>           // packet id、首尾 sample/transfer id、channel、边界标记
  packet_beats: vector<uint32_t>          // packet 的连续 transfer/beat id 存储
  packet_mismatches: vector<MismatchRecord>

  optional lazy indexes
    channel -> transfer_ids / packet_ids
    time -> lower_bound 直接使用 times，不另复制
```

`PacketRecord` 仅保存 beat span 的 offset/count、起止时间/周期、channel、SOP/EOP 完整性和 stable-field mismatch span；它不再持有 `vector<StreamBeat>`，也不复制 `first_fields`、`last_fields`、`first_filter_fields`、`last_filter_fields`。需要这些内容时，由首/尾 beat 对应的 `sample_id` 和 `field_columns` 临时 materialize。

`ValueRef` 在第一版可为 `{column_offset, known}` 或保留已有 `StreamValue` 的紧凑 wrapper，但不得在每个 row 存 field-name map。第二版才将固定宽度值转为 packed 4-state arena：known 0/1 使用 1 bit，X/Z 使用 2-bit plane 或等价可逆编码；输出前还原为当前 `StreamValue` 表达，禁止丢弃 X/Z。

### 6.6 Base analysis 构建流程

1. handler 读取 config 后先计算规范化 fingerprint，再向 `AnalysisCache` 查询 `StreamBaseAnalysis`。
2. cache hit：增加 hit/access metadata，直接进入 query view；不得重新调用 `ClockExpressionSampleScanner`。
3. cache miss：只允许一个 builder 构建该 key；并发/重入请求等待同一个 build 或收到同一失败，不得并行重复扫描同一 FSDB/config。
4. builder 使用当前 `StreamAnalyzer::compile()` 与 `ClockExpressionSampleScanner` 的既有时钟表达式、same-timestamp、sample point 和 value-format 语义。每个 clock sample 仅写入列式 base analysis，不创建 `StreamRow` map 或 JSON。
5. 每次 transfer：把 sample id 加入 `transfer_ids`；packet stream 同时更新当前 packet 的 beat span 和边界状态；stall 关闭时写一条 `StallRange`。
6. 扫描结束后冻结所有 vector，计算 summary 所需的累计计数与 `estimated_bytes`，再原子地将 entry 发布为 ready。build 中途失败时销毁局部对象，不能留下半成品或可命中 cache entry。
7. 在 ready 后才允许 action 获取 transaction/packet/row 的临时指针；entry 被 cursor 或长期 query view 使用时加 pin。

### 6.7 Query view 与既有 action 语义

`StreamQueryView` 接收 `StreamBaseAnalysis` 与当前 request 的 `time_range`、channel、filter、query、`line_limit`。它只保存 candidate id 和输出所需的临时 row/packet，不进入 LRU 预算。

#### 时间窗

1. 用 `times` 对 begin/end 做 `lower_bound` / `upper_bound`，得到 sample window；不重新扫描 FSDB。
2. 用 `transfer_ids` 和 packet/stall range 与 sample window 做交集，得到候选 transfer、stall、packet。
3. packet 从窗口外开始或在窗口外结束时，仍以完整 base packet 为证据，但 response 按当前合同标记 `partial_begin` / `partial_end`；不能因 window 裁切把 packet 误判为完整。
4. query 的 `scanned_begin/end` 继续表达实际请求窗口中的首尾 sample，而非 base cache 的全范围；base 全范围保存在内部 metadata，以免改变现有 response 语义。

#### Filter、SOP/EOP 与 X/Z

1. 不带 filter 的 summary/first/last/window 可直接基于 candidate id；`line_limit` 仅限制 materialize 数量。
2. beat stream filter 在候选 transfer 对应的 field column 上求值；packet stream filter 根据 `position=sop|eop` 取得完整 packet 的首/尾 beat field。
3. 当窗口截掉所需 SOP/EOP，或需要比较的逻辑位含 X/Z 时，仍产生现有的 `unresolved_filter_count`，而不是把未知值误判为不匹配。
4. channel filter 先使用 lazy channel index（已建时），否则在候选集上比较；channel 未知/X/Z 时维持现有“不匹配而非已知匹配”的行为。
5. `first_matched_packet` / `last_matched_packet` 必须按完整时间序取值，不能因 `line_limit` 提前停止筛选而改变 summary 或证据。

#### 各 query 类型

| query | 缓存后实现 |
|---|---|
| `summary` | 读取基础累计计数；若有窗口/filter，则对候选 id 做派生统计。 |
| `first_transfer` / `last_transfer` | 从候选 `transfer_ids` 首/尾取一个 sample id 后 materialize 单行。 |
| `transfer_window` | 只 materialize 前 `line_limit` 个候选；summary 仍基于完整候选集。 |
| `first_stall` / `last_stall` / `stall_window` | 在 `stalls` 的 sample-id range 上二分/遍历，不重新走 clock scan。 |
| `first_packet` / `last_packet` / `packet_window` | 由 packet table 和 beat span materialize；filter 时使用完整 packet 边界字段。 |
| `packet_at` | `packet_id -> packets[packet_id]` 的 O(1) 查找，再判断是否与请求窗口相交。 |

### 6.8 内存策略与压缩阈值

1. 第一版只做列式存储、field-id、packet beat span、延迟 materialize；不引入 bit-packing 以降低语义迁移风险。
2. cache entry 的 `estimated_bytes` 至少计入 vector capacity、string 内容、field registry、lazy index 和 packet mismatch 存储；容器控制块可采用保守固定开销估算。
3. `line_limit=-1` 或 export 不得把临时 JSON/导出缓冲归入 base cache；它们由请求自身的内存预算负责。
4. 只有 Stage 4 profile 显示 field value string/map 仍是 entry RSS 的主要来源，才进入 packed 4-state/value interning 阶段。该阶段需独立 benchmark，证明压缩收益大于 decode CPU 成本。
5. base entry 与 APB/AXI 共用 1 GiB engine 预算；LRU 以 entry 级淘汰。channel/selector lazy index 可以先独立淘汰，避免只为释放小索引而重扫大 entry。

### 6.9 stream 新增测试要求

除既有 `xdebug.stream` suite 外，新增下列测试；涉及真实 FSDB/NPI 的测试必须在沙箱外从 catalog gate/suite 入口执行。

#### 单元测试（不需要真实 FSDB）

1. `StreamBaseAnalysis` column-to-row materialization：固定 control、field、X/Z 与 packet beat span 输入，断言输出与现有 `StreamRow`/`StreamPacket` JSON 等价。
2. packet span：单包、多 beat、空包、interleaving channel、partial begin/end 的 range 交集和 packet_at O(1) 定位。
3. filter view：SOP/EOP 取首/尾 beat、stable field、mask/range/exact、X/Z unresolved、缺失边界 unresolved。
4. 时间索引：begin/end 落在 sample 前、sample 上、sample 后、空窗口、全窗口，断言二分边界无 off-by-one。
5. cache key：任一影响语义的 signal path、expression、reset polarity、edge/sample point、field schema、packet/interleaving 配置变化均导致 miss；仅 query/filter/line_limit/time_range 变化必须命中同一 base key。
6. entry lifecycle：build failure 不发布、同 key 单 builder、ready entry 只读、byte accounting、pin、LRU lazy-index-first eviction。

#### 真实 waveform 集成测试

1. 热命中：同一 session/config 连续执行 summary、first/last transfer、window、packet query、filter 和 channel query，断言 base scan 仅一次；每个 JSON 与关闭 cache 的基线逐字段相同。
2. time_range：完整 packet、窗口从 packet 中间开始、窗口在 packet 中间结束、窗口包住多包；断言 partial 标志、summary count 和首尾证据保持现有语义。
3. filter：beat stream 和 packet stream 分别覆盖 SOP/EOP、exact/range/mask、known hit、known miss、X/Z unresolved、窗口缺 SOP/EOP unresolved。
4. clock：negedge 默认、posedge-before、posedge-after、同 timestamp clock/data change；断言缓存前后相同，且不会回归既有 same-timestamp edge 行为。
5. reset/channel/interleaving：reset 中 transfer 不计入；channel 命中/未命中/X/Z；interleaving packet 互不串包。
6. config 生命周期：append 不使既有 stream entry miss；replace 同名 stream 立即失效并使用新 config；session.close/engine restart 后无旧 entry；FSDB identity 改变后全量失效。
7. 预算与重建：设置 `XDEBUG_ANALYSIS_CACHE_MAX_BYTES` 为不超过 128 MiB 的值，构造多个 stream entry 触发 LRU；断言未 pin entry 被淘汰、后续 request 自动完整重扫且 JSON 与基线一致。对 active cursor/pinned entry 验证不得被淘汰。
8. 内存与性能：记录 cold build、hot query 的 wall time，entry bytes 与 peak RSS；断言 hot query 不调用 scanner，且第一版结构压缩后的每 transfer/packet 字节数不劣于原样 `StreamAnalysis` 缓存原型。

## 7. 交叉 action、合同与错误处理

1. 所有 APB/AXI action（query、cursor、statistics、window、export、analysis、pair、timeline、outlier）必须通过同一 `ensure_*_analyzed` / cache lookup 入口，不能各自判断是否扫描。
2. `stream.query`、`stream.export` 和后续 stream action 必须通过同一 base-analysis lookup；否则会重新引入重复扫描。
3. 保持现有 `full_scan_count=1` 的 AXI 回归；新增 APB/stream 的 `scan_count` 内部测试断言，而非盲目把诊断字段公开。
4. cache build 失败不得写入 ready entry；同一请求返回现有分析错误。是否缓存 failure 可仅做短生命周期 negative cache，默认不实施，避免掩盖 transient FSDB/NPI 问题。
5. cache eviction、config 更新和 FSDB 变化必须使 cursor 行为可诊断，不得返回悬垂 transaction 指针。

## 8. 已确认的缓存合同

### 8.1 预算耗尽后的重建

缓存条目被 LRU 淘汰后，下一次同一 action **自动重新完整扫描 FSDB 并正常返回**。必须在内部 log/诊断记录 `cache_miss_after_evict`、淘汰原因、重建耗时和扫描次数；不新增 `CACHE_EVICTED` 公共错误，也不要求用户显式重建。

这保持既有 APB/AXI 的“请求即得到分析结果”合同。代价是大 FSDB 上淘汰后的某一次请求会变慢，因而 budget、eviction 和 rebuild 指标必须可观测。

### 8.2 默认缓存预算

第一版提供环境变量 `XDEBUG_ANALYSIS_CACHE_MAX_BYTES`，默认值为 **1 GiB（`1073741824` bytes）**；`0` 表示不限制；负数、非整数或溢出值为配置错误。预算按 engine 实例生效，涵盖 APB、AXI、stream 的 canonical result 与懒索引总和。

测试不得以 1 GiB 为前提或实际分配到该上限。缓存淘汰、pin、自动重建和输出等价性测试统一使用不超过 **128 MiB** 的显式小预算；fixture 应足以产生至少一次可验证的 LRU 淘汰。

## 9. 分阶段实施、测试与提交规划

各阶段都是可独立构建、回归、提交和回滚的边界。不得把 AXI timeline 压缩、stream 4-state packing 等 profile 驱动改动混入基础缓存或 canonical layout 提交。所有涉及真实 FSDB/NPI/VCS/VIP 的阶段测试均在沙箱外执行；每次先用 `pytest --xverif-gate <gate> --xverif-plan` 确认正式 suite，再运行目标 suite。

### Phase 0：基线测量与测试支架

**改动范围**

- 增加仅测试/内部日志可见的 scan/build/hit/miss/entry-bytes probes；不新增公共 action 参数或 response 字段。
- 为既有 APB、AXI、stream fixture 建立 baseline capture helper：首次扫描耗时、等价热 query 耗时、RSS/allocator 增量、transaction/transfer/beat/sample 数、现有 JSON baseline。
- 固化 `XDEBUG_ANALYSIS_CACHE_MAX_BYTES` 配置解析单元测试：默认 1 GiB、`0` 不限制、负数/非整数/溢出拒绝。

**阶段测试**

1. C++ unit：环境变量解析、metric 记录、JSON baseline comparator。
2. 静态检查：`python3 xdebug/tools/sync_runtime_request_schemas.py --check`、`python3 xdebug/tools/sync_axi_response_schemas.py --check`、`python3 xdebug/tools/sync_action_schema_hints.py --check`、`python3 xdebug/tools/audit_runtime_schema_compatibility.py`、`python3 xdebug/tools/validate_schema.py`、`python3 xdebug/tools/validate_examples.py`。
3. 沙箱外 baseline：分别运行 catalog 中的 APB、AXI、stream 目标 suite；只记录数据，不引入性能阈值失败。

**提交**

```text
测试：建立xdebug协议分析缓存的基线测量与预算配置校验

新增 APB/AXI/stream 首次与热查询的内部测量支架，校验 1GiB 默认预算和 0 不限制合同；未改变公共 action 输出。
验证：<实际 catalog suite 和静态检查结果>
```

### Phase 1：共享 cache lifecycle 基础设施

**改动范围**

- 新增 `xdebug/src/waveform/cache/analysis_cache.{h,cpp}` 与最小 `CacheKey`、entry metadata、LRU、byte accounting、pin/unpin、invalidate API。
- 实现 key 的 session/FSDB/config fingerprint 组件，但不改变任何 protocol 的 canonical result 布局。
- 支持自动重扫所需的 `evicted` 状态和内部 `cache_miss_after_evict` 日志；不公开 `CACHE_EVICTED`。

**阶段测试**

1. C++ unit：key 等价/差异、single-builder、build failure 不发布、LRU、byte budget、lazy-index-first eviction、pin/unpin、FSDB/session invalidation。
2. 128 MiB 以下预算：使用轻量 fake entry 触发淘汰与自动 rebuild，不启动真实 FSDB。
3. Phase 0 的全部静态 schema/contract 生成检查。

**提交**

```text
重构：引入xdebug协议分析缓存的生命周期与LRU基础设施

增加按 session、FSDB 和配置指纹隔离的内部缓存组件，支持字节预算、pin、失效和淘汰后自动重建；暂未迁移具体协议 action。
验证：<实际 C++ unit 和静态检查结果>
```

### Phase 2：AXI canonical transaction 去重与缓存接入

**改动范围**

- 用 `transactions + write/read/all/resp-time id view` 替换 `writes + reads + all` 的深拷贝 layout。
- 迁移 AXI query、cursor、analysis、statistics、export、pair/outlier、handshake 和 window helper 到 id resolve 路径。
- AXI entry 接入共享 cache lifecycle；先实现 canonical entry、时间 view、pin 和自动重扫。
- 本阶段只保留现有 outstanding sample 表示，不做 delta/checkpoint 压缩；Address/ID lazy index 可在本阶段实现，但不能与 timeline 压缩耦合。

**阶段测试**

1. C++ unit：arena/id view、排序、cursor、handshake id entry、include-data payload 保真、cache key/pin/LRU。
2. 沙箱外 `xdebug.axi_vip` 及其关联 contract suite：完整 action 矩阵、独立 VIP oracle、`full_scan_count=1` 热命中、128 MiB 以下预算的自动重扫。
3. 对同一 fixture 比较迁移前后 JSON；记录 canonical result bytes 和 peak RSS，要求没有 `all` payload 深拷贝。

**提交**

```text
优化：AXI分析缓存改为单一事务实体和索引视图

移除已完成 AXI 事务在 all 视图中的深拷贝，统一 query、cursor、analysis 与导出读取 canonical transaction，并接入缓存生命周期和自动重扫。
验证：<实际 AXI unit、VIP suite、contract 和内存测量结果>
```

### Phase 3：APB 紧凑 ref、地址索引与缓存接入

**改动范围**

- 将 `all` pointer view 改为 `ApbRef` 时间序 view；保留 reads/writes ownership。
- scan 时写入 known numeric address metadata；实现可独立淘汰的 lazy address index。
- 迁移 APB query、cursor、statistics、transfer window 到 ref resolve 和统一 cache lookup。
- 修复 address + line_limit 的重复 O(N × limit) 扫描，但不改变 selector 或输出合同。

**阶段测试**

1. C++ unit：ApbRef、address metadata/index、line-limit 复杂度计数、时间窗、cursor、cache lifecycle。
2. 沙箱外 `xdebug.apb_vip` 及其关联 contract suite：APB setup/access、wait-state、reset、error、selector、window、cursor、128 MiB 以下预算淘汰/自动重扫。
3. 同一 fixture 逐 JSON 比较迁移前后输出；记录 pointer view 与 ref view bytes、首次/热地址查询耗时。

**提交**

```text
优化：APB分析缓存使用紧凑引用和按地址懒索引

保留 APB 事务唯一 ownership，将全时序视图改为紧凑 ref，消除地址查询的重复全表扫描，并接入统一缓存失效和自动重建。
验证：<实际 APB unit、VIP suite、contract 和性能测量结果>
```

### Phase 4：stream base analysis cache 与 query view

**改动范围**

- 提取 `StreamBaseAnalysis` 与 `StreamQueryView`，首次以 column/vector/id/span 表达，不做 packed 4-state value arena。
- 将 `stream.query` 与 `stream.export`（如其依赖同一分析事实）收敛到同一 base-analysis lookup。
- 实现完整 FSDB 范围 base cache、time range 二分、packet span、临时 filter/materialize、config replace 精确失效。

**阶段测试**

1. C++ unit：column-to-row/packet materialize、time range、partial boundary、SOP/EOP filter、X/Z unresolved、channel/interleaving、key/lifecycle。
2. 沙箱外 `xdebug.stream` 及关联 contract suite：summary、first/last、window、packet_at、filter、channel、posedge before/after、same timestamp、reset、128 MiB 以下预算淘汰/自动重扫。
3. 所有 query 对关闭新缓存的 baseline 逐 JSON 比较；断言热 query 不调用 scanner，记录 base entry bytes/RSS。

**提交**

```text
优化：为stream查询引入基础分析缓存和紧凑查询视图

将完整无过滤 stream 事实与请求级时间窗、packet、filter 输出分离，复用一次时钟扫描结果，并保持边界和四态过滤语义。
验证：<实际 stream unit、waveform suite、contract 和性能测量结果>
```

### Phase 5：AXI outstanding timeline 压缩

**前置条件**：Phase 2 已稳定通过真实 AXI VIP 回归，并且 baseline/profile 证明 `outstanding_samples` 是明显 RSS 热点。

**改动范围**

- 用 global change point、per-ID delta、checkpoint 替代每 sample 的 per-ID map 复制。
- 仅迁移 timeline/statistics/outlier 所需的读取路径；不得修改 AXI transaction pairing 或 selector layout。

**阶段测试**

1. C++ unit：delta 聚合、checkpoint 回放、同 timestamp、稀疏/密集变化、per-ID 与朴素 oracle 对照。
2. 沙箱外 AXI VIP：timeline、peak、final、first_nonzero、per-ID、statistics/outlier 在多窗口上的逐 JSON/VIP oracle 对照。
3. 记录 timeline bytes 下降和回放耗时；若内存收益不足以覆盖明显的查询回归，则不提交该阶段。

**提交**

```text
优化：压缩AXI outstanding时间线的重复状态快照

以变化点、ID增量和检查点重建 outstanding 状态，减少长波形中每采样点复制 ID 映射的常驻内存，并保持 timeline 与统计输出一致。
验证：<实际 AXI unit、VIP oracle、内存和窗口查询测量结果>
```

### Phase 6：stream value packing / interning（仅 profile 驱动）

**前置条件**：Phase 4 profile 证明 field value string/map 仍是 stream entry 的主要内存来源，且 column/span 结构本身不足以达到目标。

**改动范围**

- 增加 fixed-width packed 4-state value arena、重复值 interning 和可逆 materialization。
- 不改变 `StreamBaseAnalysis`/`StreamQueryView` 的上层语义，不与 packet/filter 逻辑改动混合提交。

**阶段测试**

1. C++ unit：0/1/X/Z、不同宽度、同值 interning、value decode、mask/range/exact filter 与未压缩 oracle 对照。
2. 沙箱外 stream waveform：带 X/Z 的 field、packet stable field、filter、export、time window 逐 JSON 基线对照。
3. 记录每 beat bytes、entry bytes、peak RSS、热 query decode 开销；若压缩收益不足或输出延迟明显退化，则不提交。

**提交**

```text
优化：压缩stream基础分析中的四态字段值存储

使用可逆四态值编码和重复值复用降低 stream 缓存常驻内存，保持 filter、X/Z 和 JSON materialize 语义不变。
验证：<实际 stream unit、waveform suite、内存与热查询测量结果>
```

## 10. 提交前通用门禁

每个源码提交前都必须运行与该阶段相关的正式 catalog suite，并至少运行：

1. `python3 xdebug/tools/sync_runtime_request_schemas.py --check`
2. `python3 xdebug/tools/sync_axi_response_schemas.py --check`
3. `python3 xdebug/tools/sync_action_schema_hints.py --check`
4. `python3 xdebug/tools/audit_runtime_schema_compatibility.py`
5. `python3 xdebug/tools/validate_schema.py`
6. `python3 xdebug/tools/validate_examples.py`

若任一变更触及真实 FSDB/NPI/VIP，则相应 `xdebug.contract`、`xdebug.apb_vip`、`xdebug.axi_vip` 或 `xdebug.stream` suite 必须整体在沙箱外运行。提交前执行 `git status --short`，只显式暂存本阶段文件；commit message 使用本节给出的中文范围、动机与验证模板。发现 baseline 无关漂移时，不在当前缓存提交中顺手修复，应拆分报告或独立提交。

## 11. 风险与缓解

| 风险 | 缓解 |
|---|---|
| cache key 漏掉 clock/reset/config 字段，返回旧语义 | config fingerprint 覆盖完整规范化 config；config.load 同步 invalidate；加入等 name 不同 config 的 unit test。 |
| 指针因 eviction 或 vector 扩容失效 | canonical entry freeze 后再发放指针；cursor pin；用 id view 而非复制对象。 |
| AXI raw hex 与 stream binary/prefixed value 被错误统一 | cache 框架只管理生命周期，协议 decoder 与 Clock scanner 的 value format 不合并。 |
| stream packet/window/filter 边界语义漂移 | base analysis 保留完整范围；query view 仅派生；用 partial packet、SOP/EOP、same timestamp 回归。 |
| 为省内存淘汰 payload 导致 `include_data` 行为不确定 | Stage 4 前不淘汰 payload；任何后续重建/error 选择先走 schema/合同评审。 |
| 预算过小导致反复扫描 | 记录 evict-rebuild 频率；默认值与 LRU 根据真实 fixture profile 调整。 |

## 12. 结论

先做“领域结果缓存的统一生命周期”，不要做“原始 sample 的三协议共享缓存”。实施优先级应是：AXI 消除深拷贝、APB lazy 地址索引、stream 基础分析缓存；随后才依据 profile 决定 AXI timeline delta 与 stream packed-value 存储。这样可逐阶段获得可测的内存和热查询收益，并把协议语义风险限制在各自 decoder 内。
