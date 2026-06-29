# xcov action xout examples

本文件由真实 VDB `/home/yian/uart_example/sim/merged.vdb` 生成。每个条目包含请求 JSON 和真实 `xout` 返回。

生成约束：使用真实 NPI backend，不解析 URG HTML，不包含旧 action。

## actions

### Request

```json
{"api_version":"xcov.v1","request_id":"actions","action":"actions"}
```

### XOUT

```text
XOUT_BEGIN request_id=actions action=actions
@xcov.v1 ok action=actions request_id=actions

summary:
  matched_count: 18
  returned: 18
  truncated: false
  output_path: null

items:
  - name=session.open status=p0 api_version=xcov.v1
  - name=session.status status=p0 api_version=xcov.v1
  - name=session.close status=p0 api_version=xcov.v1
  - name=tests.list status=p0 api_version=xcov.v1
  - name=metrics.list status=p0 api_version=xcov.v1
  - name=scope.summary status=p0 api_version=xcov.v1
  - name=scope.children status=p0 api_version=xcov.v1
  - name=scope.search status=p0 api_version=xcov.v1
  - name=code_coverage.summary status=p0 api_version=xcov.v1
  - name=code_coverage.holes status=p0 api_version=xcov.v1
  - name=functional.summary status=p0 api_version=xcov.v1
  - name=functional.holes status=p0 api_version=xcov.v1
  - name=source.map status=p0 api_version=xcov.v1
  - name=source.annotate status=p0 api_version=xcov.v1
  - name=assert.report status=p0 api_version=xcov.v1
  - name=export.code_coverage status=p0 api_version=xcov.v1
  - name=export.function_coverage status=p0 api_version=xcov.v1
  - name=export.assert status=p0 api_version=xcov.v1

XOUT_END request_id=actions
```

## schema

### Request

```json
{"api_version":"xcov.v1","request_id":"schema-code-summary","action":"schema","args":{"action":"code_coverage.summary"}}
```

### XOUT

```text
XOUT_BEGIN request_id=schema-code-summary action=schema
@xcov.v1 ok action=schema request_id=schema-code-summary

summary:
  matched_count: 1
  returned: 1
  truncated: false
  output_path: null

XOUT_END request_id=schema-code-summary
```

## session.open

### Request

```json
{"api_version":"xcov.v1","request_id":"open","action":"session.open","target":{"vdb":"/home/yian/uart_example/sim/merged.vdb"},"args":{"name":"live-doc","reuse":false}}
```

### XOUT

```text
XOUT_BEGIN request_id=open action=session.open
@xcov.v1 ok action=session.open request_id=open

summary:
  session_id: live-doc
  state: alive
  vdb: /home/yian/uart_example/sim/merged.vdb
  test_count: 1
  top_scope_count: null
  worker: npi_python
  matched_count: 1
  returned: 1
  truncated: false
  output_path: null

XOUT_END request_id=open
```

## session.status

### Request

```json
{"api_version":"xcov.v1","request_id":"session-status","action":"session.status","target":{"session_id":"live-doc"}}
```

### XOUT

```text
XOUT_BEGIN request_id=session-status action=session.status
@xcov.v1 ok action=session.status request_id=session-status

summary:
  session_id: live-doc
  state: alive
  vdb: /home/yian/uart_example/sim/merged.vdb
  test_count: 1
  top_scope_count: null
  worker: npi_python
  cached_indexes: lazy
  matched_count: 1
  returned: 1
  truncated: false
  output_path: null

XOUT_END request_id=session-status
```

## tests.list

### Request

```json
{"api_version":"xcov.v1","request_id":"tests-list","action":"tests.list","target":{"session_id":"live-doc"}}
```

### XOUT

```text
XOUT_BEGIN request_id=tests-list action=tests.list
@xcov.v1 ok action=tests.list request_id=tests-list

summary:
  matched_count: 1
  returned: 1
  truncated: false
  output_mode: inline
  output_path: null
  artifact_format: json
  session_id: live-doc

filters:
  include: 
  exclude: 
  match_field: name

items:
  - name=/home/yian/uart_example/sim/merged.vdb/test

XOUT_END request_id=tests-list
```

## metrics.list

### Request

```json
{"api_version":"xcov.v1","request_id":"metrics-list","action":"metrics.list","target":{"session_id":"live-doc"}}
```

### XOUT

```text
XOUT_BEGIN request_id=metrics-list action=metrics.list
@xcov.v1 ok action=metrics.list request_id=metrics-list

summary:
  matched_count: 7
  returned: 7
  truncated: false
  output_mode: inline
  output_path: null
  artifact_format: json
  session_id: live-doc
  scope: null
  test: merged

items:
  - metric=line covered=427 coverable=839 missing=412 coverage_pct=50.8939 name=line full_name=line
  - metric=toggle covered=866 coverable=1652 missing=786 coverage_pct=52.4213 name=toggle full_name=toggle
  - metric=branch covered=230 coverable=430 missing=200 coverage_pct=53.4884 name=branch full_name=branch
  - metric=condition covered=488 coverable=598 missing=110 coverage_pct=81.6054 name=condition full_name=condition
  - metric=fsm covered=45 coverable=66 missing=21 coverage_pct=68.1818 name=fsm full_name=fsm
  - metric=assert covered=32 coverable=38 missing=6 coverage_pct=84.2105 name=assert full_name=assert
  - metric=functional covered=2946 coverable=4138 missing=1192 coverage_pct=71.1938 name=functional full_name=functional

XOUT_END request_id=metrics-list
```

## scope.summary

### Request

```json
{"api_version":"xcov.v1","request_id":"scope-summary","action":"scope.summary","target":{"session_id":"live-doc"},"args":{"scope":"uart_tb"}}
```

### XOUT

```text
XOUT_BEGIN request_id=scope-summary action=scope.summary
@xcov.v1 ok action=scope.summary request_id=scope-summary

summary:
  matched_count: 1
  returned: 1
  truncated: false
  output_mode: inline
  output_path: null
  artifact_format: json
  session_id: live-doc
  scope: uart_tb
  test: merged

filters:
  include: 
  exclude: 
  match_field: full_name

items:
  - name=uart_tb full_name=uart_tb parent=null depth=0 type=npiCovInstance def_name=uart_tb covered=2052 coverable=2900 missing=848 coverage_pct=70.7586 file=/home/yian/uart_example/sim/../uvm_tb/tb/uart_tb.sv line=20 line_pct=95.7547 toggle_pct=52.8049 branch_pct=93.2489 condition_pct=97.1944 fsm_pct=68.1818 assert_pct=92.3077 functional_pct=62.5

XOUT_END request_id=scope-summary
```

## scope.children

### Request

```json
{"api_version":"xcov.v1","request_id":"scope-children","action":"scope.children","target":{"session_id":"live-doc"},"args":{"scope":"uart_tb","limits":{"max_items":5}}}
```

### XOUT

```text
XOUT_BEGIN request_id=scope-children action=scope.children
@xcov.v1 ok action=scope.children request_id=scope-children

summary:
  matched_count: 7
  returned: 5
  truncated: true
  output_mode: inline
  output_path: null
  artifact_format: json
  session_id: live-doc
  scope: uart_tb
  test: merged

filters:
  include: 
  exclude: 
  match_field: full_name

items:
  - name=APB full_name=uart_tb.APB parent=uart_tb depth=1 type=npiCovInstance def_name=apb_if covered=53 coverable=238 missing=185 coverage_pct=22.2689 file=/home/yian/uart_example/sim/../agents/apb_agent/apb_if.sv line=20 line_pct=null toggle_pct=21.6102 branch_pct=null condition_pct=null fsm_pct=null assert_pct=100.0 functional_pct=null
  - name=APB_PROTOCOL_MONITOR full_name=uart_tb.APB_PROTOCOL_MONITOR parent=uart_tb depth=1 type=npiCovInstance def_name=apb_monitor covered=74 coverable=232 missing=158 coverage_pct=31.8966 file=/home/yian/uart_example/sim/../protocol_monitor/apb_monitor.sv line=22 line_pct=100.0 toggle_pct=24.7573 branch_pct=null condition_pct=null fsm_pct=null assert_pct=100.0 functional_pct=62.5
  - name=DUT full_name=uart_tb.DUT parent=uart_tb depth=1 type=npiCovInstance def_name=uart_16550 covered=1873 coverable=2377 missing=504 coverage_pct=78.7968 file=/home/yian/uart_example/sim/../rtl/uart/uart_16550.sv line=29 line_pct=95.5556 toggle_pct=62.7367 branch_pct=93.2489 condition_pct=97.1944 fsm_pct=68.1818 assert_pct=75.0 functional_pct=null
  - name=IRQ full_name=uart_tb.IRQ parent=uart_tb depth=1 type=npiCovInstance def_name=interrupt_if covered=6 coverable=6 missing=0 coverage_pct=100.0 file=/home/yian/uart_example/sim/../uvm_tb/tb/interrupt_if.sv line=20 line_pct=null toggle_pct=100.0 branch_pct=null condition_pct=null fsm_pct=null assert_pct=null functional_pct=null
  - name=MODEM full_name=uart_tb.MODEM parent=uart_tb depth=1 type=npiCovInstance def_name=modem_if covered=18 coverable=18 missing=0 coverage_pct=100.0 file=/home/yian/uart_example/sim/../agents/modem_agent/modem_if.sv line=20 line_pct=null toggle_pct=100.0 branch_pct=null condition_pct=null fsm_pct=null assert_pct=null functional_pct=null

XOUT_END request_id=scope-children
```

## scope.search

### Request

```json
{"api_version":"xcov.v1","request_id":"scope-search","action":"scope.search","target":{"session_id":"live-doc"},"args":{"query":{"include_patterns":["*uart*"],"match_field":"full_name"},"limits":{"max_items":5}}}
```

### XOUT

```text
XOUT_BEGIN request_id=scope-search action=scope.search
@xcov.v1 ok action=scope.search request_id=scope-search

summary:
  matched_count: 15
  returned: 5
  truncated: true
  output_mode: inline
  output_path: null
  artifact_format: json
  session_id: live-doc
  scope: null
  test: merged

filters:
  include: *uart*
  exclude: 
  match_field: full_name

items:
  - name=uart_seq_pkg full_name=uart_seq_pkg
  - name=uart_tb full_name=uart_tb
  - name=uart_vseq_pkg full_name=uart_vseq_pkg
  - name=APB full_name=uart_tb.APB
  - name=APB_PROTOCOL_MONITOR full_name=uart_tb.APB_PROTOCOL_MONITOR

XOUT_END request_id=scope-search
```

## code_coverage.summary

### Request

```json
{"api_version":"xcov.v1","request_id":"code-summary","action":"code_coverage.summary","target":{"session_id":"live-doc"},"args":{"group_by":"metric"}}
```

### XOUT

```text
XOUT_BEGIN request_id=code-summary action=code_coverage.summary
@xcov.v1 ok action=code_coverage.summary request_id=code-summary

summary:
  matched_count: 6
  returned: 6
  truncated: false
  output_mode: inline
  output_path: null
  artifact_format: json
  session_id: live-doc
  scope: null
  test: merged
  metrics: line,toggle,branch,condition,fsm,assert

filters:
  include: 
  exclude: 
  match_field: full_name

items:
  - metric=line covered=427 coverable=839 missing=412 coverage_pct=50.8939 name=line full_name=line
  - metric=toggle covered=866 coverable=1652 missing=786 coverage_pct=52.4213 name=toggle full_name=toggle
  - metric=branch covered=230 coverable=430 missing=200 coverage_pct=53.4884 name=branch full_name=branch
  - metric=condition covered=488 coverable=598 missing=110 coverage_pct=81.6054 name=condition full_name=condition
  - metric=fsm covered=45 coverable=66 missing=21 coverage_pct=68.1818 name=fsm full_name=fsm
  - metric=assert covered=32 coverable=38 missing=6 coverage_pct=84.2105 name=assert full_name=assert

XOUT_END request_id=code-summary
```

## code_coverage.holes

### Request

```json
{"api_version":"xcov.v1","request_id":"code-holes","action":"code_coverage.holes","target":{"session_id":"live-doc"},"args":{"scope":"uart_tb","metrics":["line","toggle","branch","condition","fsm","assert"],"limits":{"max_items":8}}}
```

### XOUT

```text
XOUT_BEGIN request_id=code-holes action=code_coverage.holes
@xcov.v1 ok action=code_coverage.holes request_id=code-holes

summary:
  matched_count: 4
  returned: 4
  truncated: false
  output_mode: inline
  output_path: null
  artifact_format: json
  session_id: live-doc
  scope: uart_tb
  test: merged
  metrics: line,toggle,branch,condition,fsm,assert
  note: Detailed uncovered code coverage items are available via export.code_coverage. For complex processing, use x-npi and learn the pynpi coverage APIs.

filters:
  include: 
  exclude: 
  match_field: full_name

items:
  - name=uart_tb full_name=uart_tb parent=null depth=0 type=npiCovInstance def_name=uart_tb covered=2047 coverable=2892 missing=845 coverage_pct=70.7815 file=/home/yian/uart_example/sim/../uvm_tb/tb/uart_tb.sv line=20 line_pct=95.7547 toggle_pct=52.8049 branch_pct=93.2489 condition_pct=97.1944 fsm_pct=68.1818 assert_pct=92.3077 functional_pct=null
  - name=APB full_name=uart_tb.APB parent=uart_tb depth=1 type=npiCovInstance def_name=apb_if covered=53 coverable=238 missing=185 coverage_pct=22.2689 file=/home/yian/uart_example/sim/../agents/apb_agent/apb_if.sv line=20 line_pct=null toggle_pct=21.6102 branch_pct=null condition_pct=null fsm_pct=null assert_pct=100.0 functional_pct=null
  - name=APB_PROTOCOL_MONITOR full_name=uart_tb.APB_PROTOCOL_MONITOR parent=uart_tb depth=1 type=npiCovInstance def_name=apb_monitor covered=69 coverable=224 missing=155 coverage_pct=30.8036 file=/home/yian/uart_example/sim/../protocol_monitor/apb_monitor.sv line=22 line_pct=100.0 toggle_pct=24.7573 branch_pct=null condition_pct=null fsm_pct=null assert_pct=100.0 functional_pct=null
  - name=DUT full_name=uart_tb.DUT parent=uart_tb depth=1 type=npiCovInstance def_name=uart_16550 covered=1873 coverable=2377 missing=504 coverage_pct=78.7968 file=/home/yian/uart_example/sim/../rtl/uart/uart_16550.sv line=29 line_pct=95.5556 toggle_pct=62.7367 branch_pct=93.2489 condition_pct=97.1944 fsm_pct=68.1818 assert_pct=75.0 functional_pct=null

XOUT_END request_id=code-holes
```

## functional.summary

### Request

```json
{"api_version":"xcov.v1","request_id":"functional-summary","action":"functional.summary","target":{"session_id":"live-doc"},"args":{"group_by":"covergroup","limits":{"max_items":5}}}
```

### XOUT

```text
XOUT_BEGIN request_id=functional-summary action=functional.summary
@xcov.v1 ok action=functional.summary request_id=functional-summary

summary:
  matched_count: 15
  returned: 5
  truncated: true
  output_mode: inline
  output_path: null
  artifact_format: json
  session_id: live-doc
  test: merged

filters:
  include: 
  exclude: 
  match_field: full_name

items:
  - covergroup=uart_tb.APB_PROTOCOL_MONITOR::APB_accesses_cg metric=summary name=uart_tb.APB_PROTOCOL_MONITOR::APB_accesses_cg full_name=uart_tb.APB_PROTOCOL_MONITOR::APB_accesses_cg coverage_pct=66.6667 score_basis=average_direct_coverpoint_cross_pct score_item_count=3 raw_covered=5 raw_coverable=8 raw_missing=3 raw_coverage_pct=62.5 covered=5 coverable=8 missing=3
  - covergroup=modem_agent_pkg::modem_coverage_monitor::modem_lines_cg metric=summary name=modem_agent_pkg::modem_coverage_monitor::modem_lines_cg full_name=modem_agent_pkg::modem_coverage_monitor::modem_lines_cg coverage_pct=100.0 score_basis=average_direct_coverpoint_cross_pct score_item_count=9 raw_covered=272 raw_coverable=272 raw_missing=0 raw_coverage_pct=100.0 covered=272 coverable=272 missing=0
  - covergroup=uart_env_pkg::uart_interrupt_coverage_monitor::tx_word_format_int_cg metric=summary name=uart_env_pkg::uart_interrupt_coverage_monitor::tx_word_format_int_cg full_name=uart_env_pkg::uart_interrupt_coverage_monitor::tx_word_format_int_cg coverage_pct=100.0 score_basis=average_direct_coverpoint_cross_pct score_item_count=4 raw_covered=51 raw_coverable=51 raw_missing=0 raw_coverage_pct=100.0 covered=51 coverable=51 missing=0
  - covergroup=uart_env_pkg::uart_rx_coverage_monitor::rx_word_format_cg metric=summary name=uart_env_pkg::uart_rx_coverage_monitor::rx_word_format_cg full_name=uart_env_pkg::uart_rx_coverage_monitor::rx_word_format_cg coverage_pct=100.0 score_basis=average_direct_coverpoint_cross_pct score_item_count=4 raw_covered=51 raw_coverable=51 raw_missing=0 raw_coverage_pct=100.0 covered=51 coverable=51 missing=0
  - covergroup=uart_env_pkg::uart_reg_access_coverage_monitor::reg_access_cg metric=summary name=uart_env_pkg::uart_reg_access_coverage_monitor::reg_access_cg full_name=uart_env_pkg::uart_reg_access_coverage_monitor::reg_access_cg coverage_pct=100.0 score_basis=average_direct_coverpoint_cross_pct score_item_count=3 raw_covered=27 raw_coverable=27 raw_missing=0 raw_coverage_pct=100.0 covered=27 coverable=27 missing=0

XOUT_END request_id=functional-summary
```

## functional.holes

### Request

```json
{"api_version":"xcov.v1","request_id":"functional-holes","action":"functional.holes","target":{"session_id":"live-doc"},"args":{"levels":["bin"],"limits":{"max_items":5}}}
```

### XOUT

```text
XOUT_BEGIN request_id=functional-holes action=functional.holes
@xcov.v1 ok action=functional.holes request_id=functional-holes

summary:
  matched_count: 410
  returned: 5
  truncated: true
  output_mode: inline
  output_path: null
  artifact_format: json
  session_id: live-doc
  test: merged

filters:
  include: 
  exclude: 
  match_field: full_name

items:
  - metric=functional type=npiCovCoverBin scope=uart_tb.APB_PROTOCOL_MONITOR name=err full_name=uart_tb.APB_PROTOCOL_MONITOR::APB_accesses_cg.ERR.err covered=0 coverable=1 missing=1 count=0 coverage_pct=0.0 status=not_covered file=/home/yian/uart_example/sim/../protocol_monitor/apb_monitor.sv line=130 covergroup=uart_tb.APB_PROTOCOL_MONITOR::APB_accesses_cg coverpoint=ERR bin=err evidence_source.inherited=true evidence_source.type=npiCovCoverpoint evidence_source.name=ERR evidence_source.full_name=uart_tb.APB_PROTOCOL_MONITOR::APB_accesses_cg.ERR
  - metric=functional type=npiCovCoverBin scope=uart_tb.APB_PROTOCOL_MONITOR name=[write|err] full_name=uart_tb.APB_PROTOCOL_MONITOR::APB_accesses_cg.APB_CVR.[write|err] covered=0 coverable=1 missing=1 count=0 coverage_pct=0.0 status=not_covered file=/home/yian/uart_example/sim/../protocol_monitor/apb_monitor.sv line=135 covergroup=uart_tb.APB_PROTOCOL_MONITOR::APB_accesses_cg cross=APB_CVR bin=[write|err] evidence_source.inherited=true evidence_source.type=npiCovCross evidence_source.name=APB_CVR evidence_source.full_name=uart_tb.APB_PROTOCOL_MONITOR::APB_accesses_cg.APB_CVR
  - metric=functional type=npiCovCoverBin scope=uart_tb.APB_PROTOCOL_MONITOR name=[read|err] full_name=uart_tb.APB_PROTOCOL_MONITOR::APB_accesses_cg.APB_CVR.[read|err] covered=0 coverable=1 missing=1 count=0 coverage_pct=0.0 status=not_covered file=/home/yian/uart_example/sim/../protocol_monitor/apb_monitor.sv line=135 covergroup=uart_tb.APB_PROTOCOL_MONITOR::APB_accesses_cg cross=APB_CVR bin=[read|err] evidence_source.inherited=true evidence_source.type=npiCovCross evidence_source.name=APB_CVR evidence_source.full_name=uart_tb.APB_PROTOCOL_MONITOR::APB_accesses_cg.APB_CVR
  - metric=functional type=npiCovCoverBin scope=null name=subsumed full_name=uart_env_pkg::uart_reg_access_coverage_monitor::reg_access_cg.REG_ACCESS.subsumed covered=0 coverable=2 missing=2 count=0 coverage_pct=0.0 status=not_covered file=/home/yian/uart_example/sim/../uvm_tb/env/uart_reg_access_coverage_monitor.svh line=48 covergroup=uart_env_pkg::uart_reg_access_coverage_monitor::reg_access_cg cross=REG_ACCESS bin=subsumed evidence_source.inherited=true evidence_source.type=npiCovCross evidence_source.name=REG_ACCESS evidence_source.full_name=uart_env_pkg::uart_reg_access_coverage_monitor::reg_access_cg.REG_ACCESS
  - metric=functional type=npiCovCoverBin scope=null name=no_ints full_name=uart_env_pkg::uart_interrupt_coverage_monitor::lsr_int_src_cg.LINE_STATUS_SRC.no_ints covered=0 coverable=1 missing=1 count=0 coverage_pct=0.0 status=not_covered file=/home/yian/uart_example/sim/../uvm_tb/env/uart_interrupt_coverage_monitor.svh line=249 covergroup=uart_env_pkg::uart_interrupt_coverage_monitor::lsr_int_src_cg coverpoint=LINE_STATUS_SRC bin=no_ints evidence_source.inherited=true evidence_source.type=npiCovCoverpoint evidence_source.name=LINE_STATUS_SRC evidence_source.full_name=uart_env_pkg::uart_interrupt_coverage_monitor::lsr_int_src_cg.LINE_STATUS_SRC

XOUT_END request_id=functional-holes
```

## source.map

### Request

```json
{"api_version":"xcov.v1","request_id":"source-map","action":"source.map","target":{"session_id":"live-doc"},"args":{"file":"host_if_seq_pkg.sv","line":20,"window":0,"metrics":["assert"],"limits":{"max_items":5}}}
```

### XOUT

```text
XOUT_BEGIN request_id=source-map action=source.map
@xcov.v1 ok action=source.map request_id=source-map

summary:
  matched_count: 0
  returned: 0
  truncated: false
  output_mode: inline
  output_path: null
  artifact_format: json
  session_id: live-doc
  file: host_if_seq_pkg.sv
  line: 20
  window: 0

filters:
  include: 
  exclude: 
  match_field: full_name

items:

XOUT_END request_id=source-map
```

## source.annotate

### Request

```json
{"api_version":"xcov.v1","request_id":"source-annotate","action":"source.annotate","target":{"session_id":"live-doc"},"args":{"file":"host_if_seq_pkg.sv","line":20,"window":0,"metrics":["assert"],"include_source_text":true,"limits":{"max_items":5}}}
```

### XOUT

```text
XOUT_BEGIN request_id=source-annotate action=source.annotate
@xcov.v1 ok action=source.annotate request_id=source-annotate

summary:
  matched_count: 0
  returned: 0
  truncated: false
  output_mode: inline
  output_path: null
  artifact_format: json
  note: source text is unavailable from file path; coverage annotations still use NPI evidence
  session_id: live-doc
  file: host_if_seq_pkg.sv
  line: 20
  window: 0
  include_source_text: true

filters:
  include: 
  exclude: 
  match_field: full_name

items:

XOUT_END request_id=source-annotate
```

## assert.report

### Request

```json
{"api_version":"xcov.v1","request_id":"assert-report","action":"assert.report","target":{"session_id":"live-doc"},"args":{"limits":{"max_items":5}}}
```

### XOUT

```text
XOUT_BEGIN request_id=assert-report action=assert.report
@xcov.v1 ok action=assert.report request_id=assert-report

summary:
  matched_count: 38
  returned: 5
  truncated: true
  output_mode: inline
  output_path: null
  artifact_format: json
  session_id: live-doc
  scope: null
  test: merged

filters:
  include: 
  exclude: 
  match_field: full_name

sections:
  category_summary:
    - category=0 count=38
  severity_summary:
    - severity=0 count=38
  assert_summary: kind=assertion total=36 success=30 failure=0 incomplete=7 without_attempts=4 attempts=271984123 real_successes=57053044 first_match=0
  cover_property_summary: kind=cover_property total=1 success=1 failure=0 incomplete=0 without_attempts=0 attempts=11332430 real_successes=11332430 first_match=0
  cover_sequence_summary: kind=cover_sequence total=1 success=1 failure=0 incomplete=0 without_attempts=0 attempts=11332430 real_successes=19627 first_match=19627

items:
  - kind=assertion name=TX_BUSY_CHK full_name=TX_BUSY_CHK category=0 severity=0 covered=1 coverable=1 missing=0 coverage_pct=100.0 status=covered attempts=11332430 real_successes=878 failures=0 incomplete=6 first_match=0 without_attempts=0 file=/home/yian/uart_example/sim/../rtl/uart/uart_tx.sv line=250
  - kind=assertion name=TX_FIFO_EMPTY_CHK full_name=TX_FIFO_EMPTY_CHK category=0 severity=0 covered=1 coverable=1 missing=0 coverage_pct=100.0 status=covered attempts=11332430 real_successes=11103458 failures=0 incomplete=0 first_match=0 without_attempts=0 file=/home/yian/uart_example/sim/../rtl/uart/uart_tx_fifo.sv line=97
  - kind=assertion name=TX_FIFO_FULL_CHK full_name=TX_FIFO_FULL_CHK category=0 severity=0 covered=1 coverable=1 missing=0 coverage_pct=100.0 status=covered attempts=11332430 real_successes=3317 failures=0 incomplete=0 first_match=0 without_attempts=0 file=/home/yian/uart_example/sim/../rtl/uart/uart_tx_fifo.sv line=96
  - kind=assertion name=TX_FIFO_OK_CHK full_name=TX_FIFO_OK_CHK category=0 severity=0 covered=1 coverable=1 missing=0 coverage_pct=100.0 status=covered attempts=11332430 real_successes=225649 failures=0 incomplete=0 first_match=0 without_attempts=0 file=/home/yian/uart_example/sim/../rtl/uart/uart_tx_fifo.sv line=98
  - kind=assertion name=RX_BE_CHK full_name=RX_BE_CHK category=0 severity=0 covered=0 coverable=1 missing=1 coverage_pct=0.0 status=not_covered attempts=11332430 real_successes=0 failures=0 incomplete=0 first_match=0 without_attempts=0 file=/home/yian/uart_example/sim/../rtl/uart/uart_rx.sv line=302

XOUT_END request_id=assert-report
```

## export.code_coverage

### Request

```json
{"api_version":"xcov.v1","request_id":"export-code","action":"export.code_coverage","target":{"session_id":"live-doc"},"args":{"scope":"uart_tb","threshold_pct":100.0,"output":{"path":"/tmp/xcov-doc-code-coverage.md","allow_absolute_path":true}}}
```

### XOUT

```text
XOUT_BEGIN request_id=export-code action=export.code_coverage
@xcov.v1 ok action=export.code_coverage request_id=export-code

summary:
  session_id: live-doc
  scope: uart_tb
  test: merged
  threshold_pct: 100.0
  matched_count: 236
  returned: 0
  truncated: false
  output_mode: file
  output_path: /tmp/xcov-doc-code-coverage.md
  artifact_format: md
  note: Markdown export only. For complex processing, use x-npi and learn the pynpi coverage APIs.

items:

XOUT_END request_id=export-code
```

## export.function_coverage

### Request

```json
{"api_version":"xcov.v1","request_id":"export-function","action":"export.function_coverage","target":{"session_id":"live-doc"},"args":{"threshold_pct":100.0,"output":{"path":"/tmp/xcov-doc-function-coverage.md","allow_absolute_path":true}}}
```

### XOUT

```text
XOUT_BEGIN request_id=export-function action=export.function_coverage
@xcov.v1 ok action=export.function_coverage request_id=export-function

summary:
  session_id: live-doc
  scope: null
  test: merged
  threshold_pct: 100.0
  matched_count: 465
  returned: 0
  truncated: false
  output_mode: file
  output_path: /tmp/xcov-doc-function-coverage.md
  artifact_format: md
  note: Markdown export only. For complex processing, use x-npi and learn the pynpi coverage APIs.

items:

XOUT_END request_id=export-function
```

## export.assert

### Request

```json
{"api_version":"xcov.v1","request_id":"export-assert","action":"export.assert","target":{"session_id":"live-doc"},"args":{"scope":"uart_tb","threshold_pct":100.0,"output":{"path":"/tmp/xcov-doc-assert.md","allow_absolute_path":true}}}
```

### XOUT

```text
XOUT_BEGIN request_id=export-assert action=export.assert
@xcov.v1 ok action=export.assert request_id=export-assert

summary:
  session_id: live-doc
  scope: uart_tb
  test: merged
  threshold_pct: 100.0
  matched_count: 9
  returned: 0
  truncated: false
  output_mode: file
  output_path: /tmp/xcov-doc-assert.md
  artifact_format: md
  note: Markdown export only. For complex processing, use x-npi and learn the pynpi coverage APIs.

items:

XOUT_END request_id=export-assert
```

## session.close

### Request

```json
{"api_version":"xcov.v1","request_id":"session-close","action":"session.close","target":{"session_id":"live-doc"}}
```

### XOUT

```text
XOUT_BEGIN request_id=session-close action=session.close
@xcov.v1 ok action=session.close request_id=session-close

summary:
  session_id: live-doc
  state: closed
  vdb: /home/yian/uart_example/sim/merged.vdb
  test_count: 1
  top_scope_count: null
  worker: npi_python
  matched_count: 1
  returned: 1
  truncated: false
  output_path: null

XOUT_END request_id=session-close
```
