# xdebug stream report

本报告由 `scripts/xdebug_stream_report.py` 生成，输入为仿真产出的 FSDB。

| case | stream | validate | transfers | stalls | first transfer | export rows |
| --- | --- | --- | ---: | ---: | --- | ---: |
| root_dual | root_master | ok | 8 | 2 | 60ns | 8 |
| root_dual | root_slave | ok | 8 | 2 | 60ns | 8 |
| mode08_observe | mode08_A | ok | 4 | 2 | 80ns | 4 |
| mode08_observe | mode08_B | ok | 4 | 2 | 80ns | 4 |
| mode10_pair | mode10_master | ok | 8 | 2 | 60ns | 8 |
| mode10_pair | mode10_slave | ok | 8 | 2 | 60ns | 8 |

## Artifacts

- `root_dual/root_master`: `reports/xdebug_stream_exports/root_dual_root_master.tsv`
- `root_dual/root_slave`: `reports/xdebug_stream_exports/root_dual_root_slave.tsv`
- `mode08_observe/mode08_A`: `reports/xdebug_stream_exports/mode08_observe_mode08_A.tsv`
- `mode08_observe/mode08_B`: `reports/xdebug_stream_exports/mode08_observe_mode08_B.tsv`
- `mode10_pair/mode10_master`: `reports/xdebug_stream_exports/mode10_pair_mode10_master.tsv`
- `mode10_pair/mode10_slave`: `reports/xdebug_stream_exports/mode10_pair_mode10_slave.tsv`
